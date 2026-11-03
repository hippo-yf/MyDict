using System;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using MyDict;
class FontTests
{
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static string FileFor(string name,FontStyle style){GlyphTypeface glyph;Check(new Typeface(AppFonts.Resolve(name),style,FontWeights.Normal,FontStretches.Normal).TryGetGlyphTypeface(out glyph),"Typeface unavailable: "+name);return glyph.FontUri.LocalPath;}
 [STAThread]static int Main(){try{
  var names=AppFonts.FamilyNames.ToList();Console.WriteLine("Local font families: "+string.Join(", ",names));
  string bundled=names.FirstOrDefault(n=>n.IndexOf("Caskaydia",StringComparison.OrdinalIgnoreCase)>=0);Check(bundled!=null,"Bundled font not discovered");
  Check(Path.GetFullPath(FileFor(bundled,FontStyles.Normal)).StartsWith(Path.GetFullPath(AppFonts.DirectoryPath),StringComparison.OrdinalIgnoreCase),"Bundled font resolved from system instead");
  Check(Path.GetFullPath(FileFor("Arial",FontStyles.Normal)).IndexOf(AppFonts.DirectoryPath,StringComparison.OrdinalIgnoreCase)<0,"System fallback did not work");
  if(Directory.Exists(Path.Combine(AppFonts.DirectoryPath,"local")))Check(FileFor("Georgia",FontStyles.Italic).StartsWith(AppFonts.DirectoryPath,StringComparison.OrdinalIgnoreCase),"Local Georgia italic did not override system font");
  Console.WriteLine("PASS: bundled family discovery, font-file priority, italic local face, missing local font system fallback");return 0;
 }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}}
}
