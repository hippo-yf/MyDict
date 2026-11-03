using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using MyDict;
class SqliteTests
{
 static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
 static Entry Sample(string word,bool ai=false,string time="2026-11-03T00:00:00Z")
 {
  return new Entry{Word=word,Pair="en|zh-CN",Translation=ai?"AI 翻译":"在线翻译",Summary=ai?"AI 摘要":"在线摘要",TranslationProvider=ai?"DeepSeek":"MyMemory",LookupSource=ai?"DeepSeek":"Free Dictionary / MyMemory",LookupModel=ai?"deepseek-flash":null,QueryEndpoint=ai?"https://api.deepseek.com/chat/completions":null,SuccessfulQueryAtUtc=time,Record=new DictionaryRecord{Word=word,Meanings=new List<Meaning>{new Meaning{PartOfSpeech="n. 名词",Senses=new List<Sense>{new Sense{Definition="A test word.",Chinese="测试词。",Collocations=new List<string>{"test case（测试用例）"},Examples=new List<UsageExample>{new UsageExample{Text="A test.",Chinese="一个测试。"}}}}}}}};
 }
 static int Main()
 {
  string folder=Path.Combine(Path.GetTempPath(),"mydict-sqlite-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
  try {
   var db=new WordDatabase(Path.Combine(folder,"words.sqlite"),"test");db.Open();
   for(int i=0;i<120;i++)db.Save(Sample("word"+i),i%2==0);
   Check(db.DecodedEntries==0,"Saving decoded existing vocabulary");var page=db.Page("",false,0);Check(page.Rows.Count==50&&page.Total==120&&page.Favorites==60,"Pagination/counts wrong");Check(db.DecodedEntries==0,"Paging decoded complete entries");Check(db.Page("",false,2).Rows.Count==20&&db.Page("",true,1).Rows.Count==10,"Bounded pages failed");
   db.Save(Sample("word1",true,"2025-01-01T00:00:00Z"));page=db.Page("word1",false,0);var row=page.Rows.Single(w=>w.Word=="word1");Check(row.Summary=="AI 摘要"&&row.HasDeepSeek&&row.HasOnline,"Summary/source aggregation failed");
   var selected=db.Get(row.Id);Check(selected.Entry.TranslationProvider=="MyMemory"&&selected.Entry.Record.Meanings[0].Senses[0].Examples[0].Chinese=="一个测试。","Binary payload failed");Check(db.DecodedEntries==1,"Selection loaded more than one entry");
   db.SetFavorite(row.Id,true);db.Visit(selected.Entry);db.Save(Sample("next"),null,true);Check(db.History().CanBack&&db.History(-1).Current.Entry.Word=="word1","SQLite history failed");db.Save(Sample("branch"),null,true);Check(!db.History().CanForward,"History branch failed");
   string backup=Path.Combine(folder,"shard.sqlite");long revision=db.BackupTo(backup);Check(File.ReadAllBytes(backup).Take(16).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("SQLite format 3\0")),"Not a SQLite database");
   var merged=new WordDatabase(Path.Combine(folder,"other.sqlite"),"test");merged.Open();merged.MergeFrom(backup);Check(merged.Page("",false,0).Total==122,"Snapshot merge lost words");Check(merged.Find("word1","en|zh-CN",true)!=null,"Multi-source merge failed");
   merged.SetFavorite(merged.Page("word1",false,0).Rows.Single(w=>w.Word=="word1").Id,false);merged.BackupTo(Path.Combine(folder,"delete.sqlite"));db.MergeFrom(Path.Combine(folder,"delete.sqlite"));Check(!db.IsFavorite("word1","en|zh-CN"),"Favorite tombstone merge failed");
   Check(db.IntegrityCheck()=="ok"&&merged.IntegrityCheck()=="ok","Integrity check failed");
   string legacy=Path.Combine(folder,"legacy");Directory.CreateDirectory(legacy);string lookup=Path.Combine(legacy,"lookup-cache.json"),favorites=Path.Combine(legacy,"favorites.json");
   var cache=new LookupCache(lookup){Username="test"};cache.Record(LookupCache.KeyForEntry(Sample("legacy")),Sample("legacy"));cache.Record(LookupCache.KeyForEntry(Sample("second")),Sample("second"));cache.Move(-1);cache.Save();new FavoriteStore(favorites,"test").Save(new List<Entry>{Sample("legacy")});
   var migrated=new WordDatabase(Path.Combine(legacy,"words.sqlite"),"test");migrated.Open();migrated.MigrateLegacy(lookup,favorites);Check(migrated.Page("",false,0).Total==2&&migrated.IsFavorite("legacy","en|zh-CN")&&migrated.History().Current.Entry.Word=="legacy","Streaming migration failed");long migratedRevision=migrated.Revision;migrated.MigrateLegacy(lookup,favorites);Check(migrated.Revision==migratedRevision,"Migration not idempotent");Check(File.Exists(lookup)&&File.Exists(favorites),"Legacy backup removed");
   var sync=new SyncDocument{username="test",DeviceId="test",Lookup=cache.Snapshot(),Favorites=new FavoriteStore(favorites,"test").Snapshot()};var store=new FavoriteStore(favorites,"test");store.Load();sync.Favorites=store.Snapshot();string syncPath=Path.Combine(folder,"old-shard.json");JsonFiles.Write(syncPath,sync);merged.MigrateSyncJson(syncPath);Check(merged.Find("legacy","en|zh-CN")!=null,"Legacy remote migration failed");
   string bad=Path.Combine(folder,"bad.json");File.WriteAllText(bad,"{\"Results\":{\"x\":"+JsonFiles.Serializer().Serialize(Sample("bad"))+"},\"username\":\"someone-else\"}");bool rejected=false;try{migrated.MigrateLegacy(bad,null);}catch(InvalidDataException){rejected=true;}Check(rejected&&migrated.Find("bad","en|zh-CN")==null,"Migration transaction rollback failed");
   db.MarkSynced(db.Revision);Check(!db.PendingSync,"Revision sync bookkeeping failed");db.Save(Sample("new"));Check(db.PendingSync,"Dirty revision lost");
   Console.WriteLine("PASS: SQLite native/binary payload, bounded metadata pages, multi-source summary, favorites, history, snapshot/merge, tombstones, streaming/idempotent migration, rollback, integrity, sync revision");return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{Directory.Delete(folder,true);}
 }
}
