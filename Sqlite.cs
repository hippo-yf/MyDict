using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
namespace MyDict
{
 internal sealed class Sqlite:IDisposable
 {
  private IntPtr db;
  private const string Library="winsqlite3.dll";
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_open_v2(byte[] path,out IntPtr db,int flags,IntPtr vfs);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_close_v2(IntPtr db);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_busy_timeout(IntPtr db,int ms);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr sqlite3_errmsg(IntPtr db);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_prepare_v2(IntPtr db,byte[] sql,int count,out IntPtr statement,IntPtr tail);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_step(IntPtr statement);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_finalize(IntPtr statement);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_bind_text(IntPtr statement,int index,byte[] text,int size,IntPtr destructor);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_bind_blob(IntPtr statement,int index,byte[] data,int size,IntPtr destructor);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_bind_int64(IntPtr statement,int index,long value);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_bind_null(IntPtr statement,int index);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern long sqlite3_column_int64(IntPtr statement,int index);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr sqlite3_column_text(IntPtr statement,int index);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr sqlite3_column_blob(IntPtr statement,int index);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_column_bytes(IntPtr statement,int index);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern long sqlite3_changes(IntPtr db);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr sqlite3_backup_init(IntPtr destination,byte[] destName,IntPtr source,byte[] sourceName);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_backup_step(IntPtr backup,int pages);
  [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int sqlite3_backup_finish(IntPtr backup);
  static byte[] Utf8(string value){return Encoding.UTF8.GetBytes(value+"\0");}
  public Sqlite(string path,bool readOnly=false)
  {
   int rc=sqlite3_open_v2(Utf8(path),out db,readOnly?1:2|4,IntPtr.Zero);
   if(rc!=0){string error=Error();Dispose();throw new IOException("SQLite: "+error);}
   sqlite3_busy_timeout(db,10000);
  }
  string Error(){var ptr=sqlite3_errmsg(db);return Marshal.PtrToStringAnsi(ptr);}
  public Statement Prepare(string sql,params object[] parameters)
  {
   IntPtr handle;int rc=sqlite3_prepare_v2(db,Utf8(sql),-1,out handle,IntPtr.Zero);if(rc!=0)throw new IOException("SQLite: "+Error());
   var statement=new Statement(this,handle);
   try{for(int i=0;i<parameters.Length;i++){var value=parameters[i];int code;
    if(value==null)code=sqlite3_bind_null(handle,i+1);
    else if(value is byte[]){var bytes=(byte[])value;code=sqlite3_bind_blob(handle,i+1,bytes,bytes.Length,new IntPtr(-1));}
    else if(value is string){var bytes=Utf8((string)value);code=sqlite3_bind_text(handle,i+1,bytes,bytes.Length-1,new IntPtr(-1));}
    else code=sqlite3_bind_int64(handle,i+1,Convert.ToInt64(value));
    if(code!=0)throw new IOException("SQLite: "+Error());
   }return statement;}catch{statement.Dispose();throw;}
  }
  public void Execute(string sql,params object[] parameters){using(var stmt=Prepare(sql,parameters)){while(stmt.Read()){}}}
  public long Scalar(string sql,params object[] parameters){using(var stmt=Prepare(sql,parameters))return stmt.Read()?stmt.Long(0):0;}
  public string Text(string sql,params object[] parameters){using(var stmt=Prepare(sql,parameters))return stmt.Read()?stmt.Text(0):null;}
  public long Changes {get{return sqlite3_changes(db);}}
  public void BackupTo(string destination)
  {
   using(var target=new Sqlite(destination)){
    var backup=sqlite3_backup_init(target.db,Utf8("main"),db,Utf8("main"));if(backup==IntPtr.Zero)throw new IOException("SQLite backup: "+target.Error());
    try{int rc=sqlite3_backup_step(backup,-1);if(rc!=101)throw new IOException("SQLite backup failed: "+rc);}finally{sqlite3_backup_finish(backup);}
   }
  }
  public void Dispose(){if(db!=IntPtr.Zero){sqlite3_close_v2(db);db=IntPtr.Zero;}}
  internal sealed class Statement:IDisposable
  {
   Sqlite owner;IntPtr handle;
   public Statement(Sqlite db,IntPtr value){owner=db;handle=value;}
   public bool Read(){int rc=sqlite3_step(handle);if(rc==100)return true;if(rc==101)return false;throw new IOException("SQLite: "+owner.Error());}
   public long Long(int index){return sqlite3_column_int64(handle,index);}
   public string Text(int index){int n=sqlite3_column_bytes(handle,index);var bytes=new byte[n];if(n>0)Marshal.Copy(sqlite3_column_text(handle,index),bytes,0,n);return Encoding.UTF8.GetString(bytes);}
   public byte[] Blob(int index){int n=sqlite3_column_bytes(handle,index);var bytes=new byte[n];if(n>0)Marshal.Copy(sqlite3_column_blob(handle,index),bytes,0,n);return bytes;}
   public void Dispose(){if(handle!=IntPtr.Zero){sqlite3_finalize(handle);handle=IntPtr.Zero;}}
  }
 }
}
