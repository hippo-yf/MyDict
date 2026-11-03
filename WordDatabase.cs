using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Xml;
using System.Threading;
namespace MyDict
{
 public class WordRow
 {
  public long Id {get;set;} public string Word {get;set;} public string Pair {get;set;} public string Summary {get;set;}
  public bool IsFavorite {get;set;} public bool HasDeepSeek {get;set;} public bool HasOnline {get;set;}
 }
 public class WordPage
 {
  public List<WordRow> Rows {get;set;}=new List<WordRow>(); public long Total {get;set;} public long Favorites {get;set;} public long Matches {get;set;}
 }
 public class WordSelection {public Entry Entry {get;set;} public bool IsFavorite {get;set;} public long Id {get;set;}}
 public class DatabaseHistory {public bool CanBack {get;set;} public bool CanForward {get;set;} public WordSelection Current {get;set;}}
 public sealed class WordDatabase
 {
  public string FilePath {get;private set;} public string Username {get;private set;}
  readonly object gate=new object();
  private long decodedEntries;
  public long DecodedEntries {get{return Interlocked.Read(ref decodedEntries);}}
  static string Normal(string word){return (word??"").Trim().Normalize().ToLowerInvariant();}
  public WordDatabase(string path,string username){FilePath=Path.GetFullPath(path);Username=username;}
  T Read<T>(Func<Sqlite,T> action){using(var db=new Sqlite(FilePath))return action(db);}
  T Write<T>(Func<Sqlite,T> action){lock(gate)return Read(db=>{db.Execute("BEGIN IMMEDIATE");try{var result=action(db);db.Execute("COMMIT");return result;}catch{db.Execute("ROLLBACK");throw;}});}
  static string Meta(Sqlite db,string key){return db.Text("SELECT value FROM metadata WHERE key=?",key);}
  static void Meta(Sqlite db,string key,string value){db.Execute("INSERT OR REPLACE INTO metadata(key,value) VALUES(?,?)",key,value);}
  static void Dirty(Sqlite db){db.Execute("UPDATE metadata SET value=CAST(value AS INTEGER)+1 WHERE key='revision'");}
  public void Open()
  {
   Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
   Read(db=>{
    db.Execute("PRAGMA journal_mode=WAL");db.Execute("PRAGMA synchronous=NORMAL");
    db.Execute("CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
    string user=Meta(db,"username");if(user!=null&&user!=Username)throw new InvalidDataException("数据库 username 不匹配。");
    string version=Meta(db,"schema");if(version!=null&&version!="1")throw new InvalidDataException("数据库版本不受支持。");
    db.Execute("CREATE TABLE IF NOT EXISTS words(id INTEGER PRIMARY KEY,normalized TEXT NOT NULL,pair TEXT NOT NULL,word TEXT NOT NULL,favorite INTEGER NOT NULL DEFAULT 0,favorite_at TEXT NOT NULL DEFAULT '',last_ticks INTEGER NOT NULL DEFAULT 0,summary TEXT NOT NULL DEFAULT '',UNIQUE(normalized,pair))");
    db.Execute("CREATE TABLE IF NOT EXISTS sources(source_key TEXT PRIMARY KEY,word_id INTEGER NOT NULL REFERENCES words(id),provider TEXT NOT NULL,is_ai INTEGER NOT NULL,summary TEXT NOT NULL,ticks INTEGER NOT NULL,payload BLOB NOT NULL)");
    db.Execute("CREATE INDEX IF NOT EXISTS source_word ON sources(word_id,ticks DESC)");
    db.Execute("CREATE INDEX IF NOT EXISTS word_recent ON words(last_ticks DESC,id DESC)");
    db.Execute("CREATE INDEX IF NOT EXISTS word_favorite ON words(favorite,last_ticks DESC,id DESC)");
    db.Execute("CREATE TABLE IF NOT EXISTS import_errors(id INTEGER PRIMARY KEY,word TEXT NOT NULL,error TEXT NOT NULL,at TEXT NOT NULL)");
    db.Execute("CREATE TABLE IF NOT EXISTS history(position INTEGER PRIMARY KEY,source_key TEXT NOT NULL)");
    Meta(db,"username",Username);Meta(db,"schema","1");
    db.Execute("INSERT OR IGNORE INTO metadata(key,value) VALUES('revision','0'),('synced','0'),('cursor','-1')");return 0;
   });
  }
  static byte[] Encode(Entry entry)
  {
   using(var stream=new MemoryStream()){
    using(var gzip=new GZipStream(stream,CompressionMode.Compress,true))using(var writer=XmlDictionaryWriter.CreateBinaryWriter(gzip,null,null,false))new DataContractSerializer(typeof(Entry)).WriteObject(writer,entry);
    return stream.ToArray();
   }
  }
  Entry Decode(byte[] bytes)
  {
   Interlocked.Increment(ref decodedEntries);using(var stream=new MemoryStream(bytes))using(var gzip=new GZipStream(stream,CompressionMode.Decompress))using(var reader=XmlDictionaryReader.CreateBinaryReader(gzip,XmlDictionaryReaderQuotas.Max))return (Entry)new DataContractSerializer(typeof(Entry)).ReadObject(reader);
  }
  static long EnsureWord(Sqlite db,string word,string pair)
  {
   db.Execute("INSERT OR IGNORE INTO words(normalized,pair,word) VALUES(?,?,?)",Normal(word),pair,word);
   return db.Scalar("SELECT id FROM words WHERE normalized=? AND pair=?",Normal(word),pair);
  }
  bool StoreCore(Sqlite db,Entry entry)
  {
   if(entry==null||string.IsNullOrWhiteSpace(entry.Word))throw new InvalidDataException("词条为空。");
   string pair=entry.Pair??Translator.DetectPair(entry.Word);long id=EnsureWord(db,entry.Word,pair),ticks=CacheMerge.QueryTime(entry).UtcTicks;
   string key=LookupCache.KeyForEntry(entry);byte[] payload=Encode(entry);bool ai=LookupSources.IsDeepSeek(entry);
   db.Execute("INSERT OR IGNORE INTO sources(source_key,word_id,provider,is_ai,summary,ticks,payload) VALUES(?,?,?,?,?,?,?)",key,id,entry.TranslationProvider??"MyMemory",ai?1:0,entry.ShortTranslation,ticks,payload);
   bool changed=db.Changes>0;
   if(!changed){db.Execute("UPDATE sources SET provider=?,is_ai=?,summary=?,ticks=?,payload=? WHERE source_key=? AND (ticks<? OR (ticks=? AND payload<?))",entry.TranslationProvider??"MyMemory",ai?1:0,entry.ShortTranslation,ticks,payload,key,ticks,ticks,payload);changed=db.Changes>0;}
   if(changed){db.Execute("UPDATE words SET word=CASE WHEN last_ticks<=? THEN ? ELSE word END,last_ticks=MAX(last_ticks,?),summary=COALESCE((SELECT summary FROM sources WHERE word_id=? ORDER BY is_ai DESC,ticks DESC,source_key LIMIT 1),'') WHERE id=?",ticks,entry.Word,ticks,id,id);}
   return changed;
  }
  static bool FavoriteCore(Sqlite db,long id,bool favorite,string at=null)
  {
   if(at==null){if(db.Scalar("SELECT favorite FROM words WHERE id=?",id)==(favorite?1:0))return false;at=DateTime.UtcNow.ToString("o");}
   db.Execute("UPDATE words SET favorite=?,favorite_at=? WHERE id=? AND (favorite_at<? OR (favorite_at=? AND ?=0 AND favorite=1))",favorite?1:0,at,id,at,at,favorite?1:0);return db.Changes>0;
  }
  public void Save(Entry entry,bool? favorite=null,bool visit=false)
  {
   Write(db=>{bool dirty=StoreCore(db,entry);long id=EnsureWord(db,entry.Word,entry.Pair??Translator.DetectPair(entry.Word));if(favorite.HasValue)dirty|=FavoriteCore(db,id,favorite.Value);if(dirty)Dirty(db);if(visit)VisitCore(db,LookupCache.KeyForEntry(entry));return 0;});
  }
  public void SetFavorite(long id,bool favorite){Write(db=>{if(FavoriteCore(db,id,favorite))Dirty(db);return 0;});}
  WordSelection Selection(Sqlite.Statement row){return new WordSelection{Entry=Decode(row.Blob(0)),Id=row.Long(1),IsFavorite=row.Long(2)!=0};}
  public WordSelection Find(string word,string pair,bool deepSeekOnly=false)
  {
   return Read(db=>{using(var row=db.Prepare("SELECT s.payload,w.id,w.favorite FROM words w JOIN sources s ON s.word_id=w.id WHERE w.normalized=? AND w.pair=? "+(deepSeekOnly?"AND s.is_ai=1 ":"")+"ORDER BY s.ticks DESC,s.is_ai DESC,s.source_key LIMIT 1",Normal(word),pair))return row.Read()?Selection(row):null;});
  }
  public bool IsFavorite(string word,string pair){return Read(db=>db.Scalar("SELECT favorite FROM words WHERE normalized=? AND pair=?",Normal(word),pair)!=0);}
  public WordSelection Get(long id){return Read(db=>{using(var row=db.Prepare("SELECT s.payload,w.id,w.favorite FROM sources s JOIN words w ON w.id=s.word_id WHERE w.id=? ORDER BY s.ticks DESC,s.is_ai DESC,s.source_key LIMIT 1",id))return row.Read()?Selection(row):null;});}
  public WordPage Page(string filter,bool onlyFavorites,int page,int size=50)
  {
   size=Math.Max(1,Math.Min(100,size));page=Math.Max(0,page);filter=(filter??"").Trim().ToLowerInvariant();
   return Read(db=>{
    var result=new WordPage{Total=db.Scalar("SELECT COUNT(*) FROM words"),Favorites=db.Scalar("SELECT COUNT(*) FROM words WHERE favorite=1")};
    string where=" WHERE (?=0 OR w.favorite=1) AND (?='' OR instr(lower(w.word),?)>0 OR instr(lower(w.summary),?)>0)";
    result.Matches=db.Scalar("SELECT COUNT(*) FROM words w"+where,onlyFavorites?1:0,filter,filter,filter);
    using(var row=db.Prepare("SELECT w.id,w.word,w.pair,w.summary,w.favorite,EXISTS(SELECT 1 FROM sources s WHERE s.word_id=w.id AND s.is_ai=1),EXISTS(SELECT 1 FROM sources s WHERE s.word_id=w.id AND s.is_ai=0) FROM words w"+where+" ORDER BY w.last_ticks DESC,w.id DESC LIMIT ? OFFSET ?",onlyFavorites?1:0,filter,filter,filter,size,(long)page*size))while(row.Read())result.Rows.Add(new WordRow{Id=row.Long(0),Word=row.Text(1),Pair=row.Text(2),Summary=row.Text(3),IsFavorite=row.Long(4)!=0,HasDeepSeek=row.Long(5)!=0,HasOnline=row.Long(6)!=0});
    return result;
   });
  }
  static void VisitCore(Sqlite db,string key)
  {
   long cursor=Convert.ToInt64(Meta(db,"cursor")??"-1");if(db.Text("SELECT source_key FROM history WHERE position=?",cursor)==key)return;
   db.Execute("DELETE FROM history WHERE position>?",cursor);db.Execute("INSERT INTO history(position,source_key) VALUES(?,?)",cursor+1,key);Meta(db,"cursor",(cursor+1).ToString());
  }
  public void Visit(Entry entry){Write(db=>{VisitCore(db,LookupCache.KeyForEntry(entry));return 0;});}
  public DatabaseHistory History(int delta=0,bool includeCurrent=true)
  {
   return Write(db=>{long cursor=Convert.ToInt64(Meta(db,"cursor")??"-1"),last=db.Scalar("SELECT COALESCE(MAX(position),-1) FROM history");if(delta!=0&&cursor+delta>=0&&cursor+delta<=last){cursor+=delta;Meta(db,"cursor",cursor.ToString());}
    WordSelection current=null;using(var row=db.Prepare("SELECT s.payload,w.id,w.favorite FROM history h JOIN sources original ON original.source_key=h.source_key JOIN words w ON w.id=original.word_id JOIN sources s ON s.word_id=w.id WHERE h.position=? ORDER BY s.ticks DESC,s.is_ai DESC,s.source_key LIMIT 1",cursor))if(includeCurrent&&row.Read())current=Selection(row);
    return new DatabaseHistory{CanBack=cursor>0,CanForward=cursor<last,Current=current};});
  }
  public long Revision {get{return Read(db=>db.Scalar("SELECT CAST(value AS INTEGER) FROM metadata WHERE key='revision'"));}}
  public bool PendingSync {get{return Read(db=>db.Scalar("SELECT CAST((SELECT value FROM metadata WHERE key='revision') AS INTEGER)>CAST((SELECT value FROM metadata WHERE key='synced') AS INTEGER)")!=0);}}
  public void MarkSynced(long revision){Write(db=>{Meta(db,"synced",revision.ToString());return 0;});}
  public long BackupTo(string destination){return Read(db=>{long version=db.Scalar("SELECT CAST(value AS INTEGER) FROM metadata WHERE key='revision'");db.BackupTo(destination);return version;});}
  public string IntegrityCheck(){return Read(db=>db.Text("PRAGMA integrity_check"));}
  public void ExportFavoritesCsv(string path)
  {
   using(var writer=new StreamWriter(path,false,new UTF8Encoding(true)))Read(db=>{writer.WriteLine("单词,翻译,方向,收藏时间,词典详解");using(var rows=db.Prepare("SELECT w.id FROM words w WHERE favorite=1 ORDER BY last_ticks DESC,id DESC"))while(rows.Read()){var selected=Get(rows.Long(0));writer.Write(FavoriteStore.Csv(new[]{selected.Entry}).Split(new[]{"\r\n"},2,StringSplitOptions.None)[1]);writer.WriteLine();}return 0;});
  }
  public void MigrateLegacy(string lookup,string favorites)
  {
   Write(db=>{
    bool changed=false;var serializer=JsonFiles.Serializer();
    foreach(var file in new[]{lookup,favorites}){
     if(string.IsNullOrEmpty(file)||!File.Exists(file))continue;string key="legacy:"+Path.GetFullPath(file),fingerprint=new FileInfo(file).Length+":"+File.GetLastWriteTimeUtc(file).Ticks;if(Meta(db,key)==fingerprint)continue;
     bool isLookup=file==lookup;var history=new List<string>();int cursor=-1;
     using(var reader=new LegacyJson(file))reader.Read(isLookup?"Results":"Changes",(entryKey,raw)=>{
      if(isLookup){var entry=serializer.Deserialize<Entry>(raw);changed|=StoreCore(db,entry);}
      else {if(raw.Contains("\"Entry\"")){var favorite=serializer.Deserialize<FavoriteChange>(raw);if(favorite?.Entry==null)throw new InvalidDataException("旧收藏无效。");changed|=StoreCore(db,favorite.Entry);changed|=FavoriteCore(db,EnsureWord(db,favorite.Entry.Word,favorite.Entry.Pair??Translator.DetectPair(favorite.Entry.Word)),!favorite.Deleted,favorite.UpdatedAtUtc??"1970-01-01T00:00:00.0000000Z");}
       else {var entry=serializer.Deserialize<Entry>(raw);changed|=StoreCore(db,entry);changed|=FavoriteCore(db,EnsureWord(db,entry.Word,entry.Pair??Translator.DetectPair(entry.Word)),true,"1970-01-01T00:00:00.0000000Z");}}
     },(name,raw)=>{if(name=="username"&&raw!="null"&&serializer.Deserialize<string>(raw)!=Username)throw new InvalidDataException("旧文件 username 不匹配。");if(isLookup&&name=="History")history=serializer.Deserialize<List<string>>(raw);if(isLookup&&name=="Position")cursor=serializer.Deserialize<int>(raw);});
     if(isLookup&&db.Scalar("SELECT COUNT(*) FROM history")==0){int index=0;foreach(var sourceKey in history){if(db.Scalar("SELECT COUNT(*) FROM sources WHERE source_key=?",sourceKey)>0)db.Execute("INSERT INTO history(position,source_key) VALUES(?,?)",index++,sourceKey);}Meta(db,"cursor",(index==0?-1:Math.Max(0,Math.Min(cursor,index-1))).ToString());}
     Meta(db,key,fingerprint);
    }
    if(changed)Dirty(db);return 0;
   });
  }
  public void MigrateSyncJson(string path)
  {
   Write(db=>{
    string key="legacy-sync:"+Path.GetFullPath(path),fingerprint=new FileInfo(path).Length+":"+File.GetLastWriteTimeUtc(path).Ticks;if(Meta(db,key)==fingerprint)return 0;
    bool changed=false;var serializer=JsonFiles.Serializer();
    using(var reader=new LegacyJson(path))reader.ReadSync((lookup,entryKey,raw)=>{
     if(lookup)changed|=StoreCore(db,serializer.Deserialize<Entry>(raw));
     else {var change=serializer.Deserialize<FavoriteChange>(raw);if(change?.Entry==null)throw new InvalidDataException("远端收藏无效。");changed|=StoreCore(db,change.Entry);changed|=FavoriteCore(db,EnsureWord(db,change.Entry.Word,change.Entry.Pair??Translator.DetectPair(change.Entry.Word)),!change.Deleted,change.UpdatedAtUtc);}
    },(name,raw)=>{if(name=="username"&&raw!="null"&&serializer.Deserialize<string>(raw)!=Username)throw new InvalidDataException("远端 username 不匹配。");});
    Meta(db,key,fingerprint);if(changed)Dirty(db);return 0;
   });
  }
  public void RecordImportError(string word,string error){Write(db=>{db.Execute("INSERT INTO import_errors(word,error,at) VALUES(?,?,?)",word,error,DateTime.UtcNow.ToString("o"));return 0;});}
  public void MergeFrom(string path)
  {
   using(var source=new Sqlite(path,true)){
    if(Meta(source,"username")!=Username||Meta(source,"schema")!="1")throw new InvalidDataException("远端数据库 username 或版本不匹配。");
    string key="remote:"+Path.GetFullPath(path),stamp=new FileInfo(path).Length+":"+File.GetLastWriteTimeUtc(path).Ticks+":"+Meta(source,"revision");
    if(Read(db=>Meta(db,key))==stamp)return;
    var batch=new List<Entry>(50);
    Action flush=()=>{if(batch.Count==0)return;Write(db=>{bool changed=false;foreach(var entry in batch)changed|=StoreCore(db,entry);if(changed)Dirty(db);return 0;});batch.Clear();};
    using(var rows=source.Prepare("SELECT payload FROM sources"))while(rows.Read()){batch.Add(Decode(rows.Blob(0)));if(batch.Count==50)flush();}flush();
    using(var rows=source.Prepare("SELECT word,pair,favorite,favorite_at FROM words WHERE favorite_at<>''"))while(rows.Read()){
     string word=rows.Text(0),pair=rows.Text(1),at=rows.Text(3);bool favorite=rows.Long(2)!=0;
     Write(db=>{if(FavoriteCore(db,EnsureWord(db,word,pair),favorite,at))Dirty(db);return 0;});
    }
    Write(db=>{Meta(db,key,stamp);return 0;});
   }
  }

 }
}
