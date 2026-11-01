using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using MyDict;
class SenseTests
{
 static void Check(bool x,string message){if(!x)throw new Exception(message);}
 static DictionaryRecord Record()
 {
  var r=new DictionaryRecord{Word="bank"};
  var m=new Meaning{PartOfSpeech="n. 名词"};
  m.Senses.Add(new Sense{Definition="A financial institution that keeps and lends money."});
  m.Senses.Add(new Sense{Definition="The land along the edge of a river."});
  m.Senses.Add(new Sense{Definition="The land along the edge of a river."});
  r.Meanings.Add(m);return r;
 }
 static int Main()
 {
  try{
   var r=Record();int calls=0;
   SenseTranslator.TranslateAsync(r,new AppSettings(),CancellationToken.None,null,(text,token)=> {Interlocked.Increment(ref calls);return Task.FromResult(text.StartsWith("A financial")?"存放和借出资金的金融机构。":"河流边缘的陆地；河岸。");}).GetAwaiter().GetResult();
   Check(calls==2,"Duplicate definitions were not deduplicated");
   Check(r.Meanings[0].Senses[0].Chinese.Contains("金融")&&r.Meanings[0].Senses[1].Chinese.Contains("河岸"),"Sense translations mismatched");
   var json=new JavaScriptSerializer().Serialize(r);var loaded=new JavaScriptSerializer().Deserialize<DictionaryRecord>(json);
   Check(loaded.Meanings[0].Senses[1].Chinese.Contains("河岸")&&loaded.Format().Contains("金融"),"Persistence or export lost translations");
   var failed=Record();
   SenseTranslator.TranslateAsync(failed,new AppSettings(),CancellationToken.None,null,(text,token)=>{if(text.StartsWith("A financial"))throw new InvalidOperationException("unavailable");return Task.FromResult("河岸");}).GetAwaiter().GetResult();
   Check(failed.Meanings[0].Senses[0].ChineseFailed&&failed.Meanings[0].Senses[1].Chinese=="河岸","Individual failure not isolated");
   var canceled=new CancellationTokenSource();canceled.Cancel();bool stopped=false;
   try{SenseTranslator.TranslateAsync(Record(),new AppSettings(),canceled.Token).GetAwaiter().GetResult();}catch(OperationCanceledException){stopped=true;}
   Check(stopped,"Cancellation ignored");
   Check(SenseTranslator.Split(new string('字',400)).All(s=>System.Text.Encoding.UTF8.GetByteCount(s)<=480),"Long text exceeds request limits");
   Console.WriteLine("PASS: per-sense mapping, duplicates, persistence/export, partial failure, cancellation, UTF-8 request limit");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
 }
}
