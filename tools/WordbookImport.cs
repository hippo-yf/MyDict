using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using MyDict;

// Invoked by Import-Wordbook.ps1; reuses the application's API parser and data stores.
public static class WordbookImport
{
 public static Func<DeepSeekService> CreateService = ()=>new DeepSeekService();
 public static Action EnsureClosed = RequireClosed;
 public static List<string> ReadWords(string path)
 {
  var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);var words=new List<string>();
  foreach(string line in File.ReadLines(path,new UTF8Encoding(false,true))) {
   var match=Regex.Match(line,@"^\s*\d+[,，]\s*(?<word>.*?)\s*(?:\[[^\]\r\n]*\])?\s+(?:英译中|中译英)\s*$");
   if(!match.Success)continue;
   string word=match.Groups["word"].Value.Trim().Normalize();
   if(!string.IsNullOrWhiteSpace(word)&&seen.Add(word))words.Add(word);
  }
  if(words.Count==0)throw new InvalidOperationException("未识别到词头；输入应为 imex 导出的编号单词本。");
  return words;
 }
 private static void RequireClosed()
 {
  var processes=Process.GetProcessesByName("MyDict");try {if(processes.Length>0)throw new InvalidOperationException("请先关闭拾词，并在导入完成前保持关闭；查询进度已保留，可重新执行脚本。");}finally{foreach(var p in processes)p.Dispose();}
 }
 public static int Main(string[] args)
 {
  try{return Run(args).GetAwaiter().GetResult();}catch(Exception ex){Console.Error.WriteLine(ex.Message);return 1;}
 }
 private static async Task<int> Run(string[] args)
 {
  // input, settings, limit, delayMs, retries, force, dryRun, concurrency (optional)
  string input=Path.GetFullPath(args[0]),settingsPath=Path.GetFullPath(args[1]);
  int limit=int.Parse(args[2]),delay=int.Parse(args[3]),retries=int.Parse(args[4]);bool force=bool.Parse(args[5]),dryRun=bool.Parse(args[6]);
  int concurrency=args.Length>7?int.Parse(args[7]):1;
  if(concurrency<1||concurrency>32)throw new ArgumentOutOfRangeException("concurrency","并发数范围为 1–32。");
  var all=ReadWords(input);var words=(limit>0?all.Take(limit):all).ToList();
  Console.WriteLine("识别唯一词条："+all.Count+"；本次处理："+words.Count+"；并发数："+concurrency);
  if(dryRun){Console.WriteLine("预览："+string.Join(" · ",words.Take(12)));Console.WriteLine("仅预览：未调用 API、未修改缓存或收藏。");return 0;}
  EnsureClosed();var settings=SettingsStore.Load(settingsPath);settings.DeepSeek.Validate(true);
  string profile=Path.Combine(Path.GetDirectoryName(settingsPath),"profiles",settings.username);Directory.CreateDirectory(profile);
  using(var importLock=new FileStream(Path.Combine(profile,".import-deepseek.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None))
  {
   var database=WordDatabase.OpenShared(profile,settings.username);
   database.MigrateLegacy(Path.Combine(profile,"lookup-cache.json"),Path.Combine(profile,"favorites.json"));
   // Replay legacy JSONL once, then retain it as a backup. New results commit directly to SQLite.
   string journal=Path.Combine(profile,".import-deepseek.jsonl");
   if(File.Exists(journal)){
    using(var reader=new StreamReader(journal,Encoding.UTF8)){
     string line;while((line=reader.ReadLine())!=null){if(string.IsNullOrWhiteSpace(line))continue;
      try{var recovered=JsonFiles.Serializer().Deserialize<Entry>(line);if(recovered==null||!LookupSources.IsDeepSeek(recovered))throw new InvalidOperationException("无效断点记录。");database.Save(recovered,true);}
      catch(Exception ex) when(ex is ArgumentException||ex is InvalidOperationException){if(reader.EndOfStream){Console.WriteLine("忽略最后一条未完整写入的旧断点。");break;}throw;}
     }
    }
    File.Move(journal,journal+".legacy-"+DateTime.UtcNow.Ticks);
   }
   var service=CreateService();int queried=0,reused=0,failed=0,consecutive=0,completed=0,next=0;
   bool stopped=false;string stopReason=null;
   var watch=Stopwatch.StartNew();Console.WriteLine("目标用户："+settings.username+"；按 Ctrl+C 可中断，下次执行自动续传。");
   // One coordinator commits each word and its favorite flag in a SQLite transaction.
   Action<Entry,string,bool> accept=(entry,word,fromCache)=>{
    if(fromCache)reused++;else queried++;
    database.Save(entry,true);completed++;
    Console.WriteLine("["+completed+"/"+words.Count+"] "+word+" | 新查 "+queried+" / 复用 "+reused+" / 失败 "+failed);
   };
   using(var throttle=new RequestThrottle(delay))
   {
    var running=new List<Task<FetchResult>>();
    while((next<words.Count&&!stopped)||running.Count>0)
    {
     while(!stopped&&next<words.Count&&running.Count<concurrency)
     {
      try{EnsureClosed();}catch(Exception ex){stopped=true;stopReason=ex.Message;break;}
      string word=words[next++];var cached=force?null:database.Find(word,Translator.DetectPair(word),true)?.Entry;
      if(cached!=null){accept(cached,word,true);continue;}
      running.Add(FetchAsync(service,word,settings.DeepSeek,retries,throttle));
     }
     if(running.Count==0)break;
     var finished=await Task.WhenAny(running);running.Remove(finished);var result=await finished;
     if(result.Entry!=null){consecutive=0;accept(result.Entry,result.Word,false);}
     else
     {
      failed++;completed++;consecutive++;
      database.RecordImportError(result.Word,result.Error);
      Console.Error.WriteLine("["+completed+"/"+words.Count+"] 失败 "+result.Word+"："+result.Error);
      if(result.Fatal||consecutive>=5){stopped=true;stopReason="连续失败或账户错误，已暂停；成功词条已保留，修复后重新执行即可。";}
     }
    }
   }
   // On a fatal response, stop queuing but drain in-flight queries before exiting.
   if(stopped)throw new InvalidOperationException(stopReason);
   Console.WriteLine("完成：新查 "+queried+"，复用 "+reused+"，失败 "+failed+"；耗时 "+watch.Elapsed.ToString(@"hh\:mm\:ss"));
   Console.WriteLine("SQLite 缓存和收藏："+database.FilePath);return failed==0?0:2;
  }
 }
 private sealed class FetchResult
 {
  public string Word,Error;public Entry Entry;public bool Fatal;
 }
 private sealed class RequestThrottle:IDisposable
 {
  private readonly SemaphoreSlim gate=new SemaphoreSlim(1,1);
  private readonly Stopwatch clock=Stopwatch.StartNew();private readonly int delay;private long next;
  public RequestThrottle(int delayMs){delay=delayMs;}
  public async Task WaitAsync()
  {
   await gate.WaitAsync();
   try {long wait=next-clock.ElapsedMilliseconds;if(wait>0)await Task.Delay((int)wait);next=clock.ElapsedMilliseconds+delay;}
   finally {gate.Release();}
  }
  public void Dispose(){gate.Dispose();}
 }
 private static async Task<FetchResult> FetchAsync(DeepSeekService service,string word,DeepSeekOptions options,int retries,RequestThrottle throttle)
 {
  for(int attempt=0;;attempt++)
  {
   try {await throttle.WaitAsync();var entry=await service.LookupAsync(word,options,CancellationToken.None);return new FetchResult{Word=word,Entry=entry};}
   catch(Exception ex)
   {
    bool fatal=ex.Message.Contains("HTTP 401")||ex.Message.Contains("HTTP 402")||ex.Message.Contains("HTTP 403");
    bool transient=ex is HttpRequestException||ex is OperationCanceledException||ex.Message.Contains("HTTP 429")||Regex.IsMatch(ex.Message,@"HTTP 5\d\d");
    if(!fatal&&transient&&attempt<retries){await Task.Delay(Math.Min(30000,2000*(attempt+1)));continue;}
    return new FetchResult{Word=word,Error=ex.Message,Fatal=fatal||(!(ex is HttpRequestException)&&!(ex is OperationCanceledException)&&!(ex is InvalidOperationException))};
   }
  }
 }

}
