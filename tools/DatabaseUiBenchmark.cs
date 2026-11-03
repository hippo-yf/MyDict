using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MyDict;
class DatabaseUiBenchmark
{
 const BindingFlags Hidden=BindingFlags.Instance|BindingFlags.NonPublic;
 static void Pump(Func<bool> done){var frame=new DispatcherFrame();var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(10)};var deadline=DateTime.UtcNow.AddSeconds(20);timer.Tick+=(s,e)=>{if(done()||DateTime.UtcNow>deadline)frame.Continue=false;};timer.Start();Dispatcher.PushFrame(frame);timer.Stop();if(!done())throw new Exception("UI benchmark timed out");}
 [STAThread]static int Main(string[] args)
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-real-ui-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);App app=null;MainWindow window=null;
  try {
   var configuration=SettingsStore.Load(Path.GetFullPath(args[0]));var source=new WordDatabase(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[0])),"profiles",configuration.username,"words.sqlite"),configuration.username);source.Open();
   string profile=Path.Combine(folder,"profiles",configuration.username);Directory.CreateDirectory(profile);source.BackupTo(Path.Combine(profile,"words.sqlite"));
   string settings=Path.Combine(folder,"settings.json");SettingsStore.Save(new AppSettings{username=configuration.username,CacheSync=new CacheSyncOptions{Enabled=false}},settings);typeof(SettingsStore).GetField("FilePath").SetValue(null,settings);
   app=new App();var xml=new System.Xml.XmlDocument();xml.Load(Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","..","App.xaml")));app.Resources=(ResourceDictionary)System.Windows.Markup.XamlReader.Parse("<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>"+xml.SelectSingleNode("//*[local-name()='Application.Resources']").InnerXml+"</ResourceDictionary>");app.ShutdownMode=ShutdownMode.OnExplicitShutdown;SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
   var clock=Stopwatch.StartNew();long paint=-1;window=new MainWindow();window.ContentRendered+=(s,e)=>{if(paint<0)paint=clock.ElapsedMilliseconds;};window.Show();
   Pump(()=>paint>=0&&(bool)window.GetType().GetField("databaseReady",Hidden).GetValue(window)&&((ListBox)window.FindName("FavoritesList")).Items.Count>0);
   var db=(WordDatabase)window.GetType().GetField("database",Hidden).GetValue(window);int count=((ListBox)window.FindName("FavoritesList")).Items.Count;Console.WriteLine("First paint ms="+paint+" first page ready ms="+clock.ElapsedMilliseconds+" rows="+count+" decoded details="+db.DecodedEntries+" managed MB="+(GC.GetTotalMemory(true)/1024/1024));
   if(count>50||db.DecodedEntries>1)throw new Exception("Unbounded initial load");window.Close();window=null;app.Shutdown();app=null;return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{if(window!=null)window.Close();if(app!=null)app.Shutdown();Directory.Delete(folder,true);}
 }
}
