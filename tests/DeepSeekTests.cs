using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MyDict;
class DeepSeekTests
{
 class Fake:HttpMessageHandler
 {
  public volatile int Calls;public bool RanOnUiThread;public int InvalidResponses;public bool Blank;public int Delay=15;public bool Fail;public string RequestText,Auth;
  protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken token)
  {
   RanOnUiThread|=SynchronizationContext.Current is DispatcherSynchronizationContext;Calls++;int version=Calls;Auth=request.Headers.Authorization.ToString();RequestText=await request.Content.ReadAsStringAsync();await Task.Delay(Delay,token);
   if(Fail)return new HttpResponseMessage(HttpStatusCode.Unauthorized){Content=new StringContent("do not expose server error")};
   string result=new JavaScriptSerializer().Serialize(new{word="bank",translation="银行",phonetic="/bæŋk/",meanings=new[]{new{partOfSpeech="noun",definitions=new[]{new{definition="A financial institution.",chinese="金融机构，版本"+version,example="A bank.",usageNote="可数名词",collocations=new[]{new{text="bank account",chinese="银行账户"}},examples=new[]{new{text="She saves money.",chinese="她存钱。",grammarNote="主谓宾",segments=new[]{new{text="She",role="subject",pos="pronoun"},new{text=" saves",role="predicate",pos="verb"},new{text=" money.",role="object",pos="noun"}}}}}}}},forms=new{Plural="banks"},derived=new[]{"banker"},origin=""});
   if(version<=InvalidResponses)result=Blank?"   ":"{\"word\":\"bank\",\"meanings\":[";
   return new HttpResponseMessage(HttpStatusCode.OK){Content=new StringContent(new JavaScriptSerializer().Serialize(new{choices=new[]{new{finish_reason="stop",message=new{content=result}}}}))};
  }
 }
 static BindingFlags Flags=BindingFlags.Instance|BindingFlags.NonPublic;
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static object Field(object instance,string name){return instance.GetType().GetField(name,Flags).GetValue(instance);}
 static void Pump(Func<bool> done,int seconds=5)
 {
  var frame=new DispatcherFrame();var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(20)};var deadline=DateTime.UtcNow.AddSeconds(seconds);
  timer.Tick+=(s,e)=>{if(done()||DateTime.UtcNow>deadline)frame.Continue=false;};timer.Start();Dispatcher.PushFrame(frame);timer.Stop();Check(done(),"UI operation timed out");
 }
 static void Wait(int ms){var until=DateTime.UtcNow.AddMilliseconds(ms);Pump(()=>DateTime.UtcNow>=until);}
 static string Content(MainWindow w){var d=((RichTextBox)w.FindName("ResultView")).Document;return new System.Windows.Documents.TextRange(d.ContentStart,d.ContentEnd).Text;}
 static Entry Normal(string word){return new Entry{Word=word,Pair="en|zh-CN",Translation="普通词典",TranslationProvider="MyMemory",Record=new DictionaryRecord{Word=word}};}
 static void LoadResources(App app)
 {
  var xml=new System.Xml.XmlDocument();xml.Load(Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","..","App.xaml")));
  string resources=xml.SelectSingleNode("//*[local-name()='Application.Resources']").InnerXml;
  app.Resources=(ResourceDictionary)System.Windows.Markup.XamlReader.Parse("<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>"+resources+"</ResourceDictionary>");
 }
 [STAThread]static int Main()
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-deepseek-"+Guid.NewGuid());Directory.CreateDirectory(folder);
  var app=new App();LoadResources(app);app.ShutdownMode=ShutdownMode.OnExplicitShutdown;SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(Dispatcher.CurrentDispatcher));MainWindow w=null;
  try
  {
   var options=new DeepSeekOptions{Enabled=true,ApiKey="test-key"};var settings=new AppSettings{DeepSeek=options,DelayMs=300,username="deepseek-test",CacheSync=new CacheSyncOptions{Enabled=false}};
   string path=Path.Combine(folder,"settings.json");typeof(SettingsStore).GetField("FilePath").SetValue(null,path);typeof(LookupCache).GetField("DefaultPath").SetValue(null,Path.Combine(folder,"lookup-cache.json"));SettingsStore.Save(settings);
   ThemeStyles.EnsureLoaded();
   var verb=DeepSeekService.Parse("go",new JavaScriptSerializer().Serialize(new{translation="去；走",summary="去；前往",meanings=new[]{new{partOfSpeech="verb",definitions=new[]{new{definition="To move.",chinese="移动。"}}}},forms=new{ThirdPerson="goes",PresentParticiple="going",Past="went",PastParticiple="gone"},derived=new[]{"goer n. 常去的人"}}),new DeepSeekOptions());
   Check(verb.ShortTranslation=="去；前往"&&verb.Record.Forms.ForPartOfSpeech("v. 动词").Contains("过去式 went")&&verb.Record.Forms.ForPartOfSpeech("v. 动词").Contains("过去分词 gone")&&!string.IsNullOrWhiteSpace(verb.Record.Derived),"Summary, verb forms or derivatives lost");
   var verbDoc=EntryDocument.Build(verb);var verbText=new System.Windows.Documents.TextRange(verbDoc.ContentStart,verbDoc.ContentEnd).Text;
   Check(verbText.Contains("去；前往")&&verbText.Contains("过去式 went")&&!verbText.Contains("v. 动词"),"Header summary/forms or POS duplication incorrect");

   var c=new LookupCache(Path.Combine(folder,"profiles",settings.username,"lookup-cache.json")){Username=settings.username};var ai=DeepSeekService.Parse("bank","{\"translation\":\"AI旧结果\",\"word\":\"bank\",\"meanings\":[]}",options);
   c.Record(LookupCache.KeyForEntry(ai),ai);c.Record(LookupCache.Key("bank","en|zh-CN",settings),Normal("bank"));c.Save();
   w=new MainWindow();
   var fake=new Fake();w.GetType().GetField("deepSeek",Flags).SetValue(w,new DeepSeekService(fake));w.Show();w.UpdateLayout();Pump(()=>(bool)Field(w,"databaseReady")&&Content(w).Contains("AI旧结果"));
   Check(Content(w).Contains("AI旧结果"),"DeepSeek not preferred on history restore");
   var query=(TextBox)w.FindName("Query");var button=(Button)w.FindName("SearchButton");var toggle=(System.Windows.Controls.Primitives.ToggleButton)w.FindName("DeepSeekCheck");
   query.Text="newword";Wait(600);Check(fake.Calls==0,"Typing triggered DeepSeek API");
   button.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Pump(()=>fake.Calls==1&&Field(w,"request")==null);
   Check(Content(w).Contains("版本1")&&Content(w).Contains("DeepSeek")&&!Content(w).Contains("Free Dictionary API"),"AI content or provenance incorrect");
   Check(!fake.RanOnUiThread,"Online service ran on UI dispatcher");
   Check(!Content(w).Contains("来源："),"Source/time heading still visible");
   Check(fake.Auth=="Bearer test-key"&&fake.RequestText.Contains("json_object")&&fake.RequestText.Contains("deepseek-flash"),"API request contract incorrect");
   button.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Pump(()=>fake.Calls==2&&Field(w,"request")==null);
   Check(Content(w).Contains("版本2"),"Button did not refresh cached result");
   Check(Content(w).Contains("bank account（银行账户）")&&Content(w).Contains("她存钱。")&&!Content(w).Contains("句法  ")&&!Content(w).Contains("DeepSeek AI 生成的词义"),"Rich content missing or notice visible");
   var adjust=w.GetType().GetMethod("AdjustReading",Flags);
   double oldSize=ThemeStyles.Number("number.book.word_size"),oldLeading=ThemeStyles.Number("number.book.word_line_height"),oldDocLeading=ThemeStyles.Number("number.doc.leading_scale");
   adjust.Invoke(w,new object[]{System.Windows.Input.Key.OemPlus,System.Windows.Input.ModifierKeys.Control});
   Check(ThemeStyles.Number("number.book.word_size")>oldSize&&SettingsStore.Load().Styles["number.doc.scale"]!="1","Font shortcut not persisted");
   adjust.Invoke(w,new object[]{System.Windows.Input.Key.OemCloseBrackets,System.Windows.Input.ModifierKeys.Control});
   Check(ThemeStyles.Number("number.book.word_line_height")>oldLeading&&ThemeStyles.Number("number.doc.leading_scale")>oldDocLeading,"Line shortcut failed");
   Check(!SentenceStyles.Valid("She saves money.",new[]{new GrammarSegment{Text="wrong",Role="subject"}}),"Invalid spans accepted");
   var cache=(WordDatabase)Field(w,"database");Check(cache.Find("newword","en|zh-CN",true)?.Entry.Record.Meanings[0].Senses[0].Chinese.Contains("版本2")==true,"Updated result not cached");
   Check(toggle.Foreground.ToString()=="#FF4D6BFE","Enabled icon not blue");
   toggle.IsChecked=false;Wait(550);Check(fake.Calls==2&&Content(w).Contains("版本2"),"Disabled AI cache preference failed");
   Check(toggle.Foreground.ToString()=="#FFFFFFFF","Disabled icon not white");
   toggle.IsChecked=true;Wait(550);Check(fake.Calls==2,"Toggle triggered DeepSeek API");
   int beforeClick=fake.Calls;((RichTextBox)w.FindName("ResultView")).RaiseEvent(new EntryDocument.WordLookupArgs("clicked"));Pump(()=>fake.Calls==beforeClick+1&&Field(w,"request")==null);Check(query.Text=="clicked"&&cache.Find("clicked","en|zh-CN",true)?.Entry!=null,"DeepSeek word click did not query");
   fake.Delay=250;query.Text="slow";button.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Pump(()=>fake.Calls==4);query.Text="next";Wait(650);
   Check(cache.Find("slow","en|zh-CN",true)?.Entry==null&&fake.Calls==4,"Canceled request polluted cache or auto queried");
   string lastSuccess=cache.Find("newword","en|zh-CN",true)?.Entry.SuccessfulQueryAtUtc;Check(!string.IsNullOrEmpty(lastSuccess),"Successful query timestamp missing");
   fake.Delay=15;fake.Fail=true;query.Text="newword";button.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));Pump(()=>Field(w,"request")==null);
   Check(cache.Find("newword","en|zh-CN",true)?.Entry.SuccessfulQueryAtUtc==lastSuccess,"Failed lookup changed successful query timestamp");
   Check(cache.Find("newword","en|zh-CN",true)?.Entry.Record.Meanings[0].Senses[0].Chinese.Contains("版本2")==true,"Failure replaced valid cache");
   Check(!((TextBlock)w.FindName("StateIcon")).ToolTip.ToString().Contains("test-key"),"Secret exposed in error");
   bool rejected=false;try{DeepSeekService.Parse("x"," ",options);}catch(InvalidOperationException){rejected=true;}Check(rejected,"Empty model content accepted");
   Check(!File.Exists(Path.Combine(folder,"profiles",settings.username,"favorites.json")),"Created a second JSON favorite cache");
   var loaded=new WordDatabase(cache.FilePath,settings.username);loaded.Open();Check(loaded.Find("newword","en|zh-CN",true).Entry.LookupModel=="deepseek-flash"&&loaded.Find("newword","en|zh-CN",true).Entry.Record.Meanings[0].Senses[0].Examples[0].Segments.Count==3,"Rich SQLite result not persisted");
   fake.Fail=false;fake.Delay=15;query.Focus();System.Windows.Input.Keyboard.Focus(query);
   int beforeEnter=fake.Calls;
   var enter=new System.Windows.Input.KeyEventArgs(System.Windows.Input.Keyboard.PrimaryDevice,PresentationSource.FromVisual(w),0,System.Windows.Input.Key.Enter){RoutedEvent=System.Windows.Input.Keyboard.PreviewKeyDownEvent};
   query.RaiseEvent(enter);Pump(()=>fake.Calls==beforeEnter+1&&Field(w,"request")==null);
   Check(enter.Handled,"Enter not handled as query");
   Check(Content(w).Contains("She saves money."),"Sentence spans lost spaces");
   var malformed=new Fake{InvalidResponses=1};var recovered=Task.Run(()=>new DeepSeekService(malformed).LookupAsync("bank",options,CancellationToken.None)).GetAwaiter().GetResult();
   Check(malformed.Calls==2&&recovered.Record.Meanings.Count>0&&malformed.RequestText.Contains("FLAT senses"),"Malformed JSON did not trigger bounded correction");
   var blank=new Fake{InvalidResponses=1,Blank=true};Task.Run(()=>new DeepSeekService(blank).LookupAsync("bank",options,CancellationToken.None)).GetAwaiter().GetResult();Check(blank.Calls==2,"Blank JSON not retried");
   var bad=new Fake{InvalidResponses=99};bool failed=false;try{Task.Run(()=>new DeepSeekService(bad).LookupAsync("bank",options,CancellationToken.None)).GetAwaiter().GetResult();}catch(InvalidOperationException){failed=true;}Check(failed&&bad.Calls==2,"Unbounded retry or invalid JSON accepted");
   var flat=DeepSeekService.Parse("bank","\uFEFF```json\n{\"translation\":\"银行\",\"senses\":[{\"partOfSpeech\":\"noun\",\"definition\":\"An institution.\",\"chinese\":\"金融机构。\"}]}\n```",options);Check(flat.Record.Meanings.Count==1,"Flat schema/BOM/fences not parsed");
   Console.WriteLine("PASS: DeepSeek typed-input manual-only, automatic word click, hidden provenance heading, button refresh, preferred source, request schema/auth, cancellation, error/cache preservation, persisted provenance");return 0;
  }
  catch(Exception ex){if(w!=null)Console.Error.WriteLine("UI state: "+((TextBlock)w.FindName("StateIcon")).ToolTip+" ready="+Field(w,"databaseReady"));Console.Error.WriteLine(ex);return 1;}
  finally{if(w!=null)w.Close();app.Shutdown();Directory.Delete(folder,true);}
 }
}











