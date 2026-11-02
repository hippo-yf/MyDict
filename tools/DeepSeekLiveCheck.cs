using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using MyDict;
class DeepSeekLiveCheck
{
 sealed class Capture:DelegatingHandler
 {
  public int Calls; public string Replay;
  public Capture():base(new HttpClientHandler()){}
  protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken token)
  {
   Calls++;
   if(Calls==1&&Replay!=null)return new HttpResponseMessage(HttpStatusCode.OK){Content=new StringContent(new JavaScriptSerializer().Serialize(new{choices=new[]{new{finish_reason="stop",message=new{content=Replay}}}}))};
   var response=await base.SendAsync(request,token);
   Console.WriteLine("Live HTTP "+(int)response.StatusCode);
   return response;
  }
 }
 static int Main(string[] args)
 {
  try
  {
   var serializer=new JavaScriptSerializer();var root=serializer.Deserialize<System.Collections.Generic.Dictionary<string,object>>(File.ReadAllText(args[0]));
   var options=serializer.Deserialize<DeepSeekOptions>(serializer.Serialize(root["DeepSeek"]));
   var handler=new Capture();if(args.Length>2)handler.Replay=File.ReadAllText(args[2]);
   var entry=new DeepSeekService(handler).LookupAsync(args.Length>1?args[1]:"go",options,CancellationToken.None).GetAwaiter().GetResult();
   if(entry.Record==null||entry.Record.Meanings.Count==0||string.IsNullOrWhiteSpace(entry.ShortTranslation))throw new Exception("Missing dictionary content");
   int translated=0,total=0;foreach(var meaning in entry.Record.Meanings)foreach(var sense in meaning.Senses)foreach(var phrase in sense.Collocations){total++;if(phrase.Contains("（"))translated++;}Console.WriteLine("Bilingual collocations: "+translated+"/"+total);
   Console.WriteLine("PASS live query: "+entry.Word+"; requests="+handler.Calls+"; meanings="+entry.Record.Meanings.Count+"; verbForms="+(!string.IsNullOrWhiteSpace(entry.Record.Forms.Past)));
   return 0;
  }
  catch(Exception ex){Console.Error.WriteLine(ex.GetType().Name+": "+ex.Message);return 1;}
 }
}
