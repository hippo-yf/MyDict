using System;
using System.IO;
using System.Linq;
using MyDict;
class WordRemovalTests
{
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static Entry E(string word,bool ai=false,string time="2025-01-01T00:00:00Z"){return new Entry{Word=word,Pair="en|zh-CN",Translation="test",SuccessfulQueryAtUtc=time,LookupSource=ai?"DeepSeek":"MyMemory",TranslationProvider=ai?"DeepSeek":"MyMemory",LookupModel=ai?"model":null,QueryEndpoint=ai?"https://api.deepseek.com/chat/completions":null};}
 static void Sql(string path,string command){var type=typeof(WordDatabase).Assembly.GetType("MyDict.Sqlite");using(var db=(IDisposable)Activator.CreateInstance(type,new object[]{path,false}))type.GetMethod("Execute").Invoke(db,new object[]{command,new object[0]});}
 static int Main(){string folder=Path.Combine(Path.GetTempPath(),"mydict-remove-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);try{
  var db=new WordDatabase(Path.Combine(folder,"words.sqlite"),"test");db.Open();db.Save(E("a"),true,true);db.Save(E("a",true));long id=db.Find("a","en|zh-CN").Id,revision=db.Revision;Check(db.HasDeepSeek("a","en|zh-CN")&&!db.RemoveWord(id)&&db.Revision==revision,"Favorite removal guard failed");
  db.SetFavorite(id,false);db.Save(E("b"),null,true);db.Save(E("a"),null,true);db.Save(E("c"),null,true);db.History(-2);string old=Path.Combine(folder,"old.sqlite");db.BackupTo(old);Sql(old,"DROP TABLE deleted_words");Sql(old,"DROP TABLE favorite_events");
  long decoded=db.DecodedEntries;Check(db.RemoveWord(id)&&db.DecodedEntries==decoded,"Removal loaded detail payload or failed");Check(db.Get(id)==null&&!db.HasDeepSeek("a","en|zh-CN")&&db.Page("a",false,0).Rows.Count==0,"Removed word/sources still visible");Check(db.History().Current.Entry.Word=="b"&&!db.History().CanBack&&db.History().CanForward&&db.History(1).Current.Entry.Word=="c","Removed history did not compact safely");
  db.MergeFrom(old);Check(db.Find("a","en|zh-CN")==null,"Old-schema snapshot resurrected deleted word");string removed=Path.Combine(folder,"removed.sqlite");db.BackupTo(removed);var peer=new WordDatabase(Path.Combine(folder,"peer.sqlite"),"test");peer.Open();peer.MergeFrom(old);peer.MergeFrom(removed);Check(peer.Find("a","en|zh-CN")==null,"Deletion did not propagate");
  db.Save(E("a",true,DateTimeOffset.UtcNow.AddSeconds(1).ToString("o")));Check(db.HasDeepSeek("a","en|zh-CN"),"Fresh query could not restore removed word");
  // Seed a pre-upgrade database with no sequence marker, then remove its largest id.
  long maximum=db.Page("",false,0).Rows.Max(w=>w.Id);Sql(db.FilePath,"DELETE FROM metadata WHERE key='word_sequence'");var reopened=new WordDatabase(db.FilePath,"test");reopened.Open();Check(reopened.RemoveWord(maximum),"Legacy largest-id deletion failed");reopened.Save(E("new-id"));Check(reopened.Find("new-id","en|zh-CN").Id>maximum&&!reopened.RemoveWord(maximum),"Stale UI id targeted a different word");
  // A newer cancel event must beat a subsequently received older re-favorite after deletion.
  var target=new WordDatabase(Path.Combine(folder,"order.sqlite"),"test");target.Open();target.Save(E("x"));long xid=target.Find("x","en|zh-CN").Id;target.RemoveWord(xid);
  string later=DateTimeOffset.UtcNow.AddMinutes(2).ToString("o"),earlier=DateTimeOffset.UtcNow.AddMinutes(1).ToString("o");
  var cancel=new WordDatabase(Path.Combine(folder,"cancel.sqlite"),"test");cancel.Open();cancel.Save(E("x"));Sql(cancel.FilePath,"UPDATE words SET favorite=0,favorite_at='"+later+"'");target.MergeFrom(cancel.FilePath);
  var favorite=new WordDatabase(Path.Combine(folder,"favorite.sqlite"),"test");favorite.Open();favorite.Save(E("x"));Sql(favorite.FilePath,"UPDATE words SET favorite=1,favorite_at='"+earlier+"'");target.MergeFrom(favorite.FilePath);Check(!target.IsFavorite("x","en|zh-CN"),"Older re-favorite overrode newer orphan cancel");
  string order=Path.Combine(folder,"order-snapshot.sqlite");target.BackupTo(order);favorite.MergeFrom(order);Check(!favorite.IsFavorite("x","en|zh-CN"),"Orphan cancel event failed to sync");Check(target.IntegrityCheck()=="ok"&&db.IntegrityCheck()=="ok","Removal corrupted SQLite");
  Console.WriteLine("PASS: favorite guard, multi-source deletion, history compaction, old-schema stale merge, deletion sync, fresh requery, id high-water migration, orphan favorite event ordering");return 0;
 }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}finally{Directory.Delete(folder,true);}}
}
