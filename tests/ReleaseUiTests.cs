using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MyDict;
class ReleaseUiTests
{
 const BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
 static object Field(object target,string name){return target.GetType().GetField(name,Private).GetValue(target);}
 static void Invoke(object target,string name,params object[] args){target.GetType().GetMethod(name,Private).Invoke(target,args);}
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static void Pump(Func<bool> condition,int seconds=10){var frame=new DispatcherFrame();var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(10)};var deadline=DateTime.UtcNow.AddSeconds(seconds);timer.Tick+=(s,e)=>{if(condition()||DateTime.UtcNow>=deadline)frame.Continue=false;};timer.Start();Dispatcher.PushFrame(frame);timer.Stop();Check(condition(),"UI operation timed out");}
 static void Resources(App app){var xml=new System.Xml.XmlDocument();xml.Load(Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","..","App.xaml")));string resource=xml.SelectSingleNode("//*[local-name()='Application.Resources']").InnerXml;app.Resources=(ResourceDictionary)System.Windows.Markup.XamlReader.Parse("<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>"+resource+"</ResourceDictionary>");}
 [STAThread]static int Main()
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-sqlite-ui-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);var app=new App();Resources(app);app.ShutdownMode=ShutdownMode.OnExplicitShutdown;SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());MainWindow window=null;
  try {
   string settings=Path.Combine(folder,"settings.json");typeof(SettingsStore).GetField("FilePath").SetValue(null,settings);SettingsStore.Save(new AppSettings{username="ui-test",CacheSync=new CacheSyncOptions{Enabled=false}});
   var data=new WordDatabase(Path.Combine(folder,"profiles","ui-test","words.sqlite"),"ui-test");data.Open();for(int i=0;i<125;i++)data.Save(new Entry{Word="word"+i,Pair="en|zh-CN",Translation="含义 "+i,TranslationProvider="MyMemory",SuccessfulQueryAtUtc="2026-11-03T00:00:00Z"},i%2==0);
   window=new MainWindow();Check(Field(window,"database")==null,"Constructor loaded database before showing the window");window.Show();window.UpdateLayout();Check(window.Icon!=null,"Icon missing");
   Pump(()=> (bool)Field(window,"databaseReady")&&((ListBox)window.FindName("FavoritesList")).Items.Count==50);
   var database=(WordDatabase)Field(window,"database");Check(database.DecodedEntries==0,"Startup/book page decoded full vocabulary");var book=(ListBox)window.FindName("FavoritesList");
   ((Button)window.FindName("BookNext")).RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Pump(()=>((TextBlock)window.FindName("BookPageLabel")).Text.StartsWith("2 /"));Check(book.Items.Count==50&&database.DecodedEntries==0,"Paging accumulated vocabulary/details");
   ((Button)window.FindName("BookNext")).RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Pump(()=>book.Items.Count==25);Check(database.DecodedEntries==0,"Last page decoded detail records");
   ((TextBox)window.FindName("Filter")).Text="word124";Pump(()=>book.Items.Count==1);var row=(MainWindow.BookRow)book.Items[0];Check(row.Word=="word124","Filter incorrect");
   var fakeStar=new Button{DataContext=row};Invoke(window,"BookStar_Click",fakeStar,new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Pump(()=>book.Items.Count==1&&!((MainWindow.BookRow)book.Items[0]).IsFavorite);
   Check(database.DecodedEntries==0,"Favorite toggle loaded detail records");
   book.SelectedIndex=0;Pump(()=>((TextBox)window.FindName("Query")).Text=="word124");Check(database.DecodedEntries<=2,"Selecting one word loaded many records");
   ((TextBox)window.FindName("Filter")).Text="";Pump(()=>book.Items.Count==50);Check(database.DecodedEntries<=2,"Resetting filter decoded details");
   var pending=new TaskCompletionSource<bool>();window.GetType().GetField("exchangeDatabase",Private).SetValue(window,new Func<WordDatabase,bool,Task>((db,upload)=>pending.Task));Invoke(window,"Sync_Click",null,new RoutedEventArgs());
   ((TextBox)window.FindName("Filter")).Text="word123";Pump(()=>book.Items.Count==1);Check(window.IsEnabled,"Sync disabled the window");pending.SetResult(true);Pump(()=>!(bool)Field(window,"syncBusy"));
   bool closed=false;window.Closed+=(s,e)=>closed=true;window.Close();Pump(()=>closed);window=null;
   Console.WriteLine("PASS: shell before database, 50-row pages, no detail decoding for startup/filter/favorites, on-demand selection, responsive sync, quick close");return 0;
  }catch(Exception ex){if(window!=null)Console.Error.WriteLine("UI state: "+((TextBlock)window.FindName("StateIcon")).ToolTip+"; ready="+Field(window,"databaseReady")+"; count="+((ListBox)window.FindName("FavoritesList")).Items.Count);Console.Error.WriteLine(ex);return 1;}
  finally{if(window!=null)window.Close();app.Shutdown();Directory.Delete(folder,true);}
 }
}
