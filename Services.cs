using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace MyDict
{
    public class Entry
    {
        public string Word { get; set; }
        public string Translation { get; set; }
        public string Summary { get; set; }
        [System.Web.Script.Serialization.ScriptIgnore]
        public string ShortTranslation { get { var text=string.IsNullOrWhiteSpace(Summary)?Translation:Summary; text=(text??"").Replace("\r"," ").Replace("\n"," ").Trim();return text.Length>80?text.Substring(0,79)+"…":text; } }
        public string Pair { get; set; }
        public string SavedAt { get; set; }
        public string Details { get; set; }
        public DictionaryRecord Record { get; set; }
        public string DetailNote { get; set; }
        public string TranslationProvider { get; set; }
        public string TranslationUrl { get; set; }
        public string LookupSource { get; set; }
        public string LookupModel { get; set; }
        public string QueryEndpoint { get; set; }
        public string QueriedAtUtc { get; set; }
        public string SuccessfulQueryAtUtc { get; set; }
    }
    public static class Translator
    {
        static Translator() { ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12; }
        private static readonly HttpClient Client = new HttpClient { Timeout = TimeSpan.FromSeconds(20) };
        public static string DetectPair(string text) { return text.Any(c => (c >= 0x3400 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff)) ? "zh-CN|en" : "en|zh-CN"; }
        public static string CleanText(string text) { return Regex.Replace(text ?? "", @"\bdepending\s+on\s+context\s*[:：]\s*", "", RegexOptions.IgnoreCase).Trim(); }
        public static async Task<string> TranslateConfiguredAsync(string text, string pair, AppSettings settings, CancellationToken token = default(CancellationToken))
        {
            if (settings.Provider == "MyMemory") return await TranslateAsync(text, pair, token);
            settings.Validate();
            string[] languages = pair.Split('|');
            var body = new Dictionary<string, object> { {"q",text}, {"source",languages[0].StartsWith("zh") ? "zh" : "en"}, {"target",languages[1].StartsWith("zh") ? "zh" : "en"}, {"format","text"} };
            if (!string.IsNullOrEmpty(settings.ApiKey)) body["api_key"] = settings.ApiKey;
            using (var content = new StringContent(new JavaScriptSerializer().Serialize(body), Encoding.UTF8, "application/json"))
            using (var response = await Client.PostAsync(settings.LibreUrl, content, token))
            {
                if (!response.IsSuccessStatusCode) throw new InvalidOperationException("LibreTranslate 请求失败（HTTP " + (int)response.StatusCode + "），请检查地址、密钥和服务语言支持。");
                var data = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(await response.Content.ReadAsStringAsync());
                token.ThrowIfCancellationRequested(); object value;
                if (data == null || !data.TryGetValue("translatedText", out value) || string.IsNullOrWhiteSpace(Convert.ToString(value))) throw new InvalidOperationException("LibreTranslate 未返回有效翻译。");
                return CleanText(WebUtility.HtmlDecode(Convert.ToString(value)));
            }
        }
        public static async Task<string> TranslateAsync(string text, string pair, CancellationToken token = default(CancellationToken))
        {
            if (Encoding.UTF8.GetByteCount(text) > 500) throw new InvalidOperationException("单次查询最多 500 个 UTF-8 字节，请缩短输入。");
            var url = "https://api.mymemory.translated.net/get?q=" + Uri.EscapeDataString(text) + "&langpair=" + Uri.EscapeDataString(pair);
            string json;
            using (var response = await Client.GetAsync(url, token)) { response.EnsureSuccessStatusCode(); json = await response.Content.ReadAsStringAsync(); }
            token.ThrowIfCancellationRequested();
            var root = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);
            object status;
            if (root == null || !root.TryGetValue("responseStatus", out status) || Convert.ToString(status) != "200")
                throw new InvalidOperationException("翻译服务暂不可用或已达到免费额度，请稍后重试。");
            var data = root["responseData"] as Dictionary<string, object>;
            if (data == null || !data.ContainsKey("translatedText")) throw new InvalidOperationException("翻译服务返回了无效结果。");
            var result = WebUtility.HtmlDecode(Convert.ToString(data["translatedText"]));
            if (string.IsNullOrWhiteSpace(result)) throw new InvalidOperationException("没有找到翻译结果。");
            return CleanText(result);
        }
    }
    public class FavoriteStore
    {
        public readonly string FilePath;
        private FavoriteData data = new FavoriteData();
        public FavoriteStore(string path = null, string username = null) { FilePath = path ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MyDict", "favorites.json"); data.username = username; }
        public FavoriteData Snapshot() { return JsonFiles.Serializer().Deserialize<FavoriteData>(JsonFiles.Serializer().Serialize(data)); }
        public List<Entry> Entries { get { return data.Changes.Values.Where(c => !c.Deleted).OrderByDescending(c => c.UpdatedAtUtc).Select(c => c.Entry).ToList(); } }
        public void Merge(FavoriteData other) { CacheMerge.MergeFavorites(data, other); }
        public List<Entry> Load()
        {
            if (!File.Exists(FilePath)) return new List<Entry>();
            var text = File.ReadAllText(FilePath, Encoding.UTF8);
            if (text.TrimStart().StartsWith("[")) {
                var legacy = JsonFiles.Serializer().Deserialize<List<Entry>>(text);
                if (legacy == null) throw new InvalidOperationException("收藏结构无效。");
                foreach (var entry in legacy.Where(e => e != null && !string.IsNullOrWhiteSpace(e.Word))) data.Changes[CacheMerge.FavoriteKey(entry)] = new FavoriteChange { Entry = entry, UpdatedAtUtc = "1970-01-01T00:00:00.0000000Z" };
            } else {
                var loaded = JsonFiles.Serializer().Deserialize<FavoriteData>(text);
                if (loaded == null || loaded.Changes == null || loaded.Changes.Values.Any(c => c == null || c.Entry == null || string.IsNullOrWhiteSpace(c.Entry.Word))) throw new InvalidOperationException("收藏结构无效。");
                if (!string.IsNullOrEmpty(data.username) && !string.IsNullOrEmpty(loaded.username) && loaded.username != data.username) throw new InvalidOperationException("收藏 username 不匹配，已保护原文件。");
                loaded.username = data.username ?? loaded.username; data = loaded;
            }
            return Entries;
        }
        public void Update(List<Entry> entries)
        {
            var serializer = JsonFiles.Serializer();
            var desired = entries.ToDictionary(CacheMerge.FavoriteKey, e => e);
            foreach (var pair in data.Changes.ToList()) if (!pair.Value.Deleted && !desired.ContainsKey(pair.Key)) Change(pair.Key, pair.Value.Entry, true);
            foreach (var pair in desired) {
                FavoriteChange old;
                if (!data.Changes.TryGetValue(pair.Key, out old) || old.Deleted || serializer.Serialize(old.Entry) != serializer.Serialize(pair.Value)) Change(pair.Key, serializer.Deserialize<Entry>(serializer.Serialize(pair.Value)), false);
            }
        }
        private void Change(string key, Entry entry, bool deleted)
        {
            FavoriteChange old; DateTime previous;
            var now = DateTime.UtcNow;
            if (data.Changes.TryGetValue(key, out old) && DateTime.TryParse(old.UpdatedAtUtc, null, System.Globalization.DateTimeStyles.RoundtripKind, out previous) && previous >= now) now = previous.ToUniversalTime().AddTicks(1);
            data.Changes[key] = new FavoriteChange { Entry = entry, Deleted = deleted, UpdatedAtUtc = now.ToString("o") };
        }
        public void Save(List<Entry> entries)
        {
            Update(entries); JsonFiles.Write(FilePath, data);
        }
        public static string Csv(IEnumerable<Entry> entries)
        {
            Func<string, string> quote = s => { s = s ?? ""; if (s.Length > 0 && "=+-@\t\r\n".Contains(s[0])) s = "'" + s; return "\"" + s.Replace("\"", "\"\"") + "\""; };
            return "单词,翻译,方向,收藏时间,词典详解\r\n" + string.Join("\r\n", entries.Select(e => string.Join(",", new[] { e.Word, e.Translation, e.Pair, e.SavedAt, e.Details }.Select(quote))));
        }
    }
}








