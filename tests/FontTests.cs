using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MyDict;
class FontTests
{
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]
 [return:MarshalAs(UnmanagedType.Bool)]static extern bool CreateHardLink(string target,string source,IntPtr security);
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static string FileFor(string name,FontStyle style,FontWeight weight){GlyphTypeface glyph;Check(new Typeface(AppFonts.Resolve(name),style,weight,FontStretches.Normal).TryGetGlyphTypeface(out glyph),"Typeface unavailable: "+name);return glyph.FontUri.LocalPath;}
 static bool PrivateFile(string path){return AppFonts.PrivateCollectionDirectories.Any(directory=>Path.GetFullPath(path).StartsWith(Path.GetFullPath(directory)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase));}
 static void U16(byte[] bytes,int at,int value){bytes[at]=(byte)(value>>8);bytes[at+1]=(byte)value;}
 static void U32(byte[] bytes,int at,int value){bytes[at]=(byte)(value>>24);bytes[at+1]=(byte)(value>>16);bytes[at+2]=(byte)(value>>8);bytes[at+3]=(byte)value;}
 static byte[] Names(string english,string chinese){
  var a=Encoding.BigEndianUnicode.GetBytes(english);var b=Encoding.BigEndianUnicode.GetBytes(chinese);var bytes=new byte[30+a.Length+b.Length];
  U16(bytes,2,2);U16(bytes,4,30);
  for(int i=0;i<2;i++){int at=6+i*12;U16(bytes,at,3);U16(bytes,at+2,1);U16(bytes,at+4,i==0?0x409:0x804);U16(bytes,at+6,1);U16(bytes,at+8,i==0?a.Length:b.Length);U16(bytes,at+10,i==0?0:a.Length);}
  Buffer.BlockCopy(a,0,bytes,30,a.Length);Buffer.BlockCopy(b,0,bytes,30+a.Length,b.Length);return bytes;
 }
 static void Sfnt(byte[] bytes,int at,int nameOffset,int nameLength,bool otf=false){
  U32(bytes,at,otf?0x4f54544f:0x10000);U16(bytes,at+4,1);U32(bytes,at+12,0x6e616d65);U32(bytes,at+20,nameOffset);U32(bytes,at+24,nameLength);
 }
 static void MetadataFixtures(string folder){
  // These contain valid headers/name metadata but no glyph tables. Discovering
  // them must never hand them to WPF's font engine merely to list family names.
  Directory.CreateDirectory(folder);var a=Names("MetadataOnlyTTC_A","仅元数据字体甲");var b=Names("MetadataOnlyTTC_B","仅元数据字体乙");
  int secondName=76+((a.Length+3)&~3);var ttc=new byte[secondName+b.Length];U32(ttc,0,0x74746366);U32(ttc,4,0x10000);U32(ttc,8,2);U32(ttc,12,20);U32(ttc,16,48);
  Sfnt(ttc,20,76,a.Length);Sfnt(ttc,48,secondName,b.Length);Buffer.BlockCopy(a,0,ttc,76,a.Length);Buffer.BlockCopy(b,0,ttc,secondName,b.Length);File.WriteAllBytes(Path.Combine(folder,"metadata-only.ttc"),ttc);
  var n=Names("MetadataOnlyOTF","仅元数据OpenType");var otf=new byte[28+n.Length];Sfnt(otf,0,28,n.Length,true);Buffer.BlockCopy(n,0,otf,28,n.Length);File.WriteAllBytes(Path.Combine(folder,"metadata-only.otf"),otf);
  File.WriteAllBytes(Path.Combine(folder,"broken.ttf"),new byte[3]);
 }
 static int Child(){
  var app=new Application();app.ShutdownMode=ShutdownMode.OnExplicitShutdown;
  try{
   MetadataFixtures(Path.Combine(AppFonts.DirectoryPath,"metadata-tests"));
   var names=AppFonts.FamilyNames.ToList();
   Check(names.Contains("MetadataOnlyTTC_A")&&names.Contains("MetadataOnlyTTC_B")&&names.Contains("仅元数据字体甲")&&names.Contains("MetadataOnlyOTF"),"SFNT/OTF/TTC or localized metadata indexing failed");
   Check(AppFonts.LoadedFiles.Count()==0&&AppFonts.RequestedFamilies.Count()==0,"Listing names initialized fonts");
   long bytes=AppFonts.MetadataBytesRead;AppFonts.FamilyNames.ToArray();Check(AppFonts.MetadataBytesRead==bytes,"Unchanged metadata was reread");
   long fontBytes=Directory.GetFiles(AppFonts.DirectoryPath,"*",SearchOption.AllDirectories).Sum(f=>new FileInfo(f).Length);Check(bytes<fontBytes/4,"Metadata index read font bodies");
   const string longName="CaskaydiaCove Nerd Font Propo",shortName="CaskaydiaCove NFP";
   Check(names.Contains(longName)&&names.Contains(shortName),"Long and abbreviated family aliases missing");
   var styles=ThemeStyles.DefaultStyles();
   styles["font.mainwindow.window.1.fontfamily"]="Georgia";styles["font.doc.body"]="Georgia";styles["font.doc.serif"]="Georgia";
   styles["font.about.body"]="MetadataOnlyTTC_A";styles["font.settingswindow.window.1.fontfamily"]=longName;
   var options=new AppSettings{username="font-test",FontName=shortName,DefinitionFontName=shortName,CacheSync=new CacheSyncOptions{Enabled=false},Styles=styles};
   SettingsStore.Save(options);ThemeStyles.ValidateStyles(styles);
   Check(!AppFonts.RequestedFamilies.Any(),"Validation resolved unused or legacy font names");
   ThemeStyles.EnsureLoaded();Check(!AppFonts.RequestedFamilies.Any(),"Applying default/overridden font resources eagerly resolved families");
   var body=ThemeStyles.Font("font.doc.body");Check(AppFonts.RequestedFamilies.SequenceEqual(new[]{"Georgia"}),"Document body pulled in unrelated fonts");
   var text=new TextBlock();text.SetResourceReference(TextBlock.FontFamilyProperty,"font.mainwindow.window.1.fontfamily");
   Check(text.FontFamily==body,"Dynamic font resource did not resolve to the requested family");
   Check(!AppFonts.RequestedFamilies.Contains(longName)&&!AppFonts.RequestedFamilies.Contains(shortName)&&!AppFonts.RequestedFamilies.Contains("Microsoft YaHei UI"),"Unused or replaced default font was resolved");
   Check(!AppFonts.LoadedFiles.Any(f=>Path.GetFileName(f).StartsWith("Caskaydia",StringComparison.OrdinalIgnoreCase)),"Unused Nerd Font was activated");
   if(File.Exists(Path.Combine(AppFonts.DirectoryPath,"local","georgiai.ttf")))Check(PrivateFile(FileFor("Georgia",FontStyles.Italic,FontWeights.Normal)),"Local italic face did not override system");
   // Only an explicit new request activates the Nerd family, including linked
   // bold and italic files. Its private collection contains no other families.
   Check(PrivateFile(FileFor(longName,FontStyles.Normal,FontWeights.Normal)),"Long family alias did not use local font");
   Check(FileFor(shortName,FontStyles.Normal,FontWeights.Bold).EndsWith("-Bold.ttf",StringComparison.OrdinalIgnoreCase),"Bold face was synthesized instead of linked");
   Check(FileFor(shortName,FontStyles.Italic,FontWeights.Normal).EndsWith("-Italic.ttf",StringComparison.OrdinalIgnoreCase),"Italic face was not linked");
   Check(FileFor(longName,FontStyles.Italic,FontWeights.Bold).EndsWith("-BoldItalic.ttf",StringComparison.OrdinalIgnoreCase),"Bold italic face was not linked");
   Check(AppFonts.LoadedFiles.Count(f=>Path.GetFileName(f).StartsWith("Caskaydia",StringComparison.OrdinalIgnoreCase))==4,"Wrong number of Nerd family files activated");
   foreach(string directory in AppFonts.PrivateCollectionDirectories){var linked=Directory.GetFiles(directory);if(linked.Any(f=>f.IndexOf("Caskaydia",StringComparison.OrdinalIgnoreCase)>=0))Check(linked.All(f=>f.IndexOf("Caskaydia",StringComparison.OrdinalIgnoreCase)>=0),"WPF collection references the whole fonts directory");}
   Check(!PrivateFile(FileFor("Arial",FontStyles.Normal,FontWeights.Normal)),"Missing local font did not fall back to system");
   // Add a genuine user font after the initial index and system fallback. A new
   // settings application must discover it without restarting or preloading it.
   string arial=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Fonts),"arial.ttf");
   if(File.Exists(arial)){
    string user=Path.Combine(AppFonts.DirectoryPath,"user-added");Directory.CreateDirectory(user);File.Copy(arial,Path.Combine(user,"arial.ttf"));
    int loaded=AppFonts.LoadedFiles.Count();Check(AppFonts.FamilyNames.Contains("Arial")&&AppFonts.LoadedFiles.Count()==loaded,"Adding metadata prematurely activated the new font");
    styles["font.doc.body"]="Arial";ThemeStyles.ApplyStyles(styles);Check(AppFonts.LoadedFiles.Count()==loaded,"Hot reload activated unused font resources");
    var replacement=ThemeStyles.Font("font.doc.body");Check(PrivateFile(FileFor("Arial",FontStyles.Normal,FontWeights.Normal)),"New local font was not discovered after hot reload");
   }
   Check(!AppFonts.RequestedFamilies.Any(f=>f.StartsWith("MetadataOnly",StringComparison.Ordinal)),"Unused metadata-only font was handed to WPF");
   Console.WriteLine("PASS: metadata-only SFNT/OTF/TTC and localized aliases; lazy validation/resources/final overrides; local/system priority; real regular/bold/italic/bold-italic; new-font hot reload; private requested-only collections");
   Console.WriteLine("Metadata bytes read="+AppFonts.MetadataBytesRead+"; activated families="+string.Join(", ",AppFonts.RequestedFamilies));return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}finally{app.Shutdown();}
 }
 [STAThread]static int Main(string[] args){
  if(args.Length>0&&args[0]=="--font-child")return Child();
  string folder=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"font-test-"+Guid.NewGuid().ToString("N"));
  try{
   Directory.CreateDirectory(folder);string self=Assembly.GetExecutingAssembly().Location;File.Copy(self,Path.Combine(folder,"FontTests.exe"));File.Copy(typeof(AppFonts).Assembly.Location,Path.Combine(folder,"MyDict.exe"));
   var required=new HashSet<string>(new[]{"CaskaydiaCoveNerdFontPropo-Regular.ttf","CaskaydiaCoveNerdFontPropo-Bold.ttf","CaskaydiaCoveNerdFontPropo-Italic.ttf","CaskaydiaCoveNerdFontPropo-BoldItalic.ttf","georgia.ttf","georgiai.ttf","msyh.ttc","segoeui.ttf"},StringComparer.OrdinalIgnoreCase);
   foreach(string source in Directory.GetFiles(AppFonts.DirectoryPath,"*",SearchOption.AllDirectories).Where(f=>required.Contains(Path.GetFileName(f)))){
    string relative=source.Substring(AppFonts.DirectoryPath.Length).TrimStart(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar),target=Path.Combine(folder,"fonts",relative);Directory.CreateDirectory(Path.GetDirectoryName(target));if(!CreateHardLink(target,source,IntPtr.Zero))File.Copy(source,target);
   }
   using(var process=Process.Start(new ProcessStartInfo(Path.Combine(folder,"FontTests.exe"),"--font-child"){WorkingDirectory=folder,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8})){
    var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();if(!process.WaitForExit(60000)){process.Kill();throw new Exception("Font tests timed out");}Console.Write(output.Result);Console.Error.Write(error.Result);return process.ExitCode;
   }
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
  finally{try{if(Directory.Exists(folder))Directory.Delete(folder,true);}catch(IOException){ }catch(UnauthorizedAccessException){ }}
 }
}
