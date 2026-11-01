using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace MyDict
{
    public static class SenseTranslator
    {
        private static readonly ConcurrentDictionary<string,string> Cache = new ConcurrentDictionary<string,string>();
        public static IEnumerable<string> Split(string text)
        {
            var chunk=new StringBuilder(); int bytes=0;
            var elements=StringInfo.GetTextElementEnumerator(text??"");
            while(elements.MoveNext())
            {
                string element=elements.GetTextElement(); int count=Encoding.UTF8.GetByteCount(element);
                if(bytes+count>480 && chunk.Length>0) { yield return chunk.ToString(); chunk.Clear(); bytes=0; }
                chunk.Append(element); bytes+=count;
            }
            if(chunk.Length>0) yield return chunk.ToString();
        }
        public static async Task TranslateAsync(DictionaryRecord record, AppSettings settings, CancellationToken token, Action updated = null, Func<string,CancellationToken,Task<string>> translate = null)
        {
            if(record==null) return;
            var groups=record.Meanings.SelectMany(m=>m.Senses).Where(s=>!string.IsNullOrWhiteSpace(s.Definition)&&string.IsNullOrWhiteSpace(s.Chinese)).GroupBy(s=>s.Definition).ToList();
            using(var gate=new SemaphoreSlim(2))
            {
                var tasks=groups.Select(async group=>
                {
                    await gate.WaitAsync(token);
                    try
                    {
                        string key=settings.Provider+"\n"+settings.LibreUrl+"\n"+settings.ApiKey+"\n"+group.Key;
                        string chinese;
                        if(translate!=null || !Cache.TryGetValue(key,out chinese))
                        {
                            var parts=new List<string>();
                            foreach(string part in Split(group.Key))
                            {
                                token.ThrowIfCancellationRequested();
                                string result=translate==null ? await Translator.TranslateConfiguredAsync(part,"en|zh-CN",settings,token) : await translate(part,token);
                                if(string.IsNullOrWhiteSpace(result) || !result.Any(c=>c>=0x3400&&c<=0x9fff)) throw new InvalidOperationException("没有返回中文释义。");
                                parts.Add(Translator.CleanText(result));
                            }
                            chinese=string.Join("",parts);
                            if(translate==null) Cache[key]=chinese;
                        }
                        token.ThrowIfCancellationRequested();
                        foreach(var sense in group) { sense.Chinese=chinese; sense.ChineseSource=settings.Provider; sense.ChineseFailed=false; }
                    }
                    catch(Exception ex) when (!token.IsCancellationRequested && (ex is HttpRequestException || ex is OperationCanceledException || ex is InvalidOperationException || ex is ArgumentException))
                    { foreach(var sense in group) sense.ChineseFailed=true; }
                    finally { gate.Release(); }
                    token.ThrowIfCancellationRequested(); updated?.Invoke();
                }).ToArray();
                await Task.WhenAll(tasks);
            }
        }
    }
}
