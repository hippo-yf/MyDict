using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace MyDict
{
    public static class ThemeStyles
    {
        public static string FilePath { get { return SettingsStore.FilePath; } }
        public static event Action Changed;
        public static string LastError {get;private set;}
        private static Dictionary<string,object> values;
        private static Dictionary<string,string> defaults;
        private static string DefaultText()
        {
            using(var stream=typeof(ThemeStyles).Assembly.GetManifestResourceStream("MyDict.styles.defaults.json"))
            using(var reader=new StreamReader(stream,Encoding.UTF8))return reader.ReadToEnd();
        }
        public static void EnsureLoaded()
        {
            if(values!=null)return;
            defaults=new System.Web.Script.Serialization.JavaScriptSerializer().Deserialize<Dictionary<string,string>>(DefaultText());
            Apply(defaults);
            try
            {
                SettingsStore.Load();
                MigrateSettings(FilePath,Path.Combine(Path.GetDirectoryName(FilePath),"styles.yaml"));
                ConfigLayout.Upgrade(FilePath);
                Reload();
            }
            catch(Exception ex){LastError=ex.Message;}
        }
        public static Dictionary<string,string> DefaultStyles() { if(defaults==null)EnsureLoaded();return new Dictionary<string,string>(defaults); }
        public static void MigrateSettings(string path,string oldStylesPath)
        {
            var serializer=new System.Web.Script.Serialization.JavaScriptSerializer();
            var root=serializer.Deserialize<Dictionary<string,object>>(File.ReadAllText(path,Encoding.UTF8));
            if(root==null)throw new InvalidOperationException("设置文件为空。");
            object existing;if(root.TryGetValue("Styles",out existing)&&existing!=null)return;
            var styles=new Dictionary<string,string>(defaults);
            if(File.Exists(oldStylesPath))foreach(var item in Read(File.ReadAllText(oldStylesPath,Encoding.UTF8)))styles[item.Key]=item.Value;
            ValidateStyles(styles);root["Styles"]=styles;ConfigLayout.Decorate(root);
            File.WriteAllText(path+".tmp",SettingsStore.FormatJson(serializer.Serialize(root)),new UTF8Encoding(false));
            if(File.Exists(path))File.Replace(path+".tmp",path,path+".bak");else File.Move(path+".tmp",path);
        }
        private static Dictionary<string,string> Merge(Dictionary<string,string> overrides)
        {
            var merged=new Dictionary<string,string>(defaults);
            if(overrides!=null)foreach(var item in overrides)
            {if(!defaults.ContainsKey(item.Key))throw new InvalidOperationException("未知样式键："+item.Key);merged[item.Key]=item.Value;}
            return merged;
        }
        public static void ValidateStyles(Dictionary<string,string> overrides)
        {
            foreach(var item in Merge(overrides))ConvertValue(item.Key,item.Value);
        }
        public static void ApplyStyles(Dictionary<string,string> overrides)
        {
            Apply(Merge(overrides));LastError=null;Changed?.Invoke();
        }
        public static void Reload()
        {
            var settings=SettingsStore.Load(FilePath);ApplyStyles(settings.Styles);
        }        private static Dictionary<string,string> Read(string yaml)
        {
            var result=new Dictionary<string,string>();int lineNumber=0;
            foreach(string raw in yaml.Split('\n'))
            {
                lineNumber++;string line=raw.Trim().TrimStart('\uFEFF');if(line.Length==0||line.StartsWith("#"))continue;
                int colon=line.IndexOf(':');if(colon<=0)throw new InvalidOperationException("YAML 第 "+lineNumber+" 行缺少冒号。");
                string key=line.Substring(0,colon).Trim(), value=line.Substring(colon+1).Trim();
                if(value.StartsWith("'")) {if(!value.EndsWith("'")||value.Length<2)throw new InvalidOperationException("YAML 引号未闭合："+key);value=value.Substring(1,value.Length-2).Replace("''","'");}
                else if(value.StartsWith("\""))value=new System.Web.Script.Serialization.JavaScriptSerializer().Deserialize<string>(value);
                else {int comment=value.IndexOf(" #",StringComparison.Ordinal);if(comment>=0)value=value.Substring(0,comment).Trim();if(value.Length==0||value.StartsWith("[")||value.StartsWith("{"))throw new InvalidOperationException("样式值需要标量："+key);}
                if(result.ContainsKey(key))throw new InvalidOperationException("重复样式键："+key);result[key]=value;
            }
            return result;
        }
        private static object ConvertValue(string key,string value)
        {
            string type=key.Substring(0,key.IndexOf('.'));
            switch(type)
            {
                case "number": double number=value=="Auto"?double.NaN:double.Parse(value,CultureInfo.InvariantCulture);if(double.IsInfinity(number)||number<0)throw new InvalidOperationException("尺寸须为非负有限值");if(key.Contains("opacity") && number>1)throw new InvalidOperationException("透明度须在 0–1 之间");if((key.Contains("size")||key.Contains("scale")||key.Contains("line_spacing"))&&(double.IsNaN(number)||number<=0))throw new InvalidOperationException("字号与缩放须大于 0");return number;
                case "color":return (Color)ColorConverter.ConvertFromString(value);
                case "font":if(string.IsNullOrWhiteSpace(value))throw new InvalidOperationException("字体不可为空");return AppFonts.Resolve(value);
                case "weight":return new FontWeightConverter().ConvertFromInvariantString(value);
                case "fontstyle":return new FontStyleConverter().ConvertFromInvariantString(value);
                case "thickness":return new ThicknessConverter().ConvertFromInvariantString(value);
                case "corner":return new CornerRadiusConverter().ConvertFromInvariantString(value);
                case "grid":return new GridLengthConverter().ConvertFromInvariantString(value);
                case "textalign":return Enum.Parse(typeof(TextAlignment),value);
                case "halign":return Enum.Parse(typeof(HorizontalAlignment),value);
                case "valign":return Enum.Parse(typeof(VerticalAlignment),value);
                case "formatting":return Enum.Parse(typeof(TextFormattingMode),value);
                case "rendering":return Enum.Parse(typeof(TextRenderingMode),value);
                case "hinting":return Enum.Parse(typeof(TextHintingMode),value);
                default:throw new InvalidOperationException("不支持的样式类型："+type);
            }
        }
        private static void Apply(Dictionary<string,string> raw)
        {
            var converted=new Dictionary<string,object>();
            foreach(var item in raw)
            {
                object value;
                try {value=ConvertValue(item.Key,item.Value);}catch(Exception ex){throw new InvalidOperationException(item.Key+"："+ex.Message);}
                converted[item.Key]=value;
                if(item.Key.StartsWith("color."))converted["brush."+item.Key.Substring(6)]=new SolidColorBrush((Color)value);
            }
            // Validate the complete document before touching live resources.
            values=converted;
            foreach(var item in converted)Application.Current.Resources[item.Key]=item.Value;
        }
        public static double Number(string key,double fallback=16) {object v;return values!=null&&values.TryGetValue(key,out v)?(double)v:fallback;}
        public static FontFamily Font(string key) {return (FontFamily)values[key];}
        public static Thickness Thickness(string key) {return (Thickness)values[key];}
        public static CornerRadius Corner(string key) {return (CornerRadius)values[key];}
        public static FontWeight Weight(string key) {return (FontWeight)values[key];}
        public static FontStyle FontStyle(string key) {return (FontStyle)values[key];}
        public static Brush Brush(string key) {return (Brush)values[key];}
        public static Brush BrushFor(string value)
        {
            string name=value.StartsWith("#")?"c"+value.Substring(1).ToLowerInvariant():value.ToLowerInvariant();object brush;
            return values!=null&&values.TryGetValue("brush."+name,out brush)?(Brush)brush:(Brush)new BrushConverter().ConvertFromString(value);
        }
    }
}






