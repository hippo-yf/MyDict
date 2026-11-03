using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Media;
namespace MyDict
{
 public static class AppFonts
 {
  public static string DirectoryPath {get{return Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"fonts");}}
  private static readonly object gate=new object();
  private static Dictionary<string,FontFamily> local;
  private static Dictionary<string,FontFamily> Catalog()
  {
   lock(gate){if(local!=null)return local;var result=new Dictionary<string,FontFamily>(StringComparer.OrdinalIgnoreCase);
    if(Directory.Exists(DirectoryPath)){
     foreach(string directory in new[]{DirectoryPath}.Concat(Directory.EnumerateDirectories(DirectoryPath,"*",SearchOption.AllDirectories))){
      try {foreach(var family in Fonts.GetFontFamilies(new Uri(Path.GetFullPath(directory)+Path.DirectorySeparatorChar),"./")){
       foreach(string name in family.FamilyNames.Values)if(!result.ContainsKey(name))result[name]=family;
      }}catch(Exception ex) when(ex is IOException||ex is UnauthorizedAccessException||ex is ArgumentException||ex is NotSupportedException){System.Diagnostics.Trace.WriteLine("字体目录不可读："+directory+" "+ex.Message);}
     }
    }
    local=result;return local;
   }
  }
  public static FontFamily Resolve(string name)
  {
   if(string.IsNullOrWhiteSpace(name))throw new ArgumentException("字体名称不可为空。");
   FontFamily family;if(Catalog().TryGetValue(name.Trim(),out family))return family;
   return new FontFamily(name);
  }
  public static IEnumerable<string> FamilyNames {get{return Catalog().Keys.OrderBy(x=>x,StringComparer.OrdinalIgnoreCase).ToList();}}
 }
}
