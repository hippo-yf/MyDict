using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using MyDict;
class ReleaseUiTests
{
 static BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
 static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
 static Entry Sample(string word){var r=new DictionaryRecord{Word=word};var m=new Meaning{PartOfSpeech="n. 名词"};m.Senses.Add(new Sense{Definition="bank and run.",Chinese="测试释义。"});r.Meanings.Add(m);return new Entry{Word=word,Translation="测试",Pair="en|zh-CN",Record=r};}
 static object Field(object target,string name){return target.GetType().GetField(name,Private).GetValue(target);}
 static void Invoke(object target,string name,params object[] args){target.GetType().GetMethod(name,Private).Invoke(target,args);}
 [STAThread] static int Main()
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-release-"+Guid.NewGuid());Directory.CreateDirectory(folder);
  var app=new App();app.InitializeComponent();app.ShutdownMode=ShutdownMode.OnExplicitShutdown;MainWindow window=null;
  try{
   string config=Path.Combine(folder,"settings.json"),cachePath=Path.Combine(folder,"lookup-cache.json");
   typeof(SettingsStore).GetField("FilePath").SetValue(null,config);typeof(LookupCache).GetField("DefaultPath").SetValue(null,cachePath);SettingsStore.Save(new AppSettings());
   var cache=new LookupCache(cachePath);cache.Record(LookupCache.Key("bank","en|zh-CN",new AppSettings()),Sample("bank"));cache.Record(LookupCache.Key("run","en|zh-CN",new AppSettings()),Sample("run"));cache.Save();
   window=new MainWindow();var store=Field(window,"store");store.GetType().GetField("FilePath").SetValue(store,Path.Combine(folder,"favorites.json"));window.GetType().GetField("entries",Private).SetValue(window,new List<Entry>());Invoke(window,"RefreshList");window.Show();window.UpdateLayout();
   var query=(TextBox)window.FindName("Query");Check(query.Text=="run","Startup history restore failed");
   ((Button)window.FindName("BackButton")).RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Check(query.Text=="bank","Back failed");
   ((Button)window.FindName("ForwardButton")).RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Check(query.Text=="run","Forward failed");
   var view=(RichTextBox)window.FindName("ResultView");var link=view.Document.Blocks.OfType<Paragraph>().SelectMany(p=>p.Inlines.OfType<Hyperlink>()).First(l=>new TextRange(l.ContentStart,l.ContentEnd).Text=="bank");link.RaiseEvent(new RoutedEventArgs(Hyperlink.ClickEvent));Check(query.Text=="bank"&&Field(window,"request")==null,"Cached word click failed");
   var book=(ListBox)window.FindName("FavoritesList");Check(book.Items.Count==2,"Book content missing");
   var star=new Button{DataContext=book.Items.Cast<object>().First(r=>(string)r.GetType().GetProperty("Word").GetValue(r)=="bank")};Invoke(window,"BookStar_Click",star,new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Check(((List<Entry>)Field(window,"entries")).Count==1,"Favorite failed");
   star.DataContext=book.Items.Cast<object>().First(r=>(string)r.GetType().GetProperty("Word").GetValue(r)=="bank");Invoke(window,"BookStar_Click",star,new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Check(((List<Entry>)Field(window,"entries")).Count==0,"Unfavorite failed");
   var s=SettingsStore.Load();Check(s.Styles.Count>=300,"Grouped styles missing");s.Styles["number.doc.size.20"]="22";SettingsStore.Save(s);ThemeStyles.Reload();Check(ThemeStyles.Number("number.doc.size.20")==22,"Style reload failed");
   s.Styles["number.doc.size.20"]="bad";SettingsStore.Save(s);bool rejected=false;try{ThemeStyles.Reload();}catch{rejected=true;}Check(rejected&&ThemeStyles.Number("number.doc.size.20")==22,"Invalid style corrupted theme");
   s.Styles["number.doc.size.20"]="22";SettingsStore.Save(s);window.Close();window=null;var root=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(config));Check(root.ContainsKey("_说明")&&root.ContainsKey("Window")&&SettingsStore.Load().Styles["number.doc.size.20"]=="22","Window/config persistence failed");
   Console.WriteLine("PASS: grouped config, UI startup, cache/history, word-click, favorite toggles, theme reload/error protection, window state");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{if(window!=null)window.Close();app.Shutdown();foreach(string file in Directory.GetFiles(folder))File.Delete(file);Directory.Delete(folder);}
 }
}
