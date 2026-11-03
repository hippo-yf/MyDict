using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Media;
namespace MyDict
{
 public static class AppFonts
 {
  public static string DirectoryPath {get{return Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"fonts");}}
  private static readonly object gate=new object();
  private sealed class Face {public string Family;public HashSet<string> Names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);}
  private sealed class FontFile {public string Path,Stamp;public List<Face> Faces=new List<Face>();}
  private static Dictionary<string,FontFile> files=new Dictionary<string,FontFile>(StringComparer.OrdinalIgnoreCase);
  private static Dictionary<string,FontFamily> resolved=new Dictionary<string,FontFamily>(StringComparer.Ordinal);
  private static readonly HashSet<string> requested=new HashSet<string>(StringComparer.OrdinalIgnoreCase),loaded=new HashSet<string>(StringComparer.OrdinalIgnoreCase),collections=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  private static long metadataBytes;
  // Diagnostics track requested sources made available to WPF, not all glyph
  // objects the OS may retain. Metadata-only indexing activates no fonts.
  public static IEnumerable<string> RequestedFamilies {get{lock(gate)return requested.OrderBy(s=>s,StringComparer.OrdinalIgnoreCase).ToArray();}}
  public static IEnumerable<string> LoadedFiles {get{lock(gate)return loaded.OrderBy(s=>s,StringComparer.OrdinalIgnoreCase).ToArray();}}
  public static IEnumerable<string> PrivateCollectionDirectories {get{lock(gate)return collections.ToArray();}}
  public static long MetadataBytesRead {get{lock(gate)return metadataBytes;}}
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
  [return:MarshalAs(UnmanagedType.Bool)]static extern bool CreateHardLink(string newFile,string existingFile,IntPtr security);
  private static IEnumerable<string> FontFiles(string directory)
  {
   string[] entries;try{entries=Directory.GetFileSystemEntries(directory);}catch(Exception ex)when(ex is IOException||ex is UnauthorizedAccessException){yield break;}
   foreach(string entry in entries.OrderBy(p=>p,StringComparer.OrdinalIgnoreCase)){
    FileAttributes attributes;try{attributes=File.GetAttributes(entry);}catch(Exception ex)when(ex is IOException||ex is UnauthorizedAccessException){continue;}
    if((attributes&FileAttributes.Directory)!=0){
     if((attributes&FileAttributes.ReparsePoint)==0)foreach(string nested in FontFiles(entry))yield return nested;
    }else{
     string ext=Path.GetExtension(entry);if(ext.Equals(".ttf",StringComparison.OrdinalIgnoreCase)||ext.Equals(".otf",StringComparison.OrdinalIgnoreCase)||ext.Equals(".ttc",StringComparison.OrdinalIgnoreCase)||ext.Equals(".otc",StringComparison.OrdinalIgnoreCase))yield return Path.GetFullPath(entry);
    }
   }
  }
  private static void RefreshIndex()
  {
   var current=new Dictionary<string,FontFile>(StringComparer.OrdinalIgnoreCase);
   if(Directory.Exists(DirectoryPath))foreach(string path in FontFiles(DirectoryPath)){
    try{
     var info=new FileInfo(path);string stamp=info.Length+":"+info.LastWriteTimeUtc.Ticks;FontFile record;
     if(!files.TryGetValue(path,out record)||record.Stamp!=stamp){record=new FontFile{Path=path,Stamp=stamp};record.Faces=ReadNames(path);}
     current[path]=record;
    }catch(Exception ex)when(ex is IOException||ex is InvalidDataException||ex is UnauthorizedAccessException||ex is ArgumentException||ex is NotSupportedException||ex is OverflowException){System.Diagnostics.Trace.WriteLine("字体元数据不可读："+Path.GetFileName(path));}
   }
   files=current;
  }
  private static byte[] ReadAt(FileStream stream,long offset,int length,ref long budget)
  {
   if(offset<0||length<0||offset>stream.Length-length)throw new InvalidDataException("字体表边界无效。");
   if(length>budget)throw new InvalidDataException("单个字体的元数据超过 8 MiB 限制。");budget-=length;
   var bytes=new byte[length];stream.Position=offset;int got=0;while(got<length){int n=stream.Read(bytes,got,length-got);if(n==0)throw new EndOfStreamException();got+=n;}metadataBytes+=length;return bytes;
  }
  private static ushort U16(byte[] b,int at){return (ushort)((b[at]<<8)|b[at+1]);}
  private static uint U32(byte[] b,int at){return ((uint)b[at]<<24)|((uint)b[at+1]<<16)|((uint)b[at+2]<<8)|b[at+3];}
  private static List<Face> ReadNames(string path)
  {
   // SFNT headers and the OpenType 'name' table only: no glyph or WPF catalog.
   var faces=new List<Face>();
   using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
    long budget=8L*1024*1024,decodedNameBytes=0;var header=ReadAt(stream,0,12,ref budget);var offsets=new List<long>();
    var nameTables=new HashSet<string>(StringComparer.Ordinal);
    if(U32(header,0)==0x74746366){
     uint count=U32(header,8);if(count==0||count>1024)throw new InvalidDataException("TTC face 数量无效。");
     var table=ReadAt(stream,12,checked((int)count*4),ref budget);for(int i=0;i<count;i++)offsets.Add(U32(table,i*4));
    }else offsets.Add(0);
    foreach(long offset in offsets.Distinct()){
     var faceHeader=ReadAt(stream,offset,12,ref budget);uint scaler=U32(faceHeader,0);
     if(scaler!=0x00010000&&scaler!=0x4f54544f&&scaler!=0x74727565&&scaler!=0x74797031)continue;
     int count=U16(faceHeader,4);if(count<=0||count>4096)continue;
     var directory=ReadAt(stream,offset+12,count*16,ref budget);
     for(int i=0;i<count;i++)if(U32(directory,i*16)==0x6e616d65){
      uint tableOffset=U32(directory,i*16+8),length=U32(directory,i*16+12);if(length<6||length>4*1024*1024)break;
      string tableKey=tableOffset+":"+length;if(!nameTables.Add(tableKey))break;
      var names=ReadAt(stream,tableOffset,(int)length,ref budget);
      int format=U16(names,0),records=U16(names,2),storage=U16(names,4),recordsEnd=6+records*12,languageTags=0;
      if(format>1||recordsEnd>names.Length||storage>names.Length||storage<recordsEnd)break;
      if(format==1){if(recordsEnd>names.Length-2)break;languageTags=U16(names,recordsEnd);if(languageTags>(names.Length-recordsEnd-2)/4||storage<recordsEnd+2+languageTags*4)break;}
      var face=new Face();string preferred=null;int best=int.MinValue;
      for(int n=0;n<records;n++){
       int at=6+n*12,platform=U16(names,at),encoding=U16(names,at+2),language=U16(names,at+4),id=U16(names,at+6),size=U16(names,at+8),start=storage+U16(names,at+10);
       if((id!=1&&id!=16&&id!=21)||size==0||start>names.Length-size||(language>=0x8000&&(format!=1||language-0x8000>=languageTags)))continue;
       decodedNameBytes+=size;if(decodedNameBytes>8L*1024*1024)throw new InvalidDataException("字体名称元数据过大。");
       string name;
       if(platform==0||platform==3){if((size&1)!=0)continue;name=Encoding.BigEndianUnicode.GetString(names,start,size);}
       else if(platform==1&&encoding==0)name=Encoding.GetEncoding(10000).GetString(names,start,size);
       else continue;
       name=name.Trim('\0',' ','\r','\n');if(name.Length==0||name.Any(c=>char.IsControl(c)))continue;
       face.Names.Add(name);
       // ID 1 preserves WPF four-style linking. ID 16/21 and all localized
       // names remain searchable aliases (e.g. long Nerd Font name / NFP).
       int score=(id==1?1000:id==21?100:0)+(platform==3?30:platform==0?20:0)+(language==0x409?100:language==0?10:0);
       if(score>best){best=score;preferred=name;}
      }
      if(face.Names.Count>0){face.Family=preferred;faces.Add(face);}break;
     }
    }
   }
   return faces;
  }
  private static string Hash(string text){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(text))).Replace("-","").ToLowerInvariant();}
  private static string PrepareCollection(List<FontFile> required)
  {
   string signature=Hash(string.Join("\n",required.Select(f=>f.Path+"|"+f.Stamp)));
   string cache=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,".font-cache",signature);
   try{Directory.CreateDirectory(cache);}catch(Exception ex)when(ex is IOException||ex is UnauthorizedAccessException){cache=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"MyDict","font-cache",signature);Directory.CreateDirectory(cache);}
   for(int i=0;i<required.Count;i++){
    string target=Path.Combine(cache,i.ToString("D4")+"-"+Path.GetFileName(required[i].Path));
    if(!File.Exists(target)){
     bool linked=false;try{linked=CreateHardLink(target,required[i].Path,IntPtr.Zero);}catch(Exception ex)when(ex is DllNotFoundException||ex is EntryPointNotFoundException){ }
     if(!linked&&!File.Exists(target)){
      string temporary=target+"."+Guid.NewGuid().ToString("N")+".tmp";
      try{File.Copy(required[i].Path,temporary);try{File.Move(temporary,target);}catch(IOException){if(!File.Exists(target))throw;}}finally{if(File.Exists(temporary))File.Delete(temporary);}
     }
    }
    loaded.Add(required[i].Path);
   }
   collections.Add(cache);return cache;
  }
  public static FontFamily Resolve(string name)
  {
   if(string.IsNullOrWhiteSpace(name))throw new ArgumentException("字体名称不可为空。");name=name.Trim();
   lock(gate){
    requested.Add(name);RefreshIndex();
    var matching=files.Values.SelectMany(f=>f.Faces).Where(f=>f.Names.Contains(name)).ToList();
    if(matching.Count==0){FontFamily fallback;if(!resolved.TryGetValue("system:"+name,out fallback)){fallback=new FontFamily(name);resolved["system:"+name]=fallback;}return fallback;}
    var aliases=new HashSet<string>(matching.SelectMany(f=>f.Names),StringComparer.OrdinalIgnoreCase);bool changed;
    do{changed=false;foreach(var face in files.Values.SelectMany(f=>f.Faces))if(face.Names.Overlaps(aliases))foreach(string alias in face.Names)changed|=aliases.Add(alias);}while(changed);
    var required=files.Values.Where(f=>f.Faces.Any(face=>face.Names.Overlaps(aliases))).OrderBy(f=>f.Path,StringComparer.OrdinalIgnoreCase).ToList();
    string family=matching.OrderBy(f=>f.Family.Equals(name,StringComparison.OrdinalIgnoreCase)?0:1).ThenBy(f=>f.Family,StringComparer.OrdinalIgnoreCase).First().Family;
    string cache=PrepareCollection(required),key=cache+"#"+family;FontFamily result;
    if(!resolved.TryGetValue(key,out result)){
     // Microsoft's documented directory/#family URI is scoped to this family,
     // never the entire fonts directory (which could load unrelated fonts).
     result=new FontFamily(new Uri(Path.GetFullPath(cache)+Path.DirectorySeparatorChar),"./#"+family.Replace("%","%25").Replace("#","%23").Replace(",","%2C"));resolved[key]=result;
    }
    return result;
   }
  }
  public static IEnumerable<string> FamilyNames {get{lock(gate){RefreshIndex();return files.Values.SelectMany(f=>f.Faces).SelectMany(f=>f.Names).Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(x=>x,StringComparer.OrdinalIgnoreCase).ToArray();}}}
 }
}
