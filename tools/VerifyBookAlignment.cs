using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MyDict;
using System.Windows.Media;
using System.Windows.Media.Imaging;
class VerifyBookAlignment
{
 const BindingFlags Hidden=BindingFlags.Instance|BindingFlags.NonPublic;
 static T Find<T>(DependencyObject root,string name=null) where T:FrameworkElement {if(root is T&&(name==null||((T)root).Name==name))return (T)root;for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++){var result=Find<T>(VisualTreeHelper.GetChild(root,i),name);if(result!=null)return result;}return null;}
 static void Pump(Func<bool> done){var frame=new DispatcherFrame();var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(10)};var deadline=DateTime.UtcNow.AddSeconds(20);timer.Tick+=(s,e)=>{if(done()||DateTime.UtcNow>deadline)frame.Continue=false;};timer.Start();Dispatcher.PushFrame(frame);timer.Stop();if(!done())throw new Exception("UI benchmark timed out");}
 [STAThread]static int Main(string[] args)
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-real-ui-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);App app=null;MainWindow window=null;
  try {
   var configuration=SettingsStore.Load(Path.GetFullPath(args[0]));var source=new WordDatabase(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[0])),"profiles",configuration.username,"words.sqlite"),configuration.username);source.Open();
   string profile=Path.Combine(folder,"profiles",configuration.username);Directory.CreateDirectory(profile);source.BackupTo(Path.Combine(profile,"words.sqlite"));
   string settings=Path.Combine(folder,"settings.json");configuration.CacheSync.Enabled=false;SettingsStore.Save(configuration,settings);typeof(SettingsStore).GetField("FilePath").SetValue(null,settings);
   app=new App();var xml=new System.Xml.XmlDocument();xml.Load(Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","..","App.xaml")));app.Resources=(ResourceDictionary)System.Windows.Markup.XamlReader.Parse("<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>"+xml.SelectSingleNode("//*[local-name()='Application.Resources']").InnerXml+"</ResourceDictionary>");app.ShutdownMode=ShutdownMode.OnExplicitShutdown;SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
   var clock=Stopwatch.StartNew();long paint=-1;window=new MainWindow();window.ContentRendered+=(s,e)=>{if(paint<0)paint=clock.ElapsedMilliseconds;};window.Show();
   Pump(()=>paint>=0&&(bool)window.GetType().GetField("databaseReady",Hidden).GetValue(window)&&((ListBox)window.FindName("FavoritesList")).Items.Count>0);
   Pump(()=>((System.Threading.Tasks.Task)window.GetType().GetField("storageTask",Hidden).GetValue(window)).IsCompleted); ((TabControl)window.FindName("MainTabs")).SelectedIndex=1;window.UpdateLayout();var book=(ListBox)window.FindName("FavoritesList");Pump(()=>book.ItemContainerGenerator.ContainerFromIndex(0)!=null);window.UpdateLayout();
   for(int i=0;i<Math.Min(6,book.Items.Count);i++){
    var row=(ListBoxItem)book.ItemContainerGenerator.ContainerFromIndex(i);if(row==null)continue;
    var sources=Find<ItemsControl>(row,"SourceIcons");var star=Find<Button>(row,"BookStar");var sourceGlyph=Find<Viewbox>(sources);var starGlyph=Find<Viewbox>(star);
    var sourceRect=sourceGlyph.TransformToAncestor(row).TransformBounds(new Rect(0,0,sourceGlyph.ActualWidth,sourceGlyph.ActualHeight));var starRect=starGlyph.TransformToAncestor(row).TransformBounds(new Rect(0,0,starGlyph.ActualWidth,starGlyph.ActualHeight));
    double difference=Math.Abs(sourceRect.Y+sourceRect.Height/2-starRect.Y-starRect.Height/2);Console.WriteLine("Row "+i+" height="+row.ActualHeight+" source center="+(sourceRect.Y+sourceRect.Height/2)+" star center="+(starRect.Y+starRect.Height/2)+" delta="+difference);
    if(difference>0.5)throw new Exception("Glyphs are not vertically aligned");
   }
   var bitmap=new RenderTargetBitmap((int)window.ActualWidth,(int)window.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(window);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));using(var output=File.Create(args[1]))encoder.Save(output);
   window.Close();window=null;app.Shutdown();app=null;return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{if(window!=null)window.Close();if(app!=null)app.Shutdown();Directory.Delete(folder,true);}
 }
}
