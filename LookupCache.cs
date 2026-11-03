using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;

namespace MyDict
{
    public class LookupData
    {
        public string username { get; set; }
        public Dictionary<string,Entry> Results { get; set; } = new Dictionary<string,Entry>();
        public List<string> History { get; set; } = new List<string>();
        public int Position { get; set; } = -1;
    }
    public class LookupCache
    {
        public static readonly string DefaultPath=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"lookup-cache.json");
        private readonly string path;
        private LookupData data=new LookupData();
        public string Username { get { return data.username; } set { data.username = value; } }
        public LookupData Snapshot() { return Serializer().Deserialize<LookupData>(Serializer().Serialize(data)); }
        public void Import(LookupData snapshot)
        {
            if (snapshot == null || snapshot.username != Username) throw new InvalidOperationException("导入缓存 username 不匹配。");
            data = Serializer().Deserialize<LookupData>(Serializer().Serialize(snapshot));
        }
        public void Merge(LookupData other)
        {
            if (other == null || other.username != Username || other.Results == null) return;
            foreach (var item in other.Results) {
                if (item.Value == null || string.IsNullOrWhiteSpace(item.Value.Word)) continue;
                Entry old; if (!data.Results.TryGetValue(item.Key, out old) || CacheMerge.Compare(item.Value, old) > 0) Store(item.Key, item.Value);
            }
        }
        private static JavaScriptSerializer Serializer() { return JsonFiles.Serializer(); }
        public LookupCache(string file=null) { path=file??DefaultPath; }
        public static string Key(string word,string pair,AppSettings settings)
        {
            return settings.Provider+"|"+(settings.Provider=="LibreTranslate"?settings.LibreUrl:"")+"|"+pair+"|"+word.Trim().Normalize().ToLowerInvariant();
        }
        public static string KeyForEntry(Entry entry)
        {
            string pair=entry.Pair??Translator.DetectPair(entry.Word);
            if(LookupSources.IsDeepSeek(entry))return "DeepSeek|"+entry.QueryEndpoint+"|"+entry.LookupModel+"|"+pair+"|"+entry.Word.Trim().Normalize().ToLowerInvariant();
            return Key(entry.Word,pair,new AppSettings{Provider=entry.TranslationProvider??"MyMemory",LibreUrl=entry.TranslationUrl});
        }
        public Entry FindDeepSeek(string word,string pair)
        {
            return data.Results.Values.Where(e=>LookupSources.IsDeepSeek(e)&&string.Equals(e.Word.Trim(),word.Trim(),StringComparison.OrdinalIgnoreCase)&&e.Pair==pair).OrderByDescending(CacheMerge.QueryTime).FirstOrDefault();
        }
        public Entry FindPreferred(string word,string pair,AppSettings settings) {return data.Results.Values.Where(e=>string.Equals(e.Word.Trim().Normalize(),word.Trim().Normalize(),StringComparison.OrdinalIgnoreCase)&&e.Pair==pair).OrderByDescending(CacheMerge.QueryTime).ThenByDescending(LookupSources.IsDeepSeek).FirstOrDefault();}
        public void Load()
        {
            if(!File.Exists(path))return;
            var loaded=Serializer().Deserialize<LookupData>(File.ReadAllText(path,Encoding.UTF8));
            if(loaded==null||loaded.Results==null||loaded.History==null)throw new InvalidOperationException("缓存结构无效。");
            if (!string.IsNullOrEmpty(Username) && !string.IsNullOrEmpty(loaded.username) && loaded.username != Username) throw new InvalidOperationException("缓存 username 不匹配，已保护原文件。");
            loaded.username = Username ?? loaded.username;
            loaded.Results=loaded.Results.Where(p=>p.Value!=null&&!string.IsNullOrWhiteSpace(p.Value.Word)).ToDictionary(p=>p.Key,p=>p.Value);
            loaded.History=loaded.History.Where(k=>k!=null&&loaded.Results.ContainsKey(k)).ToList();
            loaded.Position=loaded.History.Count==0?-1:Math.Max(0,Math.Min(loaded.Position,loaded.History.Count-1));
            data=loaded;
        }
        public IEnumerable<Entry> AllEntries { get { return data.Results.Values; } }
        public Entry Find(string key) { Entry entry;return data.Results.TryGetValue(key,out entry)?entry:null; }
        public bool CanBack {get{return data.Position>0;}}
        public bool CanForward {get{return data.Position>=0&&data.Position<data.History.Count-1;}}
        public Entry Current {get{return data.Position>=0&&data.Position<data.History.Count?Find(data.History[data.Position]):null;}}
        public void Store(string key,Entry entry) { Entry old; if(entry!=null&&(!data.Results.TryGetValue(key,out old)||CacheMerge.Compare(entry,old)>=0))data.Results[key]=Serializer().Deserialize<Entry>(Serializer().Serialize(entry)); }
        public void Record(string key,Entry entry)
        {
            if(entry==null)return;
            // Snapshot results so an in-flight lookup or a favorite cannot mutate a cached record.
            Store(key,entry);
            if(data.Position>=0&&data.History[data.Position]==key)return;
            if(data.Position+1<data.History.Count)data.History.RemoveRange(data.Position+1,data.History.Count-data.Position-1);
            data.History.Add(key);data.Position=data.History.Count-1;
        }
        public Entry Move(int delta)
        {
            int next=data.Position+delta;
            if(next<0||next>=data.History.Count)return null;
            data.Position=next;return Current;
        }
        public void Save()
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            File.WriteAllText(path+".tmp",SettingsStore.FormatJson(Serializer().Serialize(data)),new UTF8Encoding(false));
            if(File.Exists(path))File.Replace(path+".tmp",path,path+".bak");else File.Move(path+".tmp",path);
        }
    }
}



