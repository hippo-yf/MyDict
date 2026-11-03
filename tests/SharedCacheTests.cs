using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Xml;
using MyDict;

class SharedCacheTests
{
 static readonly byte[] PublicPrefix=Encoding.ASCII.GetBytes("MYDICT-JSON1\n");
 static readonly byte[] QtPrefix=Encoding.ASCII.GetBytes("MYDICT-QT-JSON1\n");
 static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
 static Entry Sample(string word,bool ai=false,string time="2026-01-01T00:00:00.1234567Z")
 {
  return new Entry{Word=word,Pair="en|zh-CN",Summary=ai?"跑；经营":"在线摘要",Translation="中文 <>&' \"quoted\" \\ slash 🐳\nsecond\tline\u0001",Details="详细释义 <>&' 🐳",TranslationProvider=ai?"DeepSeek":"MyMemory",LookupSource=ai?"DeepSeek":"Online",LookupModel=ai?"fixture-model":null,QueryEndpoint=ai?"https://example.invalid/chat/completions":null,SuccessfulQueryAtUtc=time,
   Record=new DictionaryRecord{Word=word,Phonetic="/rʌn/",Derived="runner 跑步者",Forms=new WordForms{ThirdPerson="runs",Past="ran",PresentParticiple="running",PastParticiple="run"},Meanings=new List<Meaning>{new Meaning{PartOfSpeech="verb",Senses=new List<Sense>{new Sense{Definition="to move quickly on foot",Chinese="用脚快速移动，跑步",ChineseFailed=false,Collocations=new List<string>{"run a business（经营企业）"},Examples=new List<UsageExample>{new UsageExample{Text="She runs every day.",Chinese="她每天跑步。",Segments=new List<GrammarSegment>{new GrammarSegment{Text="She ",Role="subject",Pos="pronoun"},new GrammarSegment{Text="runs ",Role="predicate",Pos="verb"},new GrammarSegment{Text="every day.",Role="adverbial",Pos="adverb"}}}}}}}}}};
 }
 static byte[] LegacyPayload(Entry entry)
 {
  using(var output=new MemoryStream()){
   using(var gzip=new GZipStream(output,CompressionMode.Compress,true))using(var writer=XmlDictionaryWriter.CreateBinaryWriter(gzip,null,null,false))new DataContractSerializer(typeof(Entry)).WriteObject(writer,entry);
   return output.ToArray();
  }
 }
 static byte[] Payload(string path,string key){using(var raw=new Sqlite(path,true))using(var row=raw.Prepare("SELECT payload FROM sources WHERE source_key=?",key)){Check(row.Read(),"Source missing: "+key);return row.Blob(0);}}
 static void AssertPublic(string path)
 {
  using(var raw=new Sqlite(path,true)){
   Check(raw.Text("SELECT value FROM metadata WHERE key='payload_format'")==WordDatabase.SharedPayloadFormat,"Snapshot marker is not public");
   using(var row=raw.Prepare("SELECT payload FROM sources"))while(row.Read())Check(row.Blob(0).Take(PublicPrefix.Length).SequenceEqual(PublicPrefix),"Non-public payload published");
   Check(raw.Text("PRAGMA integrity_check")=="ok","SQLite integrity check");
  }
 }
 static void RewriteLegacy(string path,bool qt)
 {
  using(var raw=new Sqlite(path)){
   var sources=new List<Tuple<string,byte[]>>();
   using(var rows=raw.Prepare("SELECT source_key,payload FROM sources"))while(rows.Read())sources.Add(Tuple.Create(rows.Text(0),rows.Blob(1)));
   foreach(var source in sources){
    string json=Encoding.UTF8.GetString(source.Item2,PublicPrefix.Length,source.Item2.Length-PublicPrefix.Length);
    byte[] bytes=qt?QtPrefix.Concat(Encoding.UTF8.GetBytes(json)).ToArray():LegacyPayload(JsonFiles.Serializer().Deserialize<Entry>(json));
    raw.Execute("UPDATE sources SET payload=? WHERE source_key=?",bytes,source.Item1);
   }
   if(qt)raw.Execute("UPDATE metadata SET value='qt-json-v1' WHERE key='payload_format'");else raw.Execute("DELETE FROM metadata WHERE key='payload_format'");
   raw.Execute("PRAGMA wal_checkpoint(TRUNCATE)");
  }
 }
 static void VerifyRichEntry(Entry entry)
 {
  Check(entry.Record.Phonetic=="/rʌn/"&&entry.Record.Forms.Past=="ran"&&entry.Record.Derived=="runner 跑步者","IPA/forms/derived lost");
  var sense=entry.Record.Meanings[0].Senses[0];
  Check(sense.Chinese=="用脚快速移动，跑步"&&sense.Collocations[0]=="run a business（经营企业）","Sense/collocation Chinese lost");
  Check(sense.Examples[0].Segments[1].Role=="predicate"&&sense.Examples[0].Chinese=="她每天跑步。","Examples/grammar lost");
 }
 static void FutureFavoriteClock(string folder)
 {
  var local=new WordDatabase(Path.Combine(folder,"clock-local.sqlite"),"shared-test");local.Open();local.Save(Sample("clock"));
  var remote=new WordDatabase(Path.Combine(folder,"clock-remote.sqlite"),"shared-test");remote.Open();remote.Save(Sample("clock"),true);
  var future=DateTimeOffset.UtcNow.AddMinutes(10);
  using(var raw=new Sqlite(remote.FilePath)){
   raw.Execute("UPDATE favorite_events SET updated_at=?",future.ToString("o"));raw.Execute("UPDATE words SET favorite_at=?",future.ToString("o"));
   raw.Execute("UPDATE metadata SET value=CAST(value AS INTEGER)+1 WHERE key='revision'");
  }
  local.MergeFrom(remote.FilePath);Check(local.IsFavorite("clock","en|zh-CN"),"Future remote favorite was not merged");
  long id=local.Page("clock",false,0).Rows.Single().Id;local.SetFavorite(id,false);
  Check(!local.IsFavorite("clock","en|zh-CN"),"Local unfavorite lost to another device's future clock");
  using(var raw=new Sqlite(local.FilePath,true))Check(DateTimeOffset.Parse(raw.Text("SELECT updated_at FROM favorite_events WHERE normalized='clock'")).UtcTicks==future.UtcTicks+1,"Local event is not ordered after the observed future event");
  var later=future.AddMinutes(5);
  using(var raw=new Sqlite(remote.FilePath)){
   raw.Execute("UPDATE favorite_events SET favorite=0,updated_at=?",later.ToString("o"));raw.Execute("UPDATE words SET favorite=0,favorite_at=?",later.ToString("o"));
   raw.Execute("UPDATE metadata SET value=CAST(value AS INTEGER)+1 WHERE key='revision'");
  }
  local.MergeFrom(remote.FilePath);local.SetFavorite(id,true);Check(local.IsFavorite("clock","en|zh-CN"),"Local favorite lost to a future unfavorite");
  // A newer query can retain a word beyond an older deletion. The next local
  // favorite operation must also be newer than that deletion marker.
  var deletion=later.AddMinutes(5);local.Save(Sample("clock",false,deletion.AddMinutes(1).ToString("o")));
  using(var raw=new Sqlite(local.FilePath))raw.Execute("INSERT INTO deleted_words(normalized,pair,deleted_ticks) VALUES(?,?,?)","clock","en|zh-CN",deletion.UtcTicks);
  local.SetFavorite(id,false);Check(!local.IsFavorite("clock","en|zh-CN"),"Local favorite action did not advance beyond the deletion marker");
  using(var raw=new Sqlite(local.FilePath,true))Check(DateTimeOffset.Parse(raw.Text("SELECT updated_at FROM favorite_events WHERE normalized='clock'")).UtcTicks==deletion.UtcTicks+1,"Local event did not use the deletion high-water time");
 }
 static int Main(string[] args)
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-shared-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
  try{
   FutureFavoriteClock(folder);
   string old=Path.Combine(folder,"words.sqlite"),qtPath=Path.Combine(folder,"words-qt.sqlite");
   var legacy=new WordDatabase(old,"shared-test");legacy.Open();
   var ai=Sample("run",true);var online=Sample("run",false,"2026-01-02T00:00:00.0000001Z");
   legacy.Save(ai,true,true);legacy.Save(online,null,true);legacy.Save(Sample("removed"),null,true);Check(legacy.RemoveWord(legacy.Page("removed",false,0).Rows[0].Id),"Fixture deletion");
   legacy.Save(Sample("another"),true,true);legacy.SetFavorite(legacy.Page("another",false,0).Rows[0].Id,false);legacy.History(-1);legacy.RecordImportError("fixture","mock import failure");
   RewriteLegacy(old,false);
   var qt=new WordDatabase(qtPath,"shared-test");qt.Open();var qtEntry=Sample("qt-only",true);qt.Save(qtEntry,true,true);RewriteLegacy(qtPath,true);
   byte[] original=File.ReadAllBytes(old),originalQt=File.ReadAllBytes(qtPath);
   // A read-only legacy merge must not open/upgrade either original file.
   var shared=WordDatabase.OpenShared(folder,"shared-test");
   Check(File.ReadAllBytes(old).SequenceEqual(original)&&File.ReadAllBytes(qtPath).SequenceEqual(originalQt),"OpenShared changed an original database");
   AssertPublic(shared.FilePath);var page=shared.Page("",false,0);Check(page.Total==3&&page.Favorites==2,"Mixed-source legacy bootstrap counts");
   Check(shared.Find("run","en|zh-CN").Entry.TranslationProvider=="MyMemory","Latest complete body priority");VerifyRichEntry(shared.Find("run","en|zh-CN",true).Entry);
   Check(shared.Page("run",false,0).Rows.Single().Summary=="跑；经营","AI summary priority");
   Check(shared.Find("removed","en|zh-CN")==null&&!shared.IsFavorite("another","en|zh-CN"),"Deletion/unfavorite state lost");
   Check(shared.History().Current.Entry.Word=="run"&&shared.History().CanBack&&shared.History().CanForward,"Local history/cursor migration failed");
   using(var raw=new Sqlite(shared.FilePath,true)){Check(raw.Scalar("SELECT COUNT(*) FROM import_errors")==1,"Local errors missing");Check(raw.Scalar("SELECT COUNT(*) FROM deleted_words")==1,"Deletion ledger missing");}
   long decoded=shared.DecodedEntries;shared.Page("",false,0);Check(decoded==shared.DecodedEntries,"Metadata paging decoded payloads");
   shared.History(1);long revision=shared.Revision;shared=WordDatabase.OpenShared(folder,"shared-test");Check(shared.Revision==revision&&shared.History().Current.Entry.Word=="another","Bootstrap is not idempotent or reset navigation");
   using(var raw=new Sqlite(shared.FilePath,true))Check(raw.Scalar("SELECT COUNT(*) FROM import_errors")==1,"Errors duplicated by bootstrap");
   var encoded=Payload(shared.FilePath,LookupCache.KeyForEntry(ai));string canonical=Encoding.UTF8.GetString(encoded,PublicPrefix.Length,encoded.Length-PublicPrefix.Length);
   Check(canonical.StartsWith("{\"DetailNote\":"),"JSON keys are not ordinal sorted");
   Check(canonical.Contains("<>&'")&&canonical.Contains("🐳")&&canonical.Contains("\\nsecond\\tline\\u0001"),"Unicode/minimal-escape canonical JSON differs from Qt");
   Check(!canonical.Contains("\\u003c")&&!canonical.Contains("\\u0027")&&!canonical.Contains("\\ud83d"),"HTML/non-ASCII escaping is not Qt-compatible");
   Check(shared.Find("run","en|zh-CN",true).Entry.Translation==ai.Translation,"Unicode/control character roundtrip");
   string snapshot=Path.Combine(folder,"public-csharp.sqlite");shared.BackupTo(snapshot);AssertPublic(snapshot);
   var another=new WordDatabase(Path.Combine(folder,"another-shared.sqlite"),"shared-test");another.Open();another.MergeFrom(snapshot);VerifyRichEntry(another.Find("qt-only","en|zh-CN",true).Entry);
   long stable=another.Revision;another.MergeFrom(snapshot);Check(another.Revision==stable,"Repeated public snapshot merge changed revision");
   // Explicit old-format readers work, and backups still transcode every source.
   var oldReader=new WordDatabase(old,"shared-test");oldReader.Open();VerifyRichEntry(oldReader.Find("run","en|zh-CN",true).Entry);
   string converted=Path.Combine(folder,"converted-binary.sqlite");oldReader.BackupTo(converted);AssertPublic(converted);
   var qtReader=new WordDatabase(qtPath,"shared-test");qtReader.Open();qtReader.BackupTo(Path.Combine(folder,"converted-qt.sqlite"));AssertPublic(Path.Combine(folder,"converted-qt.sqlite"));
   // An unreadable legacy row must never replace an already good public snapshot.
   string corrupt=Path.Combine(folder,"corrupt-binary.sqlite");File.Copy(old,corrupt);
   using(var raw=new Sqlite(corrupt))raw.Execute("UPDATE sources SET payload=? WHERE source_key=?",new byte[]{1,2,3},LookupCache.KeyForEntry(ai));
   var corruptReader=new WordDatabase(corrupt,"shared-test");corruptReader.Open();byte[] previousSnapshot=File.ReadAllBytes(converted);bool failed=false;
   try{corruptReader.BackupTo(converted);}catch(InvalidDataException){failed=true;}
   Check(failed&&File.ReadAllBytes(converted).SequenceEqual(previousSnapshot),"Failed conversion replaced a good snapshot");
   Check(Directory.GetFiles(folder,"*.shared-*.tmp*").Length==0,"Failed conversion left temporary snapshots");
   // Unknown formats fail before any destination mutation.
   string unknown=Path.Combine(folder,"unknown.sqlite");File.Copy(snapshot,unknown);
   using(var raw=new Sqlite(unknown))raw.Execute("UPDATE metadata SET value='future-unknown-v9' WHERE key='payload_format'");
   long before=another.Revision;bool rejected=false;try{another.MergeFrom(unknown);}catch(InvalidDataException){rejected=true;}Check(rejected&&another.Revision==before,"Unknown marker was merged");
   byte[] badOriginal=File.ReadAllBytes(unknown);rejected=false;try{new WordDatabase(unknown,"shared-test").Open();}catch(InvalidDataException){rejected=true;}Check(rejected&&File.ReadAllBytes(unknown).SequenceEqual(badOriginal),"Unknown format Open changed original");
   if(args.Length>0){Directory.CreateDirectory(args[0]);shared.BackupTo(Path.Combine(args[0],"public-csharp.sqlite"));File.WriteAllText(Path.Combine(args[0],"canonical-payload.json"),canonical,new UTF8Encoding(false));}
   if(args.Length>1){
    // Optional real Qt-produced database, created by MyDictTool in the isolated
    // integration fixture. This checks native QJson serialization, not a mock.
    byte[] qtOriginal=File.ReadAllBytes(args[1]);var nativeQt=new WordDatabase(args[1],"shared-test");
    VerifyRichEntry(nativeQt.Find("run","en|zh-CN",true).Entry);
    var qtNative=nativeQt.Find("qt-native","en|zh-CN",true).Entry;
    Check(qtNative.Translation=="Qt生成的公共中文 😀 <>&'\n第二行"&&qtNative.Record.Forms.Plural=="qt-natives","C# did not read native Qt JSON");
    Check(qtNative.Record.Meanings[0].Senses[0].Chinese=="Qt原生测试词","Native Qt per-sense content lost");
    another.MergeFrom(args[1]);Check(another.IsFavorite("qt-native","en|zh-CN"),"Qt favorite merge failed");
    Check(another.Find("qt-native","en|zh-CN").Entry.Translation==qtNative.Translation,"Qt -> C# merge changed translation");
    Check(File.ReadAllBytes(args[1]).SequenceEqual(qtOriginal),"C# modified the Qt source database");
    Console.WriteLine("PASS: actual Qt -> C# public SQLite read/merge, native QJson Unicode/forms/Chinese and favorite state");
   }
   Console.WriteLine("PASS: legacy binary/Qt JSON/shared JSON, canonical Unicode/control escaping, readonly bootstrap, favorite/delete ledgers, local history/errors, public-only snapshots, metadata paging, idempotence and unknown-format protection");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}finally{Directory.Delete(folder,true);}
 }
}
