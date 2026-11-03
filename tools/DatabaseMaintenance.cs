using System;
using System.Diagnostics;
using System.IO;
using MyDict;
class DatabaseMaintenance
{
 static int Main(string[] args)
 {
  try {
   if(Process.GetProcessesByName("MyDict").Length>0)throw new InvalidOperationException("请先关闭拾词，再执行维护迁移。");
   var settings=SettingsStore.Load(Path.GetFullPath(args[0]));string profile=Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[0])),"profiles",settings.username);
   var timer=Stopwatch.StartNew();var db=WordDatabase.OpenShared(profile,settings.username);db.MigrateLegacy(Path.Combine(profile,"lookup-cache.json"),Path.Combine(profile,"favorites.json"));timer.Stop();
   var page=db.Page("",false,0);Console.WriteLine("Migration ms="+timer.ElapsedMilliseconds+" words="+page.Total+" favorites="+page.Favorites+" database="+new FileInfo(db.FilePath).Length);
   long before=db.DecodedEntries;timer.Restart();for(int i=0;i<100;i++)db.Page("",false,i%Math.Max(1,(int)((page.Total+49)/50)));timer.Stop();
   Console.WriteLine("100 metadata pages ms="+timer.ElapsedMilliseconds+" decoded entries="+(db.DecodedEntries-before));
   Console.WriteLine("Integrity="+db.IntegrityCheck()+" managed MB="+(GC.GetTotalMemory(true)/1024/1024));return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
 }
}
