using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Documents;
namespace MyDict
{
 public class GrammarSegment { public string Text {get;set;} public string Role {get;set;} public string Pos {get;set;} }
 public class UsageExample
 {
  public string Text {get;set;} public string Chinese {get;set;} public string GrammarNote {get;set;}
  public List<GrammarSegment> Segments {get;set;} = new List<GrammarSegment>();
 }
 public static class SentenceStyles
 {
  private static readonly string[] Roles={"subject","predicate","object","complement","adverbial","modifier","connector","other"};
  public static bool Valid(string text,IEnumerable<GrammarSegment> segments)
  {
   return segments!=null&&segments.Any()&&segments.All(s=>s!=null&&s.Text!=null&&Roles.Contains(s.Role??"other"))&&string.Concat(segments.Select(s=>s.Text))==text;
  }
  public static void Append(Paragraph paragraph,string text,IEnumerable<GrammarSegment> segments)
  {
   if(!Valid(text,segments)){EntryDocument.AddWords(paragraph.Inlines,text,paragraph.Foreground);return;}
   foreach(var segment in segments)
   {
    string role=segment.Role??"other";
    var span=new Span { Foreground=ThemeStyles.Brush("brush.grammar."+role),FontWeight=ThemeStyles.Weight(role=="predicate"?"weight.grammar.predicate":role=="subject"||role=="object"?"weight.grammar.core":"weight.doc.normal"),FontStyle=ThemeStyles.FontStyle(role=="adverbial"||role=="modifier"||segment.Pos=="adjective"||segment.Pos=="adverb"?"fontstyle.doc.italic":"fontstyle.doc.normal") };
    EntryDocument.AddWords(span.Inlines,segment.Text,span.Foreground,true);paragraph.Inlines.Add(span);
   }
  }
  public static Paragraph Legend()
  {
   var paragraph=new Paragraph { FontFamily=ThemeStyles.Font("font.doc.body"),FontSize=ThemeStyles.Number("number.grammar.legend_size",11)*ThemeStyles.Number("number.doc.scale",1),Margin=ThemeStyles.Thickness("thickness.grammar.legend_margin") };
   var labels=new Dictionary<string,string>{{"subject","主语"},{"predicate","谓语"},{"object","宾语"},{"complement","表语/补语"},{"adverbial","状语"},{"modifier","修饰语"}};
   foreach(var label in labels)paragraph.Inlines.Add(new Run(label.Value+"  "){Foreground=ThemeStyles.Brush("brush.grammar."+label.Key),FontWeight=ThemeStyles.Weight(label.Key=="predicate"?"weight.grammar.predicate":"weight.doc.normal")});
   return paragraph;
  }
 }
}
