using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.Serialization;
using System.Security.Cryptography;
using System.Text;
using System.Xml;
using MyDict;

// Exercises the real C# and Qt clients against one local bare Git repository.
// No network requests, personal credentials, or paid dictionary APIs are used.
class CrossClientCacheTests
{
 const string Repository="git@gitee.com:shared-fixture/cache.git";
 const string User="shared-test";
 static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
 static string Quote(string text){return "\""+text.Replace('\\','/')+"\"";}
 static string Run(string program,string arguments,string directory,string pluginPath=null){
  var info=new ProcessStartInfo(program,arguments){WorkingDirectory=directory,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
  if(pluginPath!=null)info.EnvironmentVariables["QT_PLUGIN_PATH"]=pluginPath;
  using(var process=Process.Start(info)){
   var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();
   if(!process.WaitForExit(60000)){process.Kill();throw new Exception("Cross-client fixture timed out.");}
   if(process.ExitCode!=0)throw new Exception(output.Result+error.Result);
   return output.Result+error.Result;
  }
 }
 static string Git(string directory,string arguments){return Run("git",arguments,directory);}
 static void Configure(string directory,string bare){
  Directory.CreateDirectory(directory);Git(directory,"init --initial-branch=main");Git(directory,"remote add origin "+Repository);
  Git(directory,"config core.autocrlf false");Git(directory,"config user.name fixture");Git(directory,"config user.email fixture@example.invalid");
  Git(directory,"config "+Quote("url."+bare.Replace('\\','/')+".insteadOf")+" "+Repository);
 }
 static string Hash(string text){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(text))).Replace("-","").ToLowerInvariant();}
 static byte[] FileHash(string path){using(var hash=SHA256.Create())using(var input=File.OpenRead(path))return hash.ComputeHash(input);}
 static Entry Sample(string word,bool ai=false){return new Entry{Word=word,Pair="en|zh-CN",Summary=ai?"跑；经营":"运行",Translation="中文 <>&' 🐳\nexample",TranslationProvider=ai?"DeepSeek":"MyMemory",LookupSource=ai?"DeepSeek":"Online",LookupModel=ai?"fixture":null,QueryEndpoint=ai?"https://example.invalid/chat/completions":null,SuccessfulQueryAtUtc=ai?"2026-01-02T00:00:00.1234567Z":"2026-01-01T00:00:00.1234567Z",Record=new DictionaryRecord{Word=word,Phonetic="/rʌn/",Derived="runner 跑步者",Forms=new WordForms{Past="ran",ThirdPerson="runs"},Meanings=new List<Meaning>{new Meaning{PartOfSpeech="verb",Senses=new List<Sense>{new Sense{Definition="to move quickly",Chinese="快速移动",Collocations=new List<string>{"run a business（经营企业）"}}}}}}};}
 static void MakeBinaryLegacy(string path){
  var entry=Sample("legacy-binary");var db=new WordDatabase(path,User);db.Open();db.Save(entry,true);
  byte[] payload;using(var output=new MemoryStream()){
   using(var gzip=new GZipStream(output,CompressionMode.Compress,true))using(var writer=XmlDictionaryWriter.CreateBinaryWriter(gzip,null,null,false))new DataContractSerializer(typeof(Entry)).WriteObject(writer,entry);
   payload=output.ToArray();
  }
  using(var raw=new Sqlite(path)){raw.Execute("UPDATE sources SET payload=?",payload);raw.Execute("DELETE FROM metadata WHERE key='payload_format'");}
 }
 static int Main(string[] args){
  string folder=Path.Combine(Path.GetTempPath(),"mydict-cross-client-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
  try{
   string tool=Path.GetFullPath(args[0]),plugins=args.Length>1?args[1]:null;
   string bare=Path.Combine(folder,"remote.git");Directory.CreateDirectory(bare);Git(bare,"init --bare --initial-branch=main");
   string csRepo=Path.Combine(folder,"cs-git");Configure(csRepo,bare);
   string legacy=Path.Combine(csRepo,"users",User,"legacy-device.sqlite");Directory.CreateDirectory(Path.GetDirectoryName(legacy));MakeBinaryLegacy(legacy);
   var originalHash=FileHash(legacy);Git(csRepo,"add users");Git(csRepo,"-c commit.gpgsign=false commit -m legacy-fixture");Git(csRepo,"push origin HEAD:main");
   var cs=WordDatabase.OpenShared(Path.Combine(folder,"cs-profile"),User);cs.Save(Sample("run"),true);cs.Save(Sample("run",true));cs.Save(Sample("remove-me"));
   var sync=new GiteeCacheSync(csRepo,new CacheSyncOptions{Repository=Repository});sync.ExchangeDatabaseAsync(cs,new string('a',32),true).GetAwaiter().GetResult();
   Check(cs.IsFavorite("legacy-binary","en|zh-CN"),"C# did not bridge the original binary snapshot.");
   string qt=Path.Combine(folder,"qt-data");Directory.CreateDirectory(qt);
   JsonFiles.Write(Path.Combine(qt,"settings.json"),new Dictionary<string,object>{{"username",User},{"CacheSync",new Dictionary<string,object>{{"Enabled",false},{"Repository",Repository},{"DeviceId",new string('b',32)}}}});
   string qtRepo=Path.Combine(qt,"cache-sync","qt-"+Hash(Repository).Substring(0,16));Configure(qtRepo,bare);
   Run(tool,"--data-dir "+Quote(qt)+" --sync",folder,plugins);
   var qtDb=new WordDatabase(Path.Combine(qt,"profiles",User,"words-shared.sqlite"),User);
   Check(qtDb.IsFavorite("run","en|zh-CN")&&qtDb.IsFavorite("legacy-binary","en|zh-CN"),"Qt did not read C# favorites/legacy bridge.");
   var translated=qtDb.Find("run","en|zh-CN").Entry;
   Check(translated.Record.Forms.Past=="ran"&&translated.Record.Meanings[0].Senses[0].Chinese=="快速移动"&&translated.Translation.Contains("🐳"),"Structured C# content was lost in Qt.");
   string updates=Path.Combine(folder,"updates.ndjson");var now=DateTimeOffset.UtcNow.AddSeconds(5);
   using(var writer=new StreamWriter(updates,false,new UTF8Encoding(false))){
    writer.WriteLine(JsonFiles.Serializer().Serialize(new{format="mydict-portable-v1",username=User}));
    writer.WriteLine(JsonFiles.Serializer().Serialize(new{type="entry",entry=Sample("qt-word",true),favorite=true,favoriteAt=now.ToString("o")}));
    writer.WriteLine(JsonFiles.Serializer().Serialize(new{type="favorite",word="run",pair="en|zh-CN",favorite=false,updatedAt=now.ToString("o")}));
    writer.WriteLine(JsonFiles.Serializer().Serialize(new{type="deleted",word="remove-me",pair="en|zh-CN",ticks=now.UtcTicks.ToString(System.Globalization.CultureInfo.InvariantCulture)}));
   }
   Run(tool,"--data-dir "+Quote(qt)+" --import-json "+Quote(updates)+" --sync",folder,plugins);
   sync.ExchangeDatabaseAsync(cs,new string('a',32),true).GetAwaiter().GetResult();
   Check(cs.Find("qt-word","en|zh-CN").Entry.Record.Derived=="runner 跑步者"&&cs.IsFavorite("qt-word","en|zh-CN"),"C# did not read a Qt-created entry/favorite.");
   Check(!cs.IsFavorite("run","en|zh-CN")&&cs.Find("remove-me","en|zh-CN")!=null,"Qt favorite update or append-only cache retention failed in C#.");
   Run(tool,"--data-dir "+Quote(qt)+" --sync",folder,plugins);
   Check(qtDb.Find("remove-me","en|zh-CN")!=null&&!qtDb.IsFavorite("run","en|zh-CN"),"Cached word was lost or canceled favorite revived after another exchange.");
   Check(originalHash.SequenceEqual(FileHash(legacy)),"Original binary snapshot was modified.");
   string tree=Git(csRepo,"ls-tree -r --name-only origin/main");Check(tree.Contains("users/"+User+"/shared/cs-")&&tree.Contains("users/"+User+"/shared/qt-")&&!tree.Contains(".json"),"Clients did not share the same SQLite namespace.");
   Check(cs.IntegrityCheck()=="ok"&&qtDb.IntegrityCheck()=="ok","Cross-client database integrity failed.");
   Console.WriteLine("PASS: real C# -> Git -> Qt -> Git -> C# shared cache, binary bridge, rich bilingual records, favorites, ignored legacy deletion markers, preserved originals, SQLite-only snapshots");return 0;
  }catch(Exception e){Console.Error.WriteLine(e);return 1;}
  finally{foreach(var file in Directory.GetFiles(folder,"*",SearchOption.AllDirectories))File.SetAttributes(file,FileAttributes.Normal);Directory.Delete(folder,true);}
 }
}
