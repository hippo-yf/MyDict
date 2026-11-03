using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Xml;
using System.Threading;
using System.Globalization;
using System.Web.Script.Serialization;
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
  public const string SharedPayloadFormat="mydict-json-v1";
  const int MaxPayloadBytes=32*1024*1024;
  static readonly byte[] PublicPrefix=Encoding.ASCII.GetBytes("MYDICT-JSON1\n");
  static readonly byte[] QtPrefix=Encoding.ASCII.GetBytes("MYDICT-QT-JSON1\n");
  static readonly UTF8Encoding StrictUtf8=new UTF8Encoding(false,true);
  public string FilePath {get;private set;} public string Username {get;private set;}
  readonly object gate=new object();
  private long decodedEntries;
  public long DecodedEntries {get{return Interlocked.Read(ref decodedEntries);}}
  static string Normal(string word){return (word??"").Trim().Normalize().ToLowerInvariant();}
  public WordDatabase(string path,string username){FilePath=Path.GetFullPath(path);Username=username;}
  public static WordDatabase OpenShared(string profileDirectory,string username)
  {
   string profile=Path.GetFullPath(profileDirectory);
   var shared=new WordDatabase(Path.Combine(profile,"words-shared.sqlite"),username);shared.Open();
   foreach(string name in new[]{"words.sqlite","words-qt.sqlite"}){
    string legacy=Path.Combine(profile,name);if(!File.Exists(legacy))continue;
    shared.MergeFrom(legacy);shared.ImportLocalMetadata(legacy);
   }
   return shared;
  }
  public void ImportLocalDatabase(string path){MergeFrom(path);ImportLocalMetadata(path);}
  T Read<T>(Func<Sqlite,T> action){using(var db=new Sqlite(FilePath))return action(db);}
  T Write<T>(Func<Sqlite,T> action){lock(gate)return Read(db=>{db.Execute("BEGIN IMMEDIATE");try{var result=action(db);db.Execute("COMMIT");return result;}catch{db.Execute("ROLLBACK");throw;}});}
  static string Meta(Sqlite db,string key){return db.Text("SELECT value FROM metadata WHERE key=?",key);}
  static void Meta(Sqlite db,string key,string value){db.Execute("INSERT OR REPLACE INTO metadata(key,value) VALUES(?,?)",key,value);}
  static void Dirty(Sqlite db){db.Execute("UPDATE metadata SET value=CAST(value AS INTEGER)+1 WHERE key='revision'");}
  static void VerifySource(Sqlite db,string username)
  {
   if(Meta(db,"username")!=username||Meta(db,"schema")!="1")throw new InvalidDataException("数据库 username 或版本不匹配。");
   string format=Meta(db,"payload_format");
   if(format!=null&&format!="qt-json-v1"&&format!=SharedPayloadFormat)throw new InvalidDataException("不支持的词库 payload_format，原文件已保留。");
  }
  public void Open()
  {
   bool existing=File.Exists(FilePath)&&new FileInfo(FilePath).Length>0;
   // Validate before WAL/schema pragmas so a wrong-user/unknown database stays untouched.
   if(existing)using(var source=new Sqlite(FilePath,true))VerifySource(source,Username);
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
    db.Execute("CREATE TABLE IF NOT EXISTS favorite_events(normalized TEXT NOT NULL,pair TEXT NOT NULL,favorite INTEGER NOT NULL,updated_at TEXT NOT NULL,PRIMARY KEY(normalized,pair))");
    db.Execute("CREATE TABLE IF NOT EXISTS import_errors(id INTEGER PRIMARY KEY,word TEXT NOT NULL,error TEXT NOT NULL,at TEXT NOT NULL)");
    db.Execute("CREATE TABLE IF NOT EXISTS history(position INTEGER PRIMARY KEY,source_key TEXT NOT NULL)");
    db.Execute("CREATE TABLE IF NOT EXISTS deleted_words(normalized TEXT NOT NULL,pair TEXT NOT NULL,deleted_ticks INTEGER NOT NULL,PRIMARY KEY(normalized,pair))");
    Meta(db,"username",Username);Meta(db,"schema","1");
    // An explicitly opened legacy database may still contain binary rows. Do not
    // falsely label it public; the normal app opens a separate shared file instead.
    if(!existing)Meta(db,"payload_format",SharedPayloadFormat);
    long highest=db.Scalar("SELECT COALESCE(MAX(id),0) FROM words");if(Convert.ToInt64(Meta(db,"word_sequence")??"0")<highest)Meta(db,"word_sequence",highest.ToString());
    db.Execute("INSERT OR IGNORE INTO metadata(key,value) VALUES('revision','0'),('synced','0'),('cursor','-1')");return 0;
   });
  }
  static byte[] Encode(Entry entry)
  {
   return EncodePublicCanonical(entry);
  }
  static byte[] EncodePublicCanonical(Entry entry)
  {
   // QJsonDocument writes keys in UTF-16 ordinal order, keeps Unicode as UTF-8,
   // and escapes only JSON syntax/control characters. Match it byte-for-byte.
   var serializer=new JavaScriptSerializer{MaxJsonLength=MaxPayloadBytes,RecursionLimit=128};
   var builder=new StringBuilder();CanonicalJson(builder,serializer.DeserializeObject(serializer.Serialize(entry)));
   byte[] json=StrictUtf8.GetBytes(builder.ToString());
   if(json.Length>MaxPayloadBytes-PublicPrefix.Length)throw new InvalidDataException("词条 JSON 超过 32 MiB 限制。");
   var result=new byte[PublicPrefix.Length+json.Length];Buffer.BlockCopy(PublicPrefix,0,result,0,PublicPrefix.Length);Buffer.BlockCopy(json,0,result,PublicPrefix.Length,json.Length);return result;
  }
  static void CanonicalString(StringBuilder output,string text)
  {
   output.Append('"');
   for(int i=0;i<text.Length;i++){
    char c=text[i];
    switch(c){case '"':output.Append("\\\"");break;case '\\':output.Append("\\\\");break;case '\b':output.Append("\\b");break;case '\f':output.Append("\\f");break;case '\n':output.Append("\\n");break;case '\r':output.Append("\\r");break;case '\t':output.Append("\\t");break;
     default:
      if(c<0x20||(char.IsSurrogate(c)&&!(char.IsHighSurrogate(c)&&i+1<text.Length&&char.IsLowSurrogate(text[i+1]))))output.Append("\\u").Append(((int)c).ToString("x4",CultureInfo.InvariantCulture));
      else {output.Append(c);if(char.IsHighSurrogate(c))output.Append(text[++i]);}
      break;
    }
   }
   output.Append('"');
  }
  static void CanonicalJson(StringBuilder output,object value)
  {
   if(value==null){output.Append("null");return;}
   var text=value as string;if(text!=null){CanonicalString(output,text);return;}
   var map=value as IDictionary<string,object>;
   if(map!=null){output.Append('{');bool first=true;foreach(string key in map.Keys.OrderBy(k=>k,StringComparer.Ordinal)){if(!first)output.Append(',');first=false;CanonicalString(output,key);output.Append(':');CanonicalJson(output,map[key]);}output.Append('}');return;}
   if(value is bool){output.Append((bool)value?"true":"false");return;}
   var list=value as System.Collections.IEnumerable;
   if(list!=null){output.Append('[');bool first=true;foreach(object item in list){if(!first)output.Append(',');first=false;CanonicalJson(output,item);}output.Append(']');return;}
   if(value is IFormattable){output.Append(((IFormattable)value).ToString(null,CultureInfo.InvariantCulture));return;}
   throw new InvalidDataException("不支持的词条 JSON 字段类型。");
  }
  static bool StartsWith(byte[] bytes,byte[] prefix){if(bytes.Length<prefix.Length)return false;for(int i=0;i<prefix.Length;i++)if(bytes[i]!=prefix[i])return false;return true;}
  static Entry DecodePayload(byte[] bytes)
  {
   if(bytes==null||bytes.Length>MaxPayloadBytes)throw new InvalidDataException("词条 payload 超过 32 MiB 限制。");
   int prefix=StartsWith(bytes,PublicPrefix)?PublicPrefix.Length:StartsWith(bytes,QtPrefix)?QtPrefix.Length:0;
   Entry entry;
   if(prefix>0){
    try{entry=new JavaScriptSerializer{MaxJsonLength=MaxPayloadBytes,RecursionLimit=128}.Deserialize<Entry>(StrictUtf8.GetString(bytes,prefix,bytes.Length-prefix));}
    catch(Exception ex)when(ex is ArgumentException||ex is InvalidOperationException){throw new InvalidDataException("词条 JSON 无效。",ex);}
   }else{
    if(bytes.Length<2||bytes[0]!=0x1f||bytes[1]!=0x8b)throw new InvalidDataException("不支持的词条 payload，原文件已保留。");
    using(var stream=new MemoryStream(bytes))using(var gzip=new GZipStream(stream,CompressionMode.Decompress))using(var plain=new MemoryStream()){
     var buffer=new byte[8192];int count;while((count=gzip.Read(buffer,0,buffer.Length))>0){if(plain.Length+count>MaxPayloadBytes)throw new InvalidDataException("旧词条展开后超过 32 MiB 限制。");plain.Write(buffer,0,count);}plain.Position=0;
     var quotas=new XmlDictionaryReaderQuotas{MaxDepth=128,MaxStringContentLength=MaxPayloadBytes,MaxArrayLength=MaxPayloadBytes,MaxBytesPerRead=8192,MaxNameTableCharCount=MaxPayloadBytes};
     using(var reader=XmlDictionaryReader.CreateBinaryReader(plain,quotas))entry=(Entry)new DataContractSerializer(typeof(Entry)).ReadObject(reader);
    }
   }
   if(entry==null||string.IsNullOrWhiteSpace(entry.Word))throw new InvalidDataException("词条内容无效。");return entry;
  }
  Entry Decode(byte[] bytes)
  {
   Interlocked.Increment(ref decodedEntries);return DecodePayload(bytes);
  }
  static long EnsureWord(Sqlite db,string word,string pair)
  {
   long existing=WordId(db,word,pair);if(existing!=0)return existing;
   long sequence=Math.Max(db.Scalar("SELECT COALESCE(MAX(id),0) FROM words"),Convert.ToInt64(Meta(db,"word_sequence")??"0"))+1;
   db.Execute("INSERT INTO words(id,normalized,pair,word) VALUES(?,?,?,?)",sequence,Normal(word),pair,word);Meta(db,"word_sequence",sequence.ToString());
   using(var pending=db.Prepare("SELECT favorite,updated_at FROM favorite_events WHERE normalized=? AND pair=?",Normal(word),pair))if(pending.Read())FavoriteEventCore(db,word,pair,pending.Long(0)!=0,pending.Text(1));
   return sequence;
  }
  bool StoreCore(Sqlite db,Entry entry,long intentTicks=0)
  {
   if(entry==null||string.IsNullOrWhiteSpace(entry.Word))throw new InvalidDataException("词条为空。");
   string pair=entry.Pair??Translator.DetectPair(entry.Word);long ticks=CacheMerge.QueryTime(entry).UtcTicks;
   long deleted=db.Scalar("SELECT deleted_ticks FROM deleted_words WHERE normalized=? AND pair=?",Normal(entry.Word),pair);if(deleted>0&&Math.Max(ticks,intentTicks)<=deleted)return false;
   long id=EnsureWord(db,entry.Word,pair);
   string key=LookupCache.KeyForEntry(entry);byte[] payload=Encode(entry);bool ai=LookupSources.IsDeepSeek(entry);
   db.Execute("INSERT OR IGNORE INTO sources(source_key,word_id,provider,is_ai,summary,ticks,payload) VALUES(?,?,?,?,?,?,?)",key,id,entry.TranslationProvider??"MyMemory",ai?1:0,entry.ShortTranslation,ticks,payload);
   bool changed=db.Changes>0;
   if(!changed){db.Execute("UPDATE sources SET provider=?,is_ai=?,summary=?,ticks=?,payload=? WHERE source_key=? AND (ticks<? OR (ticks=? AND payload<?))",entry.TranslationProvider??"MyMemory",ai?1:0,entry.ShortTranslation,ticks,payload,key,ticks,ticks,payload);changed=db.Changes>0;}
   if(changed){db.Execute("UPDATE words SET word=CASE WHEN last_ticks<=? THEN ? ELSE word END,last_ticks=MAX(last_ticks,?),summary=COALESCE((SELECT summary FROM sources WHERE word_id=? ORDER BY is_ai DESC,ticks DESC,source_key LIMIT 1),'') WHERE id=?",ticks,entry.Word,ticks,id,id);}
   return changed;
  }
  static bool FavoriteEventCore(Sqlite db,string word,string pair,bool favorite,string at)
  {
   DateTimeOffset eventTime;if(!DateTimeOffset.TryParse(at,CultureInfo.InvariantCulture,DateTimeStyles.AssumeUniversal,out eventTime)||eventTime.UtcTicks==0)return false;
   at=eventTime.ToUniversalTime().ToString("o",CultureInfo.InvariantCulture);string normalized=Normal(word);
   db.Execute("INSERT OR IGNORE INTO favorite_events(normalized,pair,favorite,updated_at) VALUES(?,?,?,?)",normalized,pair,favorite?1:0,at);bool changed=db.Changes>0;
   if(!changed){db.Execute("UPDATE favorite_events SET favorite=?,updated_at=? WHERE normalized=? AND pair=? AND (updated_at<? OR (updated_at=? AND ?=0 AND favorite=1))",favorite?1:0,at,normalized,pair,at,at,favorite?1:0);changed=db.Changes>0;}
   bool latestFavorite;string latestAt;
   using(var row=db.Prepare("SELECT favorite,updated_at FROM favorite_events WHERE normalized=? AND pair=?",normalized,pair)){if(!row.Read())return changed;latestFavorite=row.Long(0)!=0;latestAt=row.Text(1);}
   long deleted=db.Scalar("SELECT deleted_ticks FROM deleted_words WHERE normalized=? AND pair=?",normalized,pair);if(TimeTicks(latestAt)<=deleted)return changed;
   db.Execute("UPDATE words SET favorite=?,favorite_at=? WHERE normalized=? AND pair=? AND (favorite_at<? OR (favorite_at=? AND ?=0 AND favorite=1))",latestFavorite?1:0,latestAt,normalized,pair,latestAt,latestAt,latestFavorite?1:0);return changed||db.Changes>0;
  }
  static bool FavoriteCore(Sqlite db,long id,bool favorite,string at=null)
  {
   if(id<=0)return false;string word,pair;bool previous;long previousTicks;
   using(var row=db.Prepare("SELECT word,pair,favorite,favorite_at FROM words WHERE id=?",id)){if(!row.Read())return false;word=row.Text(0);pair=row.Text(1);previous=row.Long(2)!=0;previousTicks=TimeTicks(row.Text(3));}
   if(at==null){
    if(previous==favorite)return false;
    // A deliberate local action must sort after already observed events even
    // when another device's clock runs ahead. Imported timestamps remain intact.
    long ledger=TimeTicks(db.Text("SELECT updated_at FROM favorite_events WHERE normalized=? AND pair=?",Normal(word),pair));
    long deleted=db.Scalar("SELECT deleted_ticks FROM deleted_words WHERE normalized=? AND pair=?",Normal(word),pair);
    long latest=Math.Max(previousTicks,Math.Max(ledger,deleted));
    long ticks=Math.Max(DateTimeOffset.UtcNow.UtcTicks,checked(latest+1));
    at=new DateTimeOffset(ticks,TimeSpan.Zero).ToString("o",CultureInfo.InvariantCulture);
   }
   return FavoriteEventCore(db,word,pair,favorite,at);
  }
  public void Save(Entry entry,bool? favorite=null,bool visit=false)
  {
   Write(db=>{bool dirty=StoreCore(db,entry,favorite==true?DateTimeOffset.UtcNow.UtcTicks:0);long id=db.Scalar("SELECT id FROM words WHERE normalized=? AND pair=?",Normal(entry.Word),entry.Pair??Translator.DetectPair(entry.Word));if(id==0)return 0;if(favorite.HasValue)dirty|=FavoriteCore(db,id,favorite.Value);if(dirty)Dirty(db);if(visit&&db.Scalar("SELECT COUNT(*) FROM sources WHERE source_key=?",LookupCache.KeyForEntry(entry))>0)VisitCore(db,LookupCache.KeyForEntry(entry));return 0;});
  }
  static long WordId(Sqlite db,string word,string pair){return db.Scalar("SELECT id FROM words WHERE normalized=? AND pair=?",Normal(word),pair);}
  static long TimeTicks(string value){DateTimeOffset time;return DateTimeOffset.TryParse(value,System.Globalization.CultureInfo.InvariantCulture,System.Globalization.DateTimeStyles.AssumeUniversal,out time)?time.UtcTicks:0;}
  static void RemoveContentCore(Sqlite db,long id)
  {
   long cursor=Convert.ToInt64(Meta(db,"cursor")??"-1");
   db.Execute("DELETE FROM history WHERE source_key IN (SELECT source_key FROM sources WHERE word_id=?)",id);bool historyChanged=db.Changes>0;
   db.Execute("DELETE FROM sources WHERE word_id=?",id);db.Execute("DELETE FROM words WHERE id=?",id);
   if(historyChanged){
    // Renumber only history metadata; never decode surviving dictionary entries.
    long previous=db.Scalar("SELECT COUNT(*) FROM history WHERE position<=?",cursor)-1;
    db.Execute("CREATE TEMP TABLE compact_history(position INTEGER PRIMARY KEY AUTOINCREMENT,source_key TEXT NOT NULL)");
    db.Execute("INSERT INTO compact_history(source_key) SELECT source_key FROM history ORDER BY position");db.Execute("DELETE FROM history");
    db.Execute("INSERT INTO history(position,source_key) SELECT position-1,source_key FROM compact_history ORDER BY position");db.Execute("DROP TABLE compact_history");
    long last=db.Scalar("SELECT COALESCE(MAX(position),-1) FROM history");Meta(db,"cursor",(last<0?-1:Math.Max(0,Math.Min(previous,last))).ToString());
   }
  }
  static bool DeleteCore(Sqlite db,string normalized,string pair,long ticks)
  {
   long before=db.Scalar("SELECT deleted_ticks FROM deleted_words WHERE normalized=? AND pair=?",normalized,pair);if(before>=ticks)return false;
   db.Execute("INSERT OR REPLACE INTO deleted_words(normalized,pair,deleted_ticks) VALUES(?,?,?)",normalized,pair,ticks);
   long removeId=0;
   using(var row=db.Prepare("SELECT id,last_ticks,favorite_at FROM words WHERE normalized=? AND pair=?",normalized,pair))if(row.Read()){
    long id=row.Long(0),latest=Math.Max(row.Long(1),TimeTicks(row.Text(2)));if(latest<=ticks)removeId=id;
   }
   if(removeId!=0)RemoveContentCore(db,removeId);
   return true;
  }
  public bool RemoveWord(long id)
  {
   return Write(db=>{
    string normalized,pair;long latest;
    using(var row=db.Prepare("SELECT normalized,pair,favorite,last_ticks,favorite_at FROM words WHERE id=?",id)){
     if(!row.Read()||row.Long(2)!=0)return false;normalized=row.Text(0);pair=row.Text(1);latest=Math.Max(row.Long(3),TimeTicks(row.Text(4)));
    }
    long ticks=Math.Max(DateTimeOffset.UtcNow.UtcTicks,Math.Max(latest,db.Scalar("SELECT deleted_ticks FROM deleted_words WHERE normalized=? AND pair=?",normalized,pair)))+1;
    DeleteCore(db,normalized,pair,ticks);Dirty(db);return true;
   });
  }
  public bool HasDeepSeek(string word,string pair){return Read(db=>db.Scalar("SELECT EXISTS(SELECT 1 FROM words w JOIN sources s ON s.word_id=w.id WHERE w.normalized=? AND w.pair=? AND s.is_ai=1)",Normal(word),pair)!=0);}
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
  public void Visit(Entry entry){Write(db=>{string key=LookupCache.KeyForEntry(entry);if(db.Scalar("SELECT COUNT(*) FROM sources WHERE source_key=?",key)>0)VisitCore(db,key);return 0;});}
  public DatabaseHistory History(int delta=0,bool includeCurrent=true)
  {
   return Write(db=>{long cursor=Convert.ToInt64(Meta(db,"cursor")??"-1"),last=db.Scalar("SELECT COALESCE(MAX(position),-1) FROM history");if(delta!=0&&cursor+delta>=0&&cursor+delta<=last){cursor+=delta;Meta(db,"cursor",cursor.ToString());}
    WordSelection current=null;using(var row=db.Prepare("SELECT s.payload,w.id,w.favorite FROM history h JOIN sources original ON original.source_key=h.source_key JOIN words w ON w.id=original.word_id JOIN sources s ON s.word_id=w.id WHERE h.position=? ORDER BY s.ticks DESC,s.is_ai DESC,s.source_key LIMIT 1",cursor))if(includeCurrent&&row.Read())current=Selection(row);
    return new DatabaseHistory{CanBack=cursor>0,CanForward=cursor<last,Current=current};});
  }
  public long Revision {get{return Read(db=>db.Scalar("SELECT CAST(value AS INTEGER) FROM metadata WHERE key='revision'"));}}
  public bool PendingSync {get{return Read(db=>db.Scalar("SELECT CAST((SELECT value FROM metadata WHERE key='revision') AS INTEGER)>CAST((SELECT value FROM metadata WHERE key='synced') AS INTEGER)")!=0);}}
  public void MarkSynced(long revision){Write(db=>{Meta(db,"synced",revision.ToString());return 0;});}
  public long BackupTo(string destination)
  {
   string target=Path.GetFullPath(destination);if(string.Equals(target,FilePath,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("备份目标不能是当前词库文件。");
   Directory.CreateDirectory(Path.GetDirectoryName(target));string temporary=target+".shared-"+Guid.NewGuid().ToString("N")+".tmp";
   try{
    long version=Read(db=>{long revision=db.Scalar("SELECT CAST(value AS INTEGER) FROM metadata WHERE key='revision'");db.BackupTo(temporary);return revision;});
    using(var snapshot=new Sqlite(temporary)){
     VerifySource(snapshot,Username);snapshot.Execute("BEGIN IMMEDIATE");
     try{
      string after="";bool first=true;
      while(true){
       // Close SELECT before UPDATE; retain at most one bounded batch of payloads.
       var batch=new List<Tuple<string,byte[]>>(50);long size=0;
       using(var rows=snapshot.Prepare("SELECT source_key,payload,length(payload) FROM sources WHERE (?=1 OR source_key>?) ORDER BY source_key LIMIT 50",first?1:0,after))while(rows.Read()){
        if(rows.Long(2)>MaxPayloadBytes)throw new InvalidDataException("词条 payload 超过 32 MiB 限制。");
        byte[] payload=rows.Blob(1);batch.Add(Tuple.Create(rows.Text(0),payload));size+=payload.Length;if(size>=8L*1024*1024)break;
       }
       if(batch.Count==0)break;
       foreach(var row in batch){if(!StartsWith(row.Item2,PublicPrefix))snapshot.Execute("UPDATE sources SET payload=? WHERE source_key=?",EncodePublicCanonical(DecodePayload(row.Item2)),row.Item1);after=row.Item1;}
       first=false;
      }
      if(Meta(snapshot,"payload_format")!=SharedPayloadFormat)Meta(snapshot,"payload_format",SharedPayloadFormat);snapshot.Execute("COMMIT");
     }catch{snapshot.Execute("ROLLBACK");throw;}
    }
    // A native backup can carry WAL mode. Close/checkpoint it before publishing a
    // single portable file; no user data is ever written to the source database.
    using(var snapshot=new Sqlite(temporary)){snapshot.Execute("PRAGMA wal_checkpoint(TRUNCATE)");snapshot.Execute("PRAGMA journal_mode=DELETE");}
    if(File.Exists(target))File.Replace(temporary,target,null);else File.Move(temporary,target);return version;
   }finally{foreach(string file in new[]{temporary,temporary+"-wal",temporary+"-shm"})if(File.Exists(file))File.Delete(file);}
  }
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
      else {if(raw.Contains("\"Entry\"")){var favorite=serializer.Deserialize<FavoriteChange>(raw);if(favorite?.Entry==null)throw new InvalidDataException("旧收藏无效。");changed|=StoreCore(db,favorite.Entry,!favorite.Deleted?TimeTicks(favorite.UpdatedAtUtc):0);changed|=FavoriteEventCore(db,favorite.Entry.Word,favorite.Entry.Pair??Translator.DetectPair(favorite.Entry.Word),!favorite.Deleted,favorite.UpdatedAtUtc??"1970-01-01T00:00:00.0000000Z");}
       else {var entry=serializer.Deserialize<Entry>(raw);changed|=StoreCore(db,entry,TimeTicks("1970-01-01T00:00:00.0000000Z"));changed|=FavoriteCore(db,WordId(db,entry.Word,entry.Pair??Translator.DetectPair(entry.Word)),true,"1970-01-01T00:00:00.0000000Z");}}
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
     else {var change=serializer.Deserialize<FavoriteChange>(raw);if(change?.Entry==null)throw new InvalidDataException("远端收藏无效。");changed|=StoreCore(db,change.Entry,!change.Deleted?TimeTicks(change.UpdatedAtUtc):0);changed|=FavoriteEventCore(db,change.Entry.Word,change.Entry.Pair??Translator.DetectPair(change.Entry.Word),!change.Deleted,change.UpdatedAtUtc);}
    },(name,raw)=>{if(name=="username"&&raw!="null"&&serializer.Deserialize<string>(raw)!=Username)throw new InvalidDataException("远端 username 不匹配。");});
    Meta(db,key,fingerprint);if(changed)Dirty(db);return 0;
   });
  }
  public void RecordImportError(string word,string error){Write(db=>{db.Execute("INSERT INTO import_errors(word,error,at) VALUES(?,?,?)",word,error,DateTime.UtcNow.ToString("o"));return 0;});}
  public void MergeFrom(string path)
  {
   if(string.Equals(Path.GetFullPath(path),FilePath,StringComparison.OrdinalIgnoreCase))return;
   using(var source=new Sqlite(path,true)){
    source.Execute("BEGIN");VerifySource(source,Username);
    string key="remote:"+Path.GetFullPath(path),stamp=new FileInfo(path).Length+":"+File.GetLastWriteTimeUtc(path).Ticks+":"+Meta(source,"revision");
    if(Read(db=>Meta(db,key))==stamp)return;
    if(source.Scalar("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='deleted_words'")!=0)
     using(var deleted=source.Prepare("SELECT normalized,pair,deleted_ticks FROM deleted_words"))while(deleted.Read()){
      string normalized=deleted.Text(0),pair=deleted.Text(1);long ticks=deleted.Long(2);Write(db=>{if(DeleteCore(db,normalized,pair,ticks))Dirty(db);return 0;});
     }
    if(source.Scalar("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='favorite_events'")!=0)
     using(var events=source.Prepare("SELECT normalized,pair,favorite,updated_at FROM favorite_events"))while(events.Read()){
      string word=events.Text(0),pair=events.Text(1),at=events.Text(3);bool favorite=events.Long(2)!=0;Write(db=>{if(FavoriteEventCore(db,word,pair,favorite,at))Dirty(db);return 0;});
     }
    var batch=new List<Tuple<Entry,long>>(50);
    Action flush=()=>{if(batch.Count==0)return;Write(db=>{bool changed=false;foreach(var entry in batch)changed|=StoreCore(db,entry.Item1,entry.Item2);if(changed)Dirty(db);return 0;});batch.Clear();};
    using(var rows=source.Prepare("SELECT s.payload,w.favorite,w.favorite_at FROM sources s JOIN words w ON w.id=s.word_id"))while(rows.Read()){batch.Add(Tuple.Create(Decode(rows.Blob(0)),rows.Long(1)!=0?TimeTicks(rows.Text(2)):0));if(batch.Count==50)flush();}flush();
    using(var rows=source.Prepare("SELECT word,pair,favorite,favorite_at FROM words WHERE favorite_at<>''"))while(rows.Read()){
     string word=rows.Text(0),pair=rows.Text(1),at=rows.Text(3);bool favorite=rows.Long(2)!=0;
     Write(db=>{if(FavoriteEventCore(db,word,pair,favorite,at))Dirty(db);return 0;});
    }
    Write(db=>{Meta(db,key,stamp);return 0;});
   }
  }

  void ImportLocalMetadata(string path)
  {
   using(var source=new Sqlite(path,true)){
    source.Execute("BEGIN");VerifySource(source,Username);
    string marker="local-metadata:"+Path.GetFullPath(path);
    Write(db=>{
     if(Meta(db,marker)=="1")return 0;
     bool hasHistory=source.Scalar("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='history'")!=0;
     if(hasHistory&&db.Scalar("SELECT COUNT(*) FROM history")==0){
      long originalCursor;if(!long.TryParse(Meta(source,"cursor"),out originalCursor))originalCursor=-1;long position=0,cursor=-1;
      using(var row=source.Prepare("SELECT position,source_key FROM history ORDER BY position"))while(row.Read()){
       string key=row.Text(1);if(db.Scalar("SELECT COUNT(*) FROM sources WHERE source_key=?",key)==0)continue;
       db.Execute("INSERT INTO history(position,source_key) VALUES(?,?)",position,key);if(row.Long(0)<=originalCursor)cursor=position;position++;
      }
      Meta(db,"cursor",(position==0?-1:Math.Max(0,Math.Min(cursor,position-1))).ToString(CultureInfo.InvariantCulture));
     }
     if(source.Scalar("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='import_errors'")!=0)
      using(var row=source.Prepare("SELECT word,error,at FROM import_errors ORDER BY id"))while(row.Read()){
       string word=row.Text(0),error=row.Text(1),at=row.Text(2);
       db.Execute("INSERT INTO import_errors(word,error,at) SELECT ?,?,? WHERE NOT EXISTS(SELECT 1 FROM import_errors WHERE word=? AND error=? AND at=?)",word,error,at,word,error,at);
      }
     Meta(db,marker,"1");return 0;
    });
   }
  }

 }
}
