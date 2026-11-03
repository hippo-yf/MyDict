using System;
using System.IO;
using System.IO.Compression;
using System.Runtime.Serialization;
using System.Text;
using System.Web.Script.Serialization;
using System.Xml;
using MyDict;

// Windows-only bridge. Reads the C# database without changing it; never loads the
// whole vocabulary. The portable NDJSON can then be imported on any Qt platform.
internal static class ExportLegacy
{
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 64 * 1024 * 1024, RecursionLimit = 128 };
    static bool HasTable(Sqlite db,string name) { return db.Scalar("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?",name)>0; }
    static Entry Decode(byte[] payload) {
        using(var input=new MemoryStream(payload))
        using(var gzip=new GZipStream(input,CompressionMode.Decompress))
        using(var bytes=new MemoryStream()) {
            var buffer=new byte[8192];int count;
            while((count=gzip.Read(buffer,0,buffer.Length))>0) {bytes.Write(buffer,0,count);if(bytes.Length>32L*1024*1024)throw new InvalidDataException("单个释义展开后超过 32 MiB，导出已停止。");}
            bytes.Position=0;
            using(var reader=XmlDictionaryReader.CreateBinaryReader(bytes,XmlDictionaryReaderQuotas.Max))
                return (Entry)new DataContractSerializer(typeof(Entry)).ReadObject(reader);
        }
    }
    static void Write(StreamWriter writer,object record){writer.WriteLine(Json.Serialize(record));}
    public static int Main(string[] args) {
        if(args.Length!=2){Console.Error.WriteLine("Export-Legacy <C# words.sqlite> <output.ndjson>");return 1;}
        string source=Path.GetFullPath(args[0]),output=Path.GetFullPath(args[1]),temp=output+".tmp-"+Guid.NewGuid().ToString("N");
        try {
            if(!File.Exists(source))throw new FileNotFoundException("原 SQLite 文件不存在。");
            if(File.Exists(output))throw new IOException("目标文件已经存在，请使用一个新文件名。");
            Directory.CreateDirectory(Path.GetDirectoryName(output));int total=0;
            using(var db=new Sqlite(source,true)) {
                db.Execute("BEGIN");
                if(db.Text("SELECT value FROM metadata WHERE key='schema'")!="1")throw new InvalidDataException("不支持的原数据库版本。");
                if(db.Text("SELECT value FROM metadata WHERE key='payload_format'")!=null)throw new InvalidDataException("该文件不是 C# 旧格式数据库。");
                string username=db.Text("SELECT value FROM metadata WHERE key='username'");
                if(string.IsNullOrEmpty(username))throw new InvalidDataException("原数据库缺少 username。");
                using(var writer=new StreamWriter(temp,false,new UTF8Encoding(false))) {
                    Write(writer,new {format="mydict-portable-v1",username=username});
                    using(var row=db.Prepare("SELECT s.payload,w.favorite,w.favorite_at FROM sources s JOIN words w ON w.id=s.word_id ORDER BY s.source_key"))
                        while(row.Read()) { Write(writer,new {type="entry",entry=Decode(row.Blob(0)),favorite=row.Long(1)!=0,favoriteAt=row.Text(2)});++total; }
                    if(HasTable(db,"favorite_events"))using(var row=db.Prepare("SELECT normalized,pair,favorite,updated_at FROM favorite_events ORDER BY normalized,pair"))
                        while(row.Read())Write(writer,new {type="favorite",word=row.Text(0),pair=row.Text(1),favorite=row.Long(2)!=0,updatedAt=row.Text(3)});
                    if(HasTable(db,"deleted_words"))using(var row=db.Prepare("SELECT normalized,pair,deleted_ticks FROM deleted_words ORDER BY normalized,pair"))
                        while(row.Read())Write(writer,new {type="deleted",word=row.Text(0),pair=row.Text(1),ticks=row.Long(2).ToString(System.Globalization.CultureInfo.InvariantCulture)});
                    if(HasTable(db,"history"))using(var row=db.Prepare("SELECT position,source_key FROM history ORDER BY position"))
                        while(row.Read())Write(writer,new {type="history",position=row.Long(0),sourceKey=row.Text(1)});
                    long cursor;long.TryParse(db.Text("SELECT value FROM metadata WHERE key='cursor'"),out cursor);
                    Write(writer,new {type="cursor",position=cursor});
                    if(HasTable(db,"import_errors"))using(var row=db.Prepare("SELECT word,error,at FROM import_errors ORDER BY id"))
                        while(row.Read())Write(writer,new {type="importError",word=row.Text(0),error=row.Text(1),at=row.Text(2)});
                }
                db.Execute("COMMIT");
            }
            File.Move(temp,output);Console.WriteLine("导出完成："+total+" 个来源结果；原 SQLite 未修改。");return 0;
        } catch(Exception e) { Console.Error.WriteLine(e.Message);if(File.Exists(temp))File.Delete(temp);return 1; }
    }
}
