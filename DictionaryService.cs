using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Threading;
using System.Web.Script.Serialization;

namespace MyDict
{
    public class DictionaryRecord
    {
        public string Word { get; set; }
        public string Phonetic { get; set; }
        public string Origin { get; set; }
        public WordForms Forms { get; set; } = new WordForms();
        public List<Meaning> Meanings { get; set; } = new List<Meaning>();
        public string Derived { get; set; }
        public string Notice { get; set; }
        public List<string> Sources { get; set; } = new List<string>();
        public string Format()
        {
            var b = new StringBuilder();
            b.AppendLine(Word).AppendLine("音标  " + (string.IsNullOrWhiteSpace(Phonetic) ? "来源暂未提供" : Phonetic));
            if (!string.IsNullOrWhiteSpace(Origin)) b.AppendLine("\n词源\n" + Origin);
            foreach (var meaning in Meanings)
            {
                b.AppendLine("\n" + meaning.PartOfSpeech + "  " + Forms?.ForPartOfSpeech(meaning.PartOfSpeech));
                int i = 0;
                foreach (var sense in meaning.Senses)
                {
                    b.AppendLine((++i) + ". " + sense.Definition);
                    if (!string.IsNullOrWhiteSpace(sense.Chinese)) b.AppendLine("   中文释义：" + sense.Chinese);
                    if (!string.IsNullOrWhiteSpace(sense.Example)) b.AppendLine("   例句：" + sense.Example);
                }
                if (meaning.Synonyms.Count > 0) b.AppendLine("同义词：" + string.Join(", ", meaning.Synonyms));
                if (meaning.Antonyms.Count > 0) b.AppendLine("反义词：" + string.Join(", ", meaning.Antonyms));
            }
            b.AppendLine("\n派生词 / 复合词（Wiktionary Derived terms）").AppendLine(Derived ?? "暂无资料");
            if (!string.IsNullOrWhiteSpace(Notice)) b.AppendLine("\n" + Notice);
            b.AppendLine("\n英文释义来源：Free Dictionary API\nhttps://dictionaryapi.dev/");
            b.AppendLine("派生词来源：Wiktionary（CC BY-SA，可能仅显示部分条目）\nhttps://en.wiktionary.org/wiki/" + Uri.EscapeDataString(Word) + "#English");
            if (Sources.Count > 0) b.AppendLine("词条原文 / 许可\n" + string.Join("\n", Sources.Distinct()));
            return b.ToString().Trim();
        }
    }
    public class Meaning
    {
        public string PartOfSpeech { get; set; }
        public List<Sense> Senses { get; set; } = new List<Sense>();
        public List<string> Synonyms { get; set; } = new List<string>();
        public List<string> Antonyms { get; set; } = new List<string>();
    }
    public class Sense { public string Definition { get; set; } public string Example { get; set; } public string Chinese { get; set; } public string ChineseSource { get; set; } public bool ChineseFailed { get; set; } }
    public static class DictionaryService
    {
        static DictionaryService() { ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12; Client.DefaultRequestHeaders.UserAgent.ParseAdd("MyDict/1.1 (personal desktop dictionary)"); }
        private static readonly HttpClient Client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        private static string Text(Dictionary<string, object> d, string key) { object v; return d.TryGetValue(key, out v) ? Convert.ToString(v) : ""; }
        private static IEnumerable<object> Items(Dictionary<string, object> d, string key) { object v; return d.TryGetValue(key, out v) ? v as object[] ?? new object[0] : new object[0]; }
        public static bool IsEnglishTerm(string word) { return Regex.IsMatch(word, @"^[A-Za-z]+(?:[-' ][A-Za-z]+)*$") && word.Length <= 80; }
        private static string Pos(string pos)
        {
            var map = new Dictionary<string, string> { {"noun", "n. 名词"}, {"verb", "v. 动词"}, {"adjective", "adj. 形容词"}, {"adverb", "adv. 副词"}, {"pronoun", "pron. 代词"}, {"preposition", "prep. 介词"}, {"conjunction", "conj. 连词"}, {"interjection", "interj. 感叹词"}, {"exclamation", "感叹词"}, {"determiner", "限定词"} };
            return map.ContainsKey(pos) ? map[pos] : pos;
        }
        public static DictionaryRecord Parse(string word, string json)
        {
            var roots = new JavaScriptSerializer { MaxJsonLength = 4000000 }.Deserialize<object[]>(json);
            if (roots == null || roots.Length == 0) throw new InvalidOperationException("词典返回了空数据。");
            var result = new DictionaryRecord { Word = word };
            var phonetics = new List<string>();
            foreach (Dictionary<string, object> root in roots)
            {
                result.Sources.AddRange(Items(root, "sourceUrls").Select(Convert.ToString));
                object license; if (root.TryGetValue("license", out license) && license is Dictionary<string, object>) result.Sources.Add(Text((Dictionary<string, object>)license, "name") + " " + Text((Dictionary<string, object>)license, "url"));
                var p = Text(root, "phonetic"); if (p.Length > 0) phonetics.Add(p);
                foreach (Dictionary<string, object> item in Items(root, "phonetics")) { p = Text(item, "text"); if (p.Length > 0) phonetics.Add(p); }
                if (string.IsNullOrWhiteSpace(result.Origin)) result.Origin = Text(root, "origin");
                foreach (Dictionary<string, object> m in Items(root, "meanings"))
                {
                    var meaning = new Meaning { PartOfSpeech = Pos(Text(m, "partOfSpeech")) };
                    meaning.Synonyms.AddRange(Items(m, "synonyms").Select(Convert.ToString));
                    meaning.Antonyms.AddRange(Items(m, "antonyms").Select(Convert.ToString));
                    foreach (Dictionary<string, object> d in Items(m, "definitions"))
                    {
                        meaning.Senses.Add(new Sense { Definition = Text(d, "definition"), Example = Text(d, "example") });
                        meaning.Synonyms.AddRange(Items(d, "synonyms").Select(Convert.ToString));
                        meaning.Antonyms.AddRange(Items(d, "antonyms").Select(Convert.ToString));
                    }
                    meaning.Synonyms = meaning.Synonyms.Distinct().Take(15).ToList();
                    meaning.Antonyms = meaning.Antonyms.Distinct().Take(15).ToList();
                    result.Meanings.Add(meaning);
                }
            }
            result.Phonetic = string.Join("  ·  ", phonetics.Distinct());
            return result;
        }
        public static string ParseDerived(string wiki)
        {
            var english = Regex.Match(wiki, @"(?ms)^==English==\s*(.*?)(?=^==[^=]|\z)");
            if (!english.Success) return "来源暂未收录英文派生词。";
            var terms = new List<string>();
            foreach (Match section in Regex.Matches(english.Groups[1].Value, @"(?ms)^={3,6}\s*Derived terms\s*={3,6}\s*\n(.*?)(?=^={2,6}[^=]|\z)"))
            {
                string body = section.Groups[1].Value;
                foreach (Match template in Regex.Matches(body, @"\{\{(?:col\d?(?:-u)?|der\d|der-top|l|m)\|en\|([^{}]+)\}\}"))
                {
                    foreach (var raw in template.Groups[1].Value.Split('|'))
                    {
                        var term = Regex.Replace(raw, @"<[^>]*>", "").Trim();
                        if (Regex.IsMatch(term, @"^[A-Za-z][A-Za-z '\-]*$")) terms.Add(term);
                    }
                }
                foreach (Match link in Regex.Matches(body, @"\[\[([^\]|#]+)(?:#[^\]|]+)?(?:\|[^\]]+)?\]\]"))
                    if (Regex.IsMatch(link.Groups[1].Value, @"^[A-Za-z][A-Za-z '\-]*$")) terms.Add(link.Groups[1].Value);
            }
            return terms.Count == 0 ? "来源暂未提供可解析的派生词；可访问下方原文。" : string.Join(", ", terms.Distinct(StringComparer.OrdinalIgnoreCase).Take(60));
        }
        private class WikiData { public string Derived; public WordForms Forms = new WordForms(); }
        private static async Task<WikiData> DerivedAsync(string word, CancellationToken token)
        {
            try
            {
                string json;
                using (var response = await Client.GetAsync("https://en.wiktionary.org/w/api.php?action=parse&format=json&prop=text%7Cwikitext&redirects=1&page=" + Uri.EscapeDataString(word), token)) { response.EnsureSuccessStatusCode(); json = await response.Content.ReadAsStringAsync(); }
                var root = new JavaScriptSerializer { MaxJsonLength = 4000000 }.Deserialize<Dictionary<string, object>>(json);
                object parse;
                if (!root.TryGetValue("parse", out parse)) return new WikiData { Derived = "来源暂未收录此词。" };
                var data = (Dictionary<string, object>)parse;
                return new WikiData { Derived = ParseDerived(Text((Dictionary<string, object>)data["wikitext"], "*")), Forms = WordForms.Parse(Text((Dictionary<string, object>)data["text"], "*")) };
            }
            catch (Exception ex) when (ex is HttpRequestException || ex is TaskCanceledException || ex is ArgumentException || ex is InvalidOperationException || ex is KeyNotFoundException)
            { return new WikiData { Derived = "词形及派生词服务暂不可用，可访问下方 Wiktionary 原文。" }; }
        }
        public static async Task<DictionaryRecord> LookupAsync(string word, CancellationToken token = default(CancellationToken))
        {
            word = word.Trim().ToLowerInvariant();
            var derived = DerivedAsync(word, token);
            DictionaryRecord result;
            try
            {
                using (var response = await Client.GetAsync("https://api.dictionaryapi.dev/api/v2/entries/en/" + Uri.EscapeDataString(word), token))
                {
                    if (response.StatusCode == HttpStatusCode.NotFound) result = new DictionaryRecord { Word = word, Notice = "词典暂未收录此词；请检查拼写。" };
                    else { response.EnsureSuccessStatusCode(); result = Parse(word, await response.Content.ReadAsStringAsync()); }
                }
            }
            catch (Exception ex) when (ex is HttpRequestException || ex is TaskCanceledException || ex is ArgumentException || ex is InvalidOperationException)
            { result = new DictionaryRecord { Word = word, Notice = "详细词典暂不可用，请稍后重试；基础翻译和本地收藏仍可使用。" }; }
            var wiki = await derived;
            result.Derived = wiki.Derived; result.Forms = wiki.Forms;
            token.ThrowIfCancellationRequested();
            return result;
        }
    }
}






