using System;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace MyDict
{
    public static class EntryDocument
    {
        public static AppSettings Settings { get; set; } = new AppSettings();
        public static readonly RoutedEvent WordLookupEvent = EventManager.RegisterRoutedEvent("WordLookup", RoutingStrategy.Bubble, typeof(RoutedEventHandler), typeof(EntryDocument));
        public class WordLookupArgs : RoutedEventArgs
        {
            public string Word { get; private set; }
            public WordLookupArgs(string word) : base(WordLookupEvent) { Word=word; }
        }
        private static void AddWords(InlineCollection inlines,string text,Brush foreground)
        {
            text=Translator.CleanText(text); int offset=0;
            foreach(Match match in Regex.Matches(text,@"[A-Za-z]+(?:['’\-][A-Za-z]+)*"))
            {
                if(match.Index>offset) inlines.Add(new Run(text.Substring(offset,match.Index-offset)));
                string word=match.Value;
                var link=new Hyperlink(new Run(word)) { Foreground=foreground, TextDecorations=null, Cursor=System.Windows.Input.Cursors.Hand, ToolTip="查询 "+word };
                link.MouseEnter+=(s,e)=>link.TextDecorations=TextDecorations.Underline;
                link.MouseLeave+=(s,e)=>link.TextDecorations=null;
                link.GotKeyboardFocus+=(s,e)=>link.TextDecorations=TextDecorations.Underline;
                link.LostKeyboardFocus+=(s,e)=>link.TextDecorations=null;
                link.Click+=(s,e)=>
                {
                    DependencyObject parent=link;
                    while(parent!=null && !(parent is RichTextBox))
                    {
                        var content=parent as FrameworkContentElement;
                        parent=content!=null ? content.Parent : System.Windows.Media.VisualTreeHelper.GetParent(parent);
                    }
                    var owner=parent as RichTextBox;
                    if(owner!=null)owner.RaiseEvent(new WordLookupArgs(word.Replace('’','\'')));
                    e.Handled=true;
                };
                inlines.Add(link); offset=match.Index+match.Length;
            }
            if(offset<text.Length)inlines.Add(new Run(text.Substring(offset)));
        }
        private static Paragraph WordParagraph(string text,double size,string color)
        {
            var p=Paragraph("",size,color);AddWords(p.Inlines,text,p.Foreground);return p;
        }
        private static double Size(double size) { return ThemeStyles.Number("number.doc.size."+size.ToString(System.Globalization.CultureInfo.InvariantCulture),size)*ThemeStyles.Number("number.doc.scale",1); }
        private static Brush Color(string hex) { return ThemeStyles.BrushFor(hex); }
        private static FontFamily Serif { get { return ThemeStyles.Font("font.doc.serif"); } }
        private static Paragraph Paragraph(string text, double size = 16, string color = "#363636")
        {
            return new Paragraph(new Run(Translator.CleanText(text))) { FontSize = Size(size), Foreground = Color(color), Margin = ThemeStyles.Thickness("thickness.doc.spacing.1"), LineHeight = Size(size) * ThemeStyles.Number("number.doc.line_spacing",1.55) };
        }
        private static Paragraph Heading(string text)
        {
            var p = Paragraph(text, 16, "#0874C4"); p.FontWeight=ThemeStyles.Weight("weight.doc.semibold"); p.Margin=ThemeStyles.Thickness("thickness.doc.spacing.2"); p.BorderBrush=Color("#B5D9F5"); p.BorderThickness=ThemeStyles.Thickness("thickness.doc.spacing.3"); p.Padding=ThemeStyles.Thickness("thickness.doc.spacing.4"); return p;
        }
        private static void Link(FlowDocument doc, string label, string url)
        {
            Uri uri;
            if (!Uri.TryCreate(url,UriKind.Absolute,out uri) || (uri.Scheme!="https" && uri.Scheme!="http")) return;
            var p=Paragraph("",11,"#777777"); p.Margin=ThemeStyles.Thickness("thickness.doc.spacing.5");
            var link=new Hyperlink(new Run(label)) { NavigateUri=uri, Foreground=Color("#0874C4") };
            link.RequestNavigate+=(sender,args)=> { try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(args.Uri.AbsoluteUri){UseShellExecute=true}); } catch {} args.Handled=true; };
            p.Inlines.Add(link); doc.Blocks.Add(p);
        }
        public static FlowDocument Message(string title,string subtitle)
        {
            var doc=NewDocument();
            var p=Paragraph(title,30,"#0874C4"); p.FontFamily=Serif; p.FontWeight=ThemeStyles.Weight("weight.doc.bold"); doc.Blocks.Add(p);
            doc.Blocks.Add(Paragraph(subtitle,15,"#777777")); return doc;
        }
        private static FlowDocument NewDocument() {
            ThemeStyles.EnsureLoaded();
            return new FlowDocument { FontFamily=ThemeStyles.Font("font.doc.body"), FontSize=ThemeStyles.Number("number.doc.base_size",16), Foreground=Color("#363636"), PagePadding=ThemeStyles.Thickness("thickness.doc.spacing.6") };
        }
        private static string EnglishPart(string part)
        {
            if(part.StartsWith("adj.")) return "Adjective";
            if(part.StartsWith("adv.")) return "Adverb";
            if(part.StartsWith("n.")) return "Noun";
            if(part.StartsWith("v.")) return "Verb";
            if(part.StartsWith("pron.")) return "Pronoun";
            if(part.StartsWith("prep.")) return "Preposition";
            return part;
        }
        private static Style PartButtonStyle()
        {
            var border=new FrameworkElementFactory(typeof(Border));
            border.SetValue(Border.CornerRadiusProperty,ThemeStyles.Corner("corner.doc.button"));
            border.SetBinding(Border.BackgroundProperty,new System.Windows.Data.Binding("Background"){RelativeSource=new System.Windows.Data.RelativeSource(System.Windows.Data.RelativeSourceMode.TemplatedParent)});
            border.SetBinding(Border.PaddingProperty,new System.Windows.Data.Binding("Padding"){RelativeSource=new System.Windows.Data.RelativeSource(System.Windows.Data.RelativeSourceMode.TemplatedParent)});
            var presenter=new FrameworkElementFactory(typeof(ContentPresenter));
            presenter.SetValue(FrameworkElement.HorizontalAlignmentProperty,HorizontalAlignment.Center);
            presenter.SetValue(FrameworkElement.VerticalAlignmentProperty,VerticalAlignment.Center);
            presenter.SetValue(ContentPresenter.ContentSourceProperty,"Content");
            border.AppendChild(presenter);
            var template=new ControlTemplate(typeof(Button)){VisualTree=border};
            var hover=new Trigger{Property=UIElement.IsMouseOverProperty,Value=true};hover.Setters.Add(new Setter(UIElement.OpacityProperty,ThemeStyles.Number("number.doc.hover_opacity",0.88)));template.Triggers.Add(hover);
            var style=new Style(typeof(Button));style.Setters.Add(new Setter(Control.TemplateProperty,template));
            style.Setters.Add(new Setter(Control.FocusVisualStyleProperty,null));
            return style;
        }
        public static FlowDocument Build(Entry entry) { return Build(entry,null); }
        private static FlowDocument Build(Entry entry,string selectedPart)
        {
            var doc=NewDocument(); doc.Tag=selectedPart; var record=entry.Record;
            var groups=record?.Meanings.GroupBy(m=>m.PartOfSpeech).ToList();
            var header=new Section { BorderBrush=Color("#A7D6A4"), BorderThickness=ThemeStyles.Thickness("thickness.doc.spacing.7"), Padding=ThemeStyles.Thickness("thickness.doc.spacing.8"), Margin=ThemeStyles.Thickness("thickness.doc.spacing.9") };
            if(groups!=null && groups.Count>0)
            {
                var tabs=new Paragraph { Background=Color("#E2F1E0"), Padding=ThemeStyles.Thickness("thickness.doc.spacing.10"), Margin=ThemeStyles.Thickness("thickness.doc.spacing.11"), LineHeight=Size(32) };
                foreach(var part in groups.Select(g=>g.Key).Concat(new[]{"All"}))
                {
                    string key=part;
                    bool active=selectedPart==part || (part=="All" && selectedPart==null);
                    var button=new Button { Style=PartButtonStyle(), Content=part=="All" ? "All" : EnglishPart(part), Background=Color(active?"#4E8848":"#A9D6A5"), Foreground=Color(active?"#FFFFFF":"#315E35"), FontSize=Size(14), Height=Size(36), FontWeight=ThemeStyles.Weight("weight.doc.bold"), FontFamily=ThemeStyles.Font("font.doc.tabs"), Padding=ThemeStyles.Thickness("thickness.doc.spacing.12"), Margin=ThemeStyles.Thickness("thickness.doc.spacing.13"), Cursor=System.Windows.Input.Cursors.Hand };
                    button.Click+=(s,e)=> { var owner=doc.Parent as RichTextBox; if(owner!=null) { owner.Document=Build(entry,key=="All"?null:key); owner.ScrollToHome(); } };
                    tabs.Inlines.Add(new InlineUIContainer(button){BaselineAlignment=BaselineAlignment.Center});
                }
                header.Blocks.Add(tabs);
            }
            var title=Paragraph("",34,"#0074CB"); title.FontFamily=Serif; title.FontWeight=ThemeStyles.Weight("weight.doc.bold"); title.Margin=ThemeStyles.Thickness("thickness.doc.spacing.14");
            title.Inlines.Add(new Run(entry.Word));
            if(selectedPart!=null) title.Inlines.Add(new Run("  "+EnglishPart(selectedPart).ToLowerInvariant()){FontSize=Size(21),FontWeight=ThemeStyles.Weight("weight.doc.normal"),FontStyle=ThemeStyles.FontStyle("fontstyle.doc.italic"),Foreground=Color("#D88632")});
            header.Blocks.Add(title);
            if(!string.IsNullOrWhiteSpace(record?.Phonetic))
            {
                var ipa=Paragraph(record.Phonetic,20,"#0874C4"); ipa.FontFamily=ThemeStyles.Font("font.doc.phonetic"); ipa.Margin=ThemeStyles.Thickness("thickness.doc.spacing.15"); header.Blocks.Add(ipa);
            }
            var translation=Paragraph("",19,"#008577"); translation.Margin=ThemeStyles.Thickness("thickness.doc.spacing.16"); translation.LineHeight=double.NaN;
            if(record==null || record.Meanings.Count==0) { translation.Inlines.Add(new Run(Translator.CleanText(entry.Translation)){Background=Color("#D7EAE6")}); header.Blocks.Add(translation); } doc.Blocks.Add(header);
            if(!string.IsNullOrWhiteSpace(entry.DetailNote)) doc.Blocks.Add(Paragraph(entry.DetailNote,12,"#777777"));
            if(record==null) RenderLegacy(doc,entry.Details??"这条收藏尚未包含词典资料，重新查询并收藏即可补充。");
            else
            {
                foreach(var group in groups.Where(g=>selectedPart==null||g.Key==selectedPart))
                {
                    var part=Paragraph("",15,"#D88632"); part.FontFamily=Serif; part.FontStyle=ThemeStyles.FontStyle("fontstyle.doc.italic"); part.Margin=ThemeStyles.Thickness("thickness.doc.spacing.17");
                    part.Inlines.Add(new Run(EnglishPart(group.Key).ToLowerInvariant()){FontWeight=ThemeStyles.Weight("weight.doc.bold")});
                    part.Inlines.Add(new Run("  "+group.Key){FontSize=Size(12),FontFamily=ThemeStyles.Font("font.doc.body"),FontStyle=ThemeStyles.FontStyle("fontstyle.doc.normal"),Foreground=Color("#777777")});
                    string forms=record.Forms?.ForPartOfSpeech(group.Key);
                    if(!string.IsNullOrWhiteSpace(forms)) { var inflections=new Span { FontSize=Size(16), FontStyle=ThemeStyles.FontStyle("fontstyle.doc.normal"), FontFamily=ThemeStyles.Font("font.doc.body"), Foreground=Color("#666666") }; inflections.Inlines.Add(new Run("    ")); AddWords(inflections.Inlines,forms,inflections.Foreground); part.Inlines.Add(inflections); }
                    doc.Blocks.Add(part);
                    int index=0;
                    foreach(var sense in group.SelectMany(m=>m.Senses).GroupBy(s=>s.Definition).Select(g=>g.First()))
                    {
                        var p=Paragraph("",20); p.FontFamily=Serif; p.LineHeight=Size(30); p.Margin=ThemeStyles.Thickness("thickness.doc.spacing.18");
                        p.Inlines.Add(new Run((++index)+"  "){Foreground=Color("#D60032"),FontStyle=ThemeStyles.FontStyle("fontstyle.doc.italic"),FontWeight=ThemeStyles.Weight("weight.doc.bold")});
                        AddWords(p.Inlines,sense.Definition,p.Foreground); doc.Blocks.Add(p);
                        var chinese=Paragraph("",17,"#008577"); chinese.Margin=ThemeStyles.Thickness("thickness.doc.spacing.19"); chinese.LineHeight=Size(26);
                        if(!string.IsNullOrWhiteSpace(sense.Chinese)) chinese.Inlines.Add(new Run(sense.Chinese){Background=Color("#D7EAE6")});
                        else { chinese.Inlines.Add(new Run(sense.ChineseFailed ? "中文释义暂不可用" : "中文释义待补充")); chinese.FontSize=Size(12); chinese.Foreground=Color("#999999"); }
                        doc.Blocks.Add(chinese);
                        if(Settings.ShowExamples&&!string.IsNullOrWhiteSpace(sense.Example))
                        {
                            var example=WordParagraph(sense.Example,18,"#707070"); example.FontFamily=Serif; example.FontStyle=ThemeStyles.FontStyle("fontstyle.doc.italic"); example.Margin=ThemeStyles.Thickness("thickness.doc.spacing.20"); example.Padding=ThemeStyles.Thickness("thickness.doc.spacing.21"); example.BorderThickness=ThemeStyles.Thickness("thickness.doc.spacing.22"); example.BorderBrush=Color("#BADCF5"); doc.Blocks.Add(example);
                        }
                    }
                    var synonyms=group.SelectMany(m=>m.Synonyms).Distinct().ToArray(); var antonyms=group.SelectMany(m=>m.Antonyms).Distinct().ToArray();
                    if(synonyms.Length>0) Relation(doc,"同义词",string.Join(" · ",synonyms),"#0874C4");
                    if(antonyms.Length>0) Relation(doc,"反义词",string.Join(" · ",antonyms),"#9C5666");
                }
                if(!string.IsNullOrWhiteSpace(record.Origin)){doc.Blocks.Add(Heading("词源"));doc.Blocks.Add(WordParagraph(record.Origin,14,"#707070"));}
                if(Settings.ShowDerived){doc.Blocks.Add(Heading("派生与搭配"));doc.Blocks.Add(WordParagraph(record.Derived??"来源暂未提供派生词。",14,"#0874C4"));}
                if(!string.IsNullOrWhiteSpace(record.Notice))doc.Blocks.Add(Paragraph(record.Notice,13,"#996B43"));
                doc.Blocks.Add(Heading("来源与许可"));
                if(record.Meanings.SelectMany(m=>m.Senses).Any(s=>!string.IsNullOrWhiteSpace(s.Chinese))) doc.Blocks.Add(Paragraph("中文释义按各条英文释义机器翻译（"+(entry.TranslationProvider??"MyMemory")+"）",11,"#777777")); Link(doc,"Free Dictionary API · 英文释义","https://dictionaryapi.dev/"); Link(doc,"Wiktionary · 词形、派生词与复合词（CC BY-SA）","https://en.wiktionary.org/wiki/"+Uri.EscapeDataString(record.Word)+"#English");
                foreach(var source in record.Sources.Distinct()){var match=Regex.Match(source,@"https?://\S+");if(match.Success)Link(doc,source,match.Value);}
            }
            Link(doc,(entry.TranslationProvider??"MyMemory")+" · 参考翻译",entry.TranslationUrl??"https://mymemory.translated.net/"); return doc;
        }
        public static void Refresh(RichTextBox view, Entry entry)
        {
            string selected=view.Document.Tag as string; double offset=view.VerticalOffset;
            view.Document=Build(entry,selected); view.ScrollToVerticalOffset(offset);
        }
        private static void Relation(FlowDocument doc,string label,string words,string color)
        {
            var p=Paragraph("",13,color);p.Inlines.Add(new Run(label+"  "){FontWeight=ThemeStyles.Weight("weight.doc.semibold")});AddWords(p.Inlines,words,p.Foreground);doc.Blocks.Add(p);
        }
        private static void RenderLegacy(FlowDocument doc,string text)
        {
            bool skip=false;
            foreach(var line in text.Split('\n'))
            {
                string value=line.Trim();if(value.Length==0)continue;
                if(!Settings.ShowExamples&&value.StartsWith("例句："))continue;
                if(!Settings.ShowDerived&&value.StartsWith("派生词 /")){skip=true;continue;}
                if(skip){skip=false;continue;}
                var p=WordParagraph(value,16,"#555555"); p.FontFamily=Serif;
                if(Regex.IsMatch(value,@"^(n\.|v\.|adj\.|adv\.|词源|派生词|同义词|反义词)"))p.Foreground=Color("#D88632");
                if(value.StartsWith("例句：")){p.FontStyle=ThemeStyles.FontStyle("fontstyle.doc.italic");p.Foreground=Color("#707070");}doc.Blocks.Add(p);
            }
        }
    }
}






