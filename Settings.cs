using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace MyDict
{
    public class AppSettings
    {
        public string FontName { get; set; } = "Microsoft YaHei UI";
        public double FontSize { get; set; } = 16;
        public string DefinitionFontName { get; set; } = "Georgia";
        public int DelayMs { get; set; } = 650;
        public string Provider { get; set; } = "MyMemory";
        public string LibreUrl { get; set; } = "https://libretranslate.com/translate";
        public string ProtectedApiKey { get; set; }
        [ScriptIgnore] public string ApiKey { get; set; }
        public bool ShowExamples { get; set; } = true;
        public bool ShowDerived { get; set; } = true;
        public WindowLayout Window { get; set; }
        [ScriptIgnore] public System.Collections.Generic.Dictionary<string,string> Styles { get; set; }
        public void Validate()
        {
            if (string.IsNullOrWhiteSpace(FontName)) throw new InvalidOperationException("请选择字体。");
            if (double.IsNaN(FontSize) || double.IsInfinity(FontSize) || FontSize < 12 || FontSize > 28) throw new InvalidOperationException("字号范围为 12–28。");
            if (DelayMs < 300 || DelayMs > 3000) throw new InvalidOperationException("查询延迟范围为 300–3000 毫秒。");
            if (Provider != "MyMemory" && Provider != "LibreTranslate") throw new InvalidOperationException("请选择有效的翻译来源。");
            if (Provider == "LibreTranslate")
            {
                Uri uri;
                if (!Uri.TryCreate(LibreUrl, UriKind.Absolute, out uri) || !(uri.Scheme == "https" || (uri.Scheme == "http" && uri.IsLoopback)) || !string.IsNullOrEmpty(uri.UserInfo)) throw new InvalidOperationException("服务地址需为 HTTPS；本机服务可用 HTTP。请填写完整 /translate 地址。");
            }
        }
    }
    public static class SettingsStore
    {
        public static readonly string FilePath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "settings.json");
        private static readonly string LegacyFilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MyDict", "settings.json");
        public static void EnsureFile(string path, string legacyPath)
        {
            if (File.Exists(path)) return;
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            if (File.Exists(legacyPath)) File.Copy(legacyPath, path, false);
            else Save(new AppSettings(), path);
        }
        public static AppSettings Load(string path = null)
        {
            if (path == null) { EnsureFile(FilePath, LegacyFilePath); path = FilePath; }
            if (!File.Exists(path)) return new AppSettings();
            string json = File.ReadAllText(path, Encoding.UTF8);
            var serializer = new JavaScriptSerializer();
            var raw = serializer.Deserialize<System.Collections.Generic.Dictionary<string, object>>(json);
            if(raw==null)throw new InvalidOperationException("设置文件为空。");
            var options=new System.Collections.Generic.Dictionary<string,object>(raw);options.Remove("Styles");
            var settings = serializer.Deserialize<AppSettings>(serializer.Serialize(options));
            if (settings == null) throw new InvalidOperationException("设置文件为空。");
            settings.Validate();
            object styleData;
            if(raw.TryGetValue("Styles",out styleData))settings.Styles=ConfigLayout.FlattenStyles(styleData);
            object manualKey;
            if (raw.TryGetValue("ApiKey", out manualKey) && !string.IsNullOrWhiteSpace(Convert.ToString(manualKey))) settings.ApiKey = Convert.ToString(manualKey);
            else if (!string.IsNullOrEmpty(settings.ProtectedApiKey)) settings.ApiKey = Encoding.UTF8.GetString(ProtectedData.Unprotect(Convert.FromBase64String(settings.ProtectedApiKey), null, DataProtectionScope.CurrentUser));
            return settings;
        }
        public static string FormatJson(string json)
        {
            var result = new StringBuilder(); int depth = 0; bool quoted = false, escaped = false;
            foreach (char c in json)
            {
                if (quoted)
                {
                    result.Append(c);
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') quoted = false;
                    continue;
                }
                if (c == '"') { quoted = true; result.Append(c); }
                else if (c == '{' || c == '[') { result.Append(c).AppendLine(); depth++; result.Append(' ', depth * 2); }
                else if (c == '}' || c == ']') { result.AppendLine(); depth--; result.Append(' ', depth * 2).Append(c); }
                else if (c == ',') { result.Append(c).AppendLine().Append(' ', depth * 2); }
                else if (c == ':') result.Append(": ");
                else if (!char.IsWhiteSpace(c)) result.Append(c);
            }
            return result.AppendLine().ToString();
        }
        public static void SaveWindow(WindowLayout window, string path = null)
        {
            if (window == null || !window.IsValid()) return;
            path = path ?? FilePath;
            var serializer = new JavaScriptSerializer();
            // Merge only window geometry so external JSON edits and manually supplied keys survive.
            var values = File.Exists(path)
                ? serializer.Deserialize<System.Collections.Generic.Dictionary<string, object>>(File.ReadAllText(path, Encoding.UTF8))
                : serializer.Deserialize<System.Collections.Generic.Dictionary<string, object>>(serializer.Serialize(new AppSettings()));
            if (values == null) throw new InvalidOperationException("设置文件为空，未覆盖原文件。");
            values["Window"] = serializer.DeserializeObject(serializer.Serialize(window));
            ConfigLayout.Decorate(values);
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            File.WriteAllText(path + ".tmp", FormatJson(serializer.Serialize(values)), new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(path + ".tmp", path, path + ".bak"); else File.Move(path + ".tmp", path);
        }
        public static void Save(AppSettings settings, string path = null)
        {
            settings.Validate(); path = path ?? FilePath;
            settings.ProtectedApiKey = string.IsNullOrEmpty(settings.ApiKey) ? null : Convert.ToBase64String(ProtectedData.Protect(Encoding.UTF8.GetBytes(settings.ApiKey), null, DataProtectionScope.CurrentUser));
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            var serializer = new JavaScriptSerializer();
            var values = serializer.Deserialize<System.Collections.Generic.Dictionary<string, object>>(serializer.Serialize(settings));
            if(settings.Styles==null && File.Exists(path))
            {
                var previous=serializer.Deserialize<System.Collections.Generic.Dictionary<string,object>>(File.ReadAllText(path,Encoding.UTF8)); object previousStyles;
                if(previous!=null && previous.TryGetValue("Styles",out previousStyles)) values["Styles"]=previousStyles;
            }
            if(settings.Styles!=null)values["Styles"]=ConfigLayout.GroupStyles(settings.Styles);
            values["ApiKey"] = "";
            ConfigLayout.Decorate(values);
            File.WriteAllText(path + ".tmp", FormatJson(serializer.Serialize(values)), new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(path + ".tmp", path, path + ".bak"); else File.Move(path + ".tmp", path);
        }
    }
}








