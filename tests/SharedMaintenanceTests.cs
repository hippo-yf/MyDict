using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using MyDict;

class SharedMaintenanceTests
{
 const string SourceRepository="git@gitee.com:shared-fixture/source.git";
 const string TargetRepository="git@gitee.com:shared-fixture/destination.git";
 static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
 static string Quote(string value){return "\""+value.Replace('\\','/')+"\"";}
 sealed class Result { public int Code;public string Output; }
 static Result Run(string program,string args,string directory){
  using(var p=Process.Start(new ProcessStartInfo(program,args){WorkingDirectory=directory,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true})){
   var output=p.StandardOutput.ReadToEndAsync();var error=p.StandardError.ReadToEndAsync();
   if(!p.WaitForExit(20000)){p.Kill();throw new Exception("Maintenance fixture timed out.");}
   return new Result{Code=p.ExitCode,Output=output.Result+error.Result};
  }
 }
 static string Git(string directory,string args){var result=Run("git",args,directory);if(result.Code!=0)throw new Exception(result.Output);return result.Output;}
 static byte[] Hash(string path){using(var hash=SHA256.Create())using(var input=File.OpenRead(path))return hash.ComputeHash(input);}
 static string RepositoryId(string repo){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(repo))).Replace("-","").Substring(0,16);}
 static string Settings(string directory,string user,string repository,string device){
  Directory.CreateDirectory(directory);string path=Path.Combine(directory,"settings.json");
  JsonFiles.Write(path,new Dictionary<string,object>{{"username",user},{"_fixture_note","must remain unchanged"},{"DeepSeek",new Dictionary<string,object>{{"ApiKey","fixture-private-key"}}},{"CacheSync",new Dictionary<string,object>{{"Enabled",false},{"Repository",repository},{"DeviceId",device},{"PrivateKeyPath","keys/local-only"},{"KnownHostsPath","keys/known-hosts"}}}});
  return path;
 }
 static WordDatabase Database(string directory,string user,string file,string word){
  string path=Path.Combine(directory,"profiles",user,file);var db=new WordDatabase(path,user);db.Open();
  db.Save(new Entry{Word=word,Pair="en|zh-CN",Translation="fixture",TranslationProvider="MyMemory",SuccessfulQueryAtUtc="2026-01-01T00:00:00.0000001Z"},true);return db;
 }
 static string ConfigureLocalRemote(string data,string repository,string folder,string suffix){
  string bare=Path.Combine(folder,suffix+".git");Directory.CreateDirectory(bare);Git(bare,"init --bare --initial-branch=main");
  string cache=Path.Combine(data,"cache-sync",RepositoryId(repository));Directory.CreateDirectory(cache);
  Git(cache,"init --initial-branch=main");Git(cache,"remote add origin "+Quote(repository));
  Git(cache,"config "+Quote("url."+bare.Replace('\\','/')+".insteadOf")+" "+repository);return bare;
 }
 static int Main(string[] args){
  string folder=Path.Combine(Path.GetTempPath(),"mydict-maintenance-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
  try{
   Check(args.Length==1,"Pass the compiled SharedCacheMaintenance.exe.");string tool=Path.GetFullPath(args[0]);
   string source=Path.Combine(folder,"source"),sourceSettings=Settings(source,"alice",SourceRepository,"");
   var original=Database(source,"alice","words.sqlite","source-word");byte[] sourceSettingsHash=Hash(sourceSettings),originalHash=Hash(original.FilePath);
   string other=Path.Combine(folder,"other-user"),otherSettings=Settings(other,"bob",TargetRepository,new string('b',32));var otherDb=Database(other,"bob","words-shared.sqlite","bob-word");
   byte[] otherSettingsHash=Hash(otherSettings),otherHash=Hash(otherDb.FilePath);
   var rejected=Run(tool,Quote(sourceSettings)+" --data-dir "+Quote(other),folder);
   Check(rejected.Code==1&&rejected.Output.Contains("username"),"Different destination username was not rejected");
   Check(!Directory.Exists(Path.Combine(other,"profiles","alice")),"Guard created an invisible source-user profile");
   Check(Hash(sourceSettings).SequenceEqual(sourceSettingsHash)&&Hash(otherSettings).SequenceEqual(otherSettingsHash)&&Hash(otherDb.FilePath).SequenceEqual(otherHash),"Mismatch guard changed settings or another user's database");
   // Same username still honors the target repository/device/SSH configuration.
   // Both URLs are redirected to local fixtures, so even a regression cannot use network.
   string matching=Path.Combine(folder,"matching-user"),matchingSettings=Settings(matching,"alice",TargetRepository,new string('c',32));
   Database(matching,"alice","words-shared.sqlite","target-word");byte[] matchingHash=Hash(matchingSettings);
   string targetBare=ConfigureLocalRemote(matching,TargetRepository,folder,"target"),wrongBare=ConfigureLocalRemote(matching,SourceRepository,folder,"wrong-source");
   var synchronized=Run(tool,Quote(sourceSettings)+" --data-dir "+Quote(matching)+" --sync",folder);Check(synchronized.Code==0,synchronized.Output);
   string tree=Git(targetBare,"ls-tree -r --name-only main");Check(tree.Contains("users/alice/shared/cs-"),"Existing target repository was ignored");
   Check(Run("git","rev-parse --verify HEAD",wrongBare).Code!=0,"Source repository received the destination's snapshot");
   var matchingDb=new WordDatabase(Path.Combine(matching,"profiles","alice","words-shared.sqlite"),"alice");
   Check(matchingDb.Find("source-word","en|zh-CN")!=null&&matchingDb.Find("target-word","en|zh-CN")!=null,"Target content was overwritten instead of merged");
   Check(Hash(matchingSettings).SequenceEqual(matchingHash),"Existing target settings were overwritten");
   // New folders get a stable generated seed, while the source's empty seed stays empty.
   string fresh=Path.Combine(folder,"fresh");var created=Run(tool,Quote(sourceSettings)+" --data-dir "+Quote(fresh),folder);Check(created.Code==0,created.Output);
   var copied=JsonFiles.Read<Dictionary<string,object>>(Path.Combine(fresh,"settings.json"));var copiedOptions=(Dictionary<string,object>)copied["CacheSync"];
   Check(!string.IsNullOrWhiteSpace(Convert.ToString(copiedOptions["DeviceId"])),"New destination device seed was not persisted");
   Check(Path.IsPathRooted(Convert.ToString(copiedOptions["PrivateKeyPath"]))&&Convert.ToString(copiedOptions["PrivateKeyPath"]).StartsWith(source,StringComparison.OrdinalIgnoreCase),"Copied relative SSH path lost its source directory");
   Check(Hash(sourceSettings).SequenceEqual(sourceSettingsHash)&&Hash(original.FilePath).SequenceEqual(originalHash),"Upgrade modified original source settings/database");
   string blank=Path.Combine(folder,"blank");var blankSettings=Settings(blank,"",SourceRepository,"");byte[] blankHash=Hash(blankSettings);
   var blankResult=Run(tool,Quote(blankSettings)+" --data-dir "+Quote(Path.Combine(folder,"blank-target")),folder);
   Check(blankResult.Code==1&&Hash(blankSettings).SequenceEqual(blankHash),"Blank source identity was guessed or rewritten");
   string invalid=Path.Combine(folder,"invalid.json");File.WriteAllText(invalid,"{\"username\":\"alice\",\"DeepSeek\":{\"ApiKey\":\"fixture-do-not-print\"",new UTF8Encoding(false));
   var bad=Run(tool,Quote(invalid),folder);Check(bad.Code==1&&!bad.Output.Contains("fixture-do-not-print"),"Malformed JSON exception exposed source content");
   Console.WriteLine("PASS: maintenance identity guard, unchanged source/target settings, target repository selection, merge preservation, stable copied device seed, relative SSH paths and private error messages");return 0;
  }catch(Exception e){Console.Error.WriteLine(e);return 1;}
  finally{foreach(string file in Directory.GetFiles(folder,"*",SearchOption.AllDirectories))File.SetAttributes(file,FileAttributes.Normal);Directory.Delete(folder,true);}
 }
}
