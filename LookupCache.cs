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
        public Dictionary<string,Entry> Results { get; set; } = new Dictionary<string,Entry>();
        public List<string> History { get; set; } = new List<string>();
        public int Position { get; set; } = -1;
    }
    public class LookupCache
    {
        public static readonly string DefaultPath=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"lookup-cache.json");
        private readonly string path;
        private LookupData data=new LookupData();
        private static JavaScriptSerializer Serializer() { return new JavaScriptSerializer { MaxJsonLength=64*1024*1024, RecursionLimit=128 }; }
        public LookupCache(string file=null) { path=file??DefaultPath; }
        public static string Key(string word,string pair,AppSettings settings)
        {
            return settings.Provider+"|"+(settings.Provider=="LibreTranslate"?settings.LibreUrl:"")+"|"+pair+"|"+word.Trim().Normalize().ToLowerInvariant();
        }
        public void Load()
        {
            if(!File.Exists(path))return;
            var loaded=Serializer().Deserialize<LookupData>(File.ReadAllText(path,Encoding.UTF8));
            if(loaded==null||loaded.Results==null||loaded.History==null)throw new InvalidOperationException("缓存结构无效。");
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
        public void Store(string key,Entry entry) { if(entry!=null)data.Results[key]=Serializer().Deserialize<Entry>(Serializer().Serialize(entry)); }
        public void Record(string key,Entry entry)
        {
            if(entry==null)return;
            // Snapshot results so an in-flight lookup or a favorite cannot mutate a cached record.
            data.Results[key]=Serializer().Deserialize<Entry>(Serializer().Serialize(entry));
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
            File.WriteAllText(path+".tmp",Serializer().Serialize(data),new UTF8Encoding(false));
            if(File.Exists(path))File.Replace(path+".tmp",path,path+".bak");else File.Move(path+".tmp",path);
        }
    }
}


