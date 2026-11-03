using System;
using System.IO;
using MyDict;
class CacheTests
{
 static void Check(bool x,string text){if(!x)throw new Exception(text);}
 static Entry Entry(string word){return new Entry{Word=word,Translation="中文",Pair="en|zh-CN",Record=new DictionaryRecord{Word=word}};}
 static int Main()
 {
  string path=Path.Combine(Path.GetTempPath(),"lookup-"+Guid.NewGuid()+".json");
  try{
   var c=new LookupCache(path);var s=new AppSettings();
   string a=LookupCache.Key("Bank","en|zh-CN",s),b=LookupCache.Key("run","en|zh-CN",s),d=LookupCache.Key("go","en|zh-CN",s);
   Check(a==LookupCache.Key(" bank ","en|zh-CN",s),"Normalization failed");
   c.Record(a,Entry("bank"));c.Record(b,Entry("run"));c.Save();
   Check(c.CanBack&&!c.CanForward&&c.Move(-1).Word=="bank","Back failed");
   Check(c.CanForward&&c.Move(1).Word=="run","Forward failed");
   c.Move(-1);c.Record(d,Entry("go"));
   Check(!c.CanForward&&c.Find(b).Word=="run","History branching lost cached result");
   c.Save();var loaded=new LookupCache(path);loaded.Load();
   Check(loaded.Current.Word=="go"&&loaded.Move(-1).Word=="bank","Restart history failed");
   Check(loaded.Find(a).Translation=="中文","Cached result lost");
   s.Provider="LibreTranslate";Check(a!=LookupCache.Key("bank","en|zh-CN",s),"Providers share cache key");
   var fresh=Entry("bank");fresh.SuccessfulQueryAtUtc="2026-11-02T05:00:00Z";fresh.Translation="new";c.Record(a,fresh);
   var stale=Entry("bank");stale.SuccessfulQueryAtUtc="2026-11-02T12:00:00+08:00";stale.Translation="old";c.Record(a,stale);
   Check(c.Find(a).Translation=="new","Older success overwrote a newer result, or timestamps were compared as strings");
   var oldAi=Entry("bank");oldAi.LookupSource="DeepSeek";oldAi.QueriedAtUtc="2026-11-02T03:00:00Z";c.Store(LookupCache.KeyForEntry(oldAi),oldAi);
   Check(c.FindPreferred("bank","en|zh-CN",new AppSettings()).Translation=="new","Older AI result hid newer successful query");
   c.Save();var timestampReload=new LookupCache(path);timestampReload.Load();Check(timestampReload.Find(a).SuccessfulQueryAtUtc==fresh.SuccessfulQueryAtUtc,"Success timestamp not persisted");
   File.WriteAllText(path,"{bad json");bool rejected=false;try{new LookupCache(path).Load();}catch{rejected=true;}
   Check(rejected&&File.ReadAllText(path)=="{bad json","Corrupt cache overwritten");
   Console.WriteLine("PASS: normalization, result cache, back/forward, branch truncation, retained cache, restart cursor, provider isolation, corrupt cache protection");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{foreach(var file in new[]{path,path+".bak",path+".tmp"})if(File.Exists(file))File.Delete(file);}
 }
}
