using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using MyDict;
class WordbookImportTests
{
 class Fake:HttpMessageHandler
 {
  public int Calls,Active,MaxActive;public bool FailFirst;
  private readonly object gate=new object();
  protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken token)
  {
   int call=Interlocked.Increment(ref Calls);int active=Interlocked.Increment(ref Active);lock(gate){MaxActive=Math.Max(MaxActive,active);}
   try {
   await Task.Delay(FailFirst?(call==1?5:200):20,token);
   if(FailFirst&&call==1)return new HttpResponseMessage(HttpStatusCode.Unauthorized){Content=new StringContent("unauthorized")};
   string content=new JavaScriptSerializer().Serialize(new{translation="测试",summary="简义",senses=new[]{new{partOfSpeech="noun",definition="A test.",chinese="一个测试。"}},forms=new{Plural="tests"}});
   return new HttpResponseMessage(HttpStatusCode.OK){Content=new StringContent(new JavaScriptSerializer().Serialize(new{choices=new[]{new{finish_reason="stop",message=new{content=content}}}}))};
   }finally{Interlocked.Decrement(ref Active);}
  }
 }
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static int Main()
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-import-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
  try {
   string input=Path.Combine(folder,"words.txt"),settings=Path.Combine(folder,"settings.json");
   File.WriteAllText(input,"1, incumbent  [ipa]  英译中\n旧释义\n2, delayed hypersensitivity    英译中\n3, Incumbent [ipa] 英译中\n"+string.Join("\n",Enumerable.Range(4,25).Select(i=>i+", word"+i+"  英译中")));
   var words=WordbookImport.ReadWords(input);Check(words.Count==27&&words[0]=="incumbent"&&words[1]=="delayed hypersensitivity","Extraction/deduplication failed");
   var fake=new Fake();WordbookImport.CreateService=()=>new DeepSeekService(fake);WordbookImport.EnsureClosed=()=>{};
   SettingsStore.Save(new AppSettings{username="import-test",CacheSync=new CacheSyncOptions{Enabled=false},DeepSeek=new DeepSeekOptions{ApiKey="test"}},settings);
   Func<string,string,string[]> arguments=(force,dry)=>new[]{input,settings,"0","0","0",force,dry};
   Check(WordbookImport.Main(arguments("false","true"))==0&&fake.Calls==0&&!Directory.Exists(Path.Combine(folder,"profiles")),"Dry run wrote files or called API");
   Check(WordbookImport.Main(arguments("false","false"))==0&&fake.Calls==27,"Initial sequential import failed");
    string profile=Path.Combine(folder,"profiles","import-test");var cache=WordDatabase.OpenShared(profile,"import-test");
   Check(cache.Page("",false,0).Total==27&&cache.Page("",false,0).Favorites==27&&cache.History().Current==null,"Cache/favorites/history incorrect");
   Check(cache.PendingSync,"Sync pending marker missing");
   Check(WordbookImport.Main(arguments("false","false"))==0&&fake.Calls==27,"Resume made duplicate API calls");
   // Reuse a recovered response without billing it again, including a torn final journal record.
   var entry=DeepSeekService.Parse("recovered","{\"translation\":\"恢复\"}",new DeepSeekOptions());
   File.WriteAllText(Path.Combine(profile,".import-deepseek.jsonl"),JsonFiles.Serializer().Serialize(entry)+"\n{partial");
   Check(WordbookImport.Main(arguments("false","false"))==0&&fake.Calls==27,"Journal replay failed");
   Check(cache.Find("recovered","en|zh-CN",true)!=null&&cache.IsFavorite("recovered","en|zh-CN"),"Journal result lost");
   var limited=arguments("true","false");limited[2]="1";Check(WordbookImport.Main(limited)==0&&fake.Calls==28,"Force/limit failed");
   var concurrent=new Fake();WordbookImport.CreateService=()=>new DeepSeekService(concurrent);
   var parallel=arguments("true","false").Concat(new[]{"3"}).ToArray();
   Check(WordbookImport.Main(parallel)==0&&concurrent.Calls==27&&concurrent.MaxActive==3&&concurrent.Active==0,"Concurrency limit or parallel queries failed");
   Check(cache.Page("",false,0).Total==28&&cache.Page("",false,0).Favorites==28,"Parallel writes lost cache or favorites");
   var stopFake=new Fake{FailFirst=true};WordbookImport.CreateService=()=>new DeepSeekService(stopFake);parallel[2]="6";
   Check(WordbookImport.Main(parallel)==1&&stopFake.Calls==3&&stopFake.Active==0,"Fatal response did not stop scheduling and drain in-flight requests");
   Check(cache.Page("",false,0).Total==28&&cache.Page("",false,0).Favorites==28,"Fatal shutdown lost saved data");
   Console.WriteLine("PASS: extraction, deduplication, dry-run, sequential API, SQLite atomic writes, favorites, resume, torn journal recovery, force/limit, bounded parallelism, single writer, fatal drain");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{Directory.Delete(folder,true);}
 }
}
