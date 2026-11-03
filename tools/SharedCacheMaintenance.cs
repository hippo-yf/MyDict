using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Collections.Generic;
using MyDict;

// Offline upgrade first, optional explicit --sync publishes the shared snapshot.
class SharedCacheMaintenance
{
 static AppSettings ReadSettings(string path,out Dictionary<string,object> original){
  try{
   var serializer=JsonFiles.Serializer();original=serializer.Deserialize<Dictionary<string,object>>(File.ReadAllText(path,Encoding.UTF8));
   if(original==null)throw new InvalidDataException("设置文件为空，原文件已保留。");
   var options=new Dictionary<string,object>(original);options.Remove("Styles");
   var settings=serializer.Deserialize<AppSettings>(serializer.Serialize(options));
   if(settings==null)throw new InvalidDataException("设置文件为空，原文件已保留。");
   if(string.IsNullOrWhiteSpace(settings.username))throw new InvalidDataException("升级前请在源和目标设置中明确填写相同 username；维护工具不会改写源设置或猜测词库身份。");
   settings.Validate();return settings;
  }catch(ArgumentException){throw new InvalidDataException("设置文件结构或 JSON 无效，原文件已保留。");}
  catch(InvalidOperationException){throw new InvalidDataException("设置文件结构或配置字段无效，原文件已保留。");}
 }
 static string LocalPath(string value,string directory){
  if(string.IsNullOrWhiteSpace(value))return "";
  string expanded=Environment.ExpandEnvironmentVariables(value);
  return Path.GetFullPath(Path.IsPathRooted(expanded)?expanded:Path.Combine(directory,expanded));
 }
 static void CreateSettings(string path,Dictionary<string,object> settings){
  string temporary=path+".upgrade-"+Guid.NewGuid().ToString("N")+".tmp";
  try{
   File.WriteAllText(temporary,SettingsStore.FormatJson(JsonFiles.Serializer().Serialize(settings)),new UTF8Encoding(false));
   // Move without overwrite: a concurrently created destination belongs to its
   // owner and must never be silently replaced by an upgrade command.
   File.Move(temporary,path);
  }finally{if(File.Exists(temporary))File.Delete(temporary);}
 }
 static int Main(string[] args){
  try{
   if(args.Length==0){Console.Error.WriteLine("SharedCacheMaintenance <settings.json> [--data-dir <directory>] [--sync]");return 1;}
   string sourceSettings=Path.GetFullPath(args[0]),sourceDirectory=Path.GetDirectoryName(sourceSettings),destination=sourceDirectory;bool sync=false;
   for(int i=1;i<args.Length;i++)if(args[i]=="--sync")sync=true;else if(args[i]=="--data-dir"&&i+1<args.Length)destination=Path.GetFullPath(args[++i]);else throw new ArgumentException("未知参数。");
   if(!File.Exists(sourceSettings))throw new FileNotFoundException("设置文件不存在。");
   Dictionary<string,object> original;var sourceConfiguration=ReadSettings(sourceSettings,out original);
   var settings=sourceConfiguration;
   // SSH file paths belong to the source machine and must stay local. Normalize
   // relative paths when copying settings to a separate upgraded program folder.
   string targetSettings=Path.Combine(destination,"settings.json");
   if(File.Exists(targetSettings)){
    Dictionary<string,object> destinationOriginal;settings=ReadSettings(targetSettings,out destinationOriginal);
    if(!string.Equals(settings.username,sourceConfiguration.username,StringComparison.Ordinal))throw new InvalidDataException("源和目标 settings.json 的 username 不同；未导入或同步任何词库，也未修改设置。请选择该用户自己的目标目录。");
    // The target client owns its existing repository, credentials and device
    // seed. Normalize only the in-memory SSH paths against that target folder.
    settings.CacheSync.PrivateKeyPath=LocalPath(settings.CacheSync.PrivateKeyPath,destination);settings.CacheSync.KnownHostsPath=LocalPath(settings.CacheSync.KnownHostsPath,destination);
   }else{
    settings.CacheSync.PrivateKeyPath=LocalPath(settings.CacheSync.PrivateKeyPath,sourceDirectory);settings.CacheSync.KnownHostsPath=LocalPath(settings.CacheSync.KnownHostsPath,sourceDirectory);
    if(string.IsNullOrWhiteSpace(settings.CacheSync.DeviceId))settings.CacheSync.DeviceId=Guid.NewGuid().ToString("N");
    Directory.CreateDirectory(destination);
    object rawOptions;var options=original.TryGetValue("CacheSync",out rawOptions)?rawOptions as Dictionary<string,object>:null;
    if(options==null){options=new Dictionary<string,object>();original["CacheSync"]=options;}
    original["username"]=settings.username;options["PrivateKeyPath"]=settings.CacheSync.PrivateKeyPath;options["KnownHostsPath"]=settings.CacheSync.KnownHostsPath;options["DeviceId"]=settings.CacheSync.DeviceId;
    CreateSettings(targetSettings,original);
   }
   string profile=Path.Combine(destination,"profiles",settings.username),sourceProfile=Path.Combine(sourceDirectory,"profiles",settings.username);
   var database=WordDatabase.OpenShared(profile,settings.username);
   if(!string.Equals(profile,sourceProfile,StringComparison.OrdinalIgnoreCase))
    foreach(string file in new[]{"words.sqlite","words-qt.sqlite","words-shared.sqlite"}){string source=Path.Combine(sourceProfile,file);if(File.Exists(source))database.ImportLocalDatabase(source);}
   if(sync){
    string repositoryId;using(var hash=SHA256.Create())repositoryId=BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(settings.CacheSync.Repository))).Replace("-","").Substring(0,16);
    var exchange=new GiteeCacheSync(Path.Combine(destination,"cache-sync",repositoryId),settings.CacheSync);
    exchange.ExchangeDatabaseAsync(database,CacheIdentity.DeviceId(settings.CacheSync.DeviceId),true).GetAwaiter().GetResult();
   }
   var page=database.Page("",false,0,1);
   Console.WriteLine("Shared user="+settings.username+" words="+page.Total+" favorites="+page.Favorites+" format="+WordDatabase.SharedPayloadFormat+" integrity="+database.IntegrityCheck());
   Console.WriteLine(sync?"Gitee shared snapshot synchronized; original databases retained.":"Local shared database ready; originals retained. Use --sync to publish.");return 0;
  }catch(Exception e){Console.Error.WriteLine(e.Message);return 1;}
 }
}
