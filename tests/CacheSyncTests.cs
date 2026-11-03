using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using MyDict;
class CacheSyncTests
{
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static string Q(string value){return "\""+value.Replace('\\','/')+"\"";}
 static string Git(string directory,string args){using(var process=Process.Start(new ProcessStartInfo("git",args){WorkingDirectory=directory,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true})){var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();process.WaitForExit();if(process.ExitCode!=0)throw new Exception(error.Result);return output.Result;}}
 static GiteeCacheSync Client(string directory,string bare){Directory.CreateDirectory(directory);Git(directory,"init --initial-branch=main");Git(directory,"remote add origin git@gitee.com:hippo-yf/MyDict-Cache.git");Git(directory,"config "+Q("url."+bare.Replace('\\','/')+".insteadOf")+" git@gitee.com:hippo-yf/MyDict-Cache.git");return new GiteeCacheSync(directory,new CacheSyncOptions());}
 static Entry Entry(string word,string time="2026-11-03T00:00:00Z"){return new Entry{Word=word,Translation="测试 "+word,Pair="en|zh-CN",SuccessfulQueryAtUtc=time};}
 static int Main()
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-sqlite-sync-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
  try {
   string bare=Path.Combine(folder,"remote.git");Directory.CreateDirectory(bare);Git(bare,"init --bare --initial-branch=main");
   string aPath=Path.Combine(folder,"a"),bPath=Path.Combine(folder,"b"),aId=new string('a',32),bId=new string('b',32);
   var a=Client(aPath,bare);var b=Client(bPath,bare);var dbA=new WordDatabase(Path.Combine(folder,"a.sqlite"),"alice");dbA.Open();dbA.Save(Entry("bank"),true);var dbB=new WordDatabase(Path.Combine(folder,"b.sqlite"),"alice");dbB.Open();dbB.Save(Entry("run"),true);
   a.ExchangeDatabaseAsync(dbA,aId,false).GetAwaiter().GetResult();Check(!Directory.GetFiles(aPath,"*.sqlite",SearchOption.AllDirectories).Any(),"Pull-only uploaded a snapshot");
   a.ExchangeDatabaseAsync(dbA,aId,true).GetAwaiter().GetResult();b.ExchangeDatabaseAsync(dbB,bId,true).GetAwaiter().GetResult();Check(dbB.Page("",false,0).Total==2&&dbB.Page("",false,0).Favorites==2,"Two-device merge failed");
   dbA.SetFavorite(dbA.Page("bank",false,0).Rows.Single().Id,false);a.ExchangeDatabaseAsync(dbA,aId,true).GetAwaiter().GetResult();b.ExchangeDatabaseAsync(dbB,bId,false).GetAwaiter().GetResult();Check(!dbB.IsFavorite("bank","en|zh-CN"),"Favorite tombstone lost");
   // Normal sync must ignore JSON even when a legacy file is malformed.
   string ignored=Path.Combine(aPath,"users","alice","ignored.json");File.WriteAllText(ignored,"not valid json");
   dbA.Save(Entry("newer","2026-11-03T01:00:00Z"));a.ExchangeDatabaseAsync(dbA,aId,true).GetAwaiter().GetResult();Check(!dbA.PendingSync,"Sync failed or read legacy JSON");Check(!Git(aPath,"ls-tree -r --name-only origin/main").Contains(".json"),"JSON was uploaded");
   // Unrelated tracked/staged changes are preserved but never included in the sync commit.
   string unrelated=Path.Combine(aPath,"notes.txt");File.WriteAllText(unrelated,"keep");Git(aPath,"add notes.txt");Git(aPath,"-c commit.gpgsign=false commit -m fixture");Git(aPath,"push origin HEAD:main");File.Delete(unrelated);Git(aPath,"add notes.txt");
   dbA.Save(Entry("dirty"));a.ExchangeDatabaseAsync(dbA,aId,true).GetAwaiter().GetResult();Check(!File.Exists(unrelated)&&Git(aPath,"ls-tree -r --name-only origin/main").Contains("notes.txt"),"Unrelated staged deletion uploaded or lost");
   b.ExchangeDatabaseAsync(dbB,bId,false).GetAwaiter().GetResult();Check(dbB.Find("newer","en|zh-CN")!=null&&dbA.IntegrityCheck()=="ok"&&dbB.IntegrityCheck()=="ok","Snapshot integrity/merge failed");
   var other=new WordDatabase(Path.Combine(folder,"other.sqlite"),"bob");other.Open();b.ExchangeDatabaseAsync(other,new string('c',32),false).GetAwaiter().GetResult();Check(other.Page("",false,0).Total==0,"Cross-user data leak");
   Git(aPath,"config "+Q("url."+bare.Replace('\\','/')+".insteadOf")+" unused");Git(aPath,"config "+Q("url."+Path.Combine(folder,"missing.git").Replace('\\','/')+".insteadOf")+" git@gitee.com:hippo-yf/MyDict-Cache.git");
   bool failed=false;try{a.ExchangeDatabaseAsync(dbA,aId,true).GetAwaiter().GetResult();}catch(IOException){failed=true;}Check(failed&&dbA.Find("bank","en|zh-CN")!=null,"Offline sync damaged local data");
   string legacyPath=Path.Combine(bPath,"users","alice","legacy.json");var legacyEntry=Entry("legacy-json");
   JsonFiles.Write(legacyPath,new SyncDocument{username="alice",Lookup=new LookupData{username="alice",Results=new System.Collections.Generic.Dictionary<string,Entry>{{LookupCache.KeyForEntry(legacyEntry),legacyEntry}}},Favorites=new FavoriteData{username="alice"}});
   Git(bPath,"add users/alice/legacy.json");Git(bPath,"-c commit.gpgsign=false commit -m legacy-fixture");Git(bPath,"push origin HEAD:main");File.Delete(legacyPath);
   Check(b.RetireLegacyJsonAsync().GetAwaiter().GetResult()==1,"JSON cleanup did not handle locally deleted snapshot");
   Check(!Git(bPath,"ls-tree -r --name-only origin/main").Contains(".json"),"Cleanup left a tracked JSON snapshot");
   b.ExchangeDatabaseAsync(dbB,bId,false).GetAwaiter().GetResult();Check(dbB.Find("legacy-json","en|zh-CN")==null&&!Git(bPath,"ls-tree -r --name-only origin/main").Contains("legacy-json.sqlite"),"Deletion unexpectedly converted JSON into SQLite");
   Console.WriteLine("PASS: SQLite-only sync, ignored JSON, two-device merge, favorite tombstones, user isolation, dirty working tree, staged-change isolation, offline preservation");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{foreach(var file in Directory.GetFiles(folder,"*",SearchOption.AllDirectories))File.SetAttributes(file,FileAttributes.Normal);Directory.Delete(folder,true);}
 }
}
