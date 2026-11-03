using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using MyDict;
class RetireGiteeJson
{
 static int Main(string[] args)
 {
  try{string settingsPath=Path.GetFullPath(args[0]);var settings=SettingsStore.Load(settingsPath);string root=Path.GetDirectoryName(settingsPath),id;
   using(var hash=SHA256.Create())id=BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(settings.CacheSync.Repository))).Replace("-","").Substring(0,16);
   var sync=new GiteeCacheSync(Path.Combine(root,"cache-sync",id),settings.CacheSync);
   int count=sync.RetireLegacyJsonAsync().GetAwaiter().GetResult();Console.WriteLine("Deleted JSON snapshots: "+count+"; SQLite files unchanged.");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex.Message);return 1;}
 }
}
