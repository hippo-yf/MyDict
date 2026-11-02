using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;
namespace MyDict
{
    public static class ConfigLayout
    {
        public static Dictionary<string,string> FlattenStyles(object node)
        {
            var result=new Dictionary<string,string>(); Flatten(node,result); return result;
        }
        private static void Flatten(object node,Dictionary<string,string> result)
        {
            var strings=node as Dictionary<string,string>;
            if(strings!=null){foreach(var item in strings){if(item.Key.StartsWith("_"))continue;if(result.ContainsKey(item.Key))throw new InvalidOperationException("重复样式键："+item.Key);result[item.Key]=item.Value;}return;}
            var map=node as Dictionary<string,object>;
            if(map==null) {if(node!=null)throw new InvalidOperationException("Styles 必须是对象。");return;}
            foreach(var item in map)
            {
                if(item.Key.StartsWith("_"))continue;
                if(item.Value is Dictionary<string,object>)Flatten(item.Value,result);
                else {if(result.ContainsKey(item.Key))throw new InvalidOperationException("重复样式键："+item.Key);result[item.Key]=Convert.ToString(item.Value,CultureInfo.InvariantCulture);}
            }
        }
        private static string Group(string key)
        {
            if(key.Contains(".grammar."))return "09_句法与词性样式";
            if(key.StartsWith("color."))return "01_全局配色";
            if(key.Contains(".deepseek."))return "08_DeepSeek与查询";
            if(key.Contains(".book."))return "02_单词本";
            if(key.Contains(".doc."))return "03_词条与释义";
            if(key.Contains(".mainwindow."))return "04_主窗口与导航";
            if(key.Contains(".about."))return "05_关于窗口";
            if(key.Contains(".settingswindow."))return "06_旧版设置窗口";
            return "07_通用控件";
        }
        public static Dictionary<string,object> GroupStyles(Dictionary<string,string> flat)
        {
            var result=new Dictionary<string,object>();
            foreach(var group in flat.GroupBy(p=>Group(p.Key)).OrderBy(g=>g.Key))
            {
                var values=new Dictionary<string,object>();var notes=new Dictionary<string,object>();
                foreach(var item in group)notes[item.Key]=DescribeStyle(item.Key);
                values["_说明"]=notes;
                foreach(var item in group)values[item.Key]=item.Value;
                result[group.Key]=values;
            }
            return result;
        }
        public static string DescribeStyle(string key)
        {
            var named=new Dictionary<string,string> {
                {"font.doc.body","中文释义及辅助正文使用的系统字体名称。"},{"font.doc.serif","英文词头、英文释义和例句使用的衬线字体名称。"},
                {"font.doc.phonetic","音标字体；请选择支持 IPA 音标的系统字体。"},{"font.doc.tabs","词性筛选按钮字体。"},
                {"number.doc.leading_scale","解释区行距倍率；Ctrl+[ 或 Ctrl+] 调整。"},
                {"number.doc.scale","解释区整体尺寸倍率；1 为原始大小，1.1 表示放大 10%。"},{"number.doc.line_spacing","普通段落的行距倍率。"},
                {"number.book.word_size","单词本词头字号。"},{"number.book.translation_size","单词本中文翻译字号。"},
                {"number.book.word_line_height","单词本词头每行高度。"},{"number.book.translation_line_height","单词本翻译每行高度。"},
                {"thickness.book.row_padding","单词本条目内边距；两个值表示水平、垂直，四个值为左、上、右、下。"},
                {"thickness.book.row_margin","单词本条目外边距，按左、上、右、下填写。"},
                {"number.book.star_size","单词本每条右侧收藏星标字号。"},{"number.book.star_height","单词本收藏星标按钮高度。"},
                {"number.doc.size.20","英文释义、音标等使用的基准字号（乘以整体倍率）。"},
                {"number.doc.size.17","每条中文释义的基准字号（乘以整体倍率）。"},
                {"number.doc.size.16","词形信息及普通正文的基准字号（乘以整体倍率）。"}
            };
            string value;if(named.TryGetValue(key,out value))return value;
            if(key.StartsWith("thickness.doc.spacing."))
            {
                var descriptions=new[]{"普通段落外边距","小标题外边距","小标题左侧标线粗细","小标题内边距","来源链接外边距","整个解释文档内边距","词头卡片边框粗细","词头卡片内边距","词头卡片外边距","词性导航栏内边距","词性导航栏外边距","词性按钮内边距","词性按钮外边距","蓝色词头外边距","音标外边距","基础翻译外边距","词性与词形行外边距","编号英文释义外边距","中文释义外边距","例句外边距","例句内边距","例句左侧标线粗细"};
                int index;if(int.TryParse(key.Substring(key.LastIndexOf('.')+1),out index)&&index>=1&&index<=descriptions.Length)return descriptions[index-1]+"；单值应用于四边，两个值为水平/垂直，四个值按左/上/右/下填写。";
            }
            string area=Group(key).Substring(3);
            var controls=new Dictionary<string,string>{{".query.","查询输入框"},{".backbutton.","历史后退按钮"},{".forwardbutton.","历史前进按钮"},{".settingsbutton.","设置按钮"},{".aboutbutton.","关于按钮"},{".savebutton.","收藏按钮"},{".copybutton.","复制按钮"},{".favoriteslist.","单词本列表"},{".filter.","单词本筛选输入框"},{".countlabel.","收藏数量文字"},{".onlyfavorites.","只看收藏复选框"},{".emptyfavorites.","单词本空列表提示"},{".window.1.","窗口"},{".maintabs.","查词/单词本选项卡"},{".part_contenthost.","输入框内容区域"},{".frame.","输入框外框"},{".surface.","按钮背景区域"}};
            foreach(var control in controls)if(key.Contains(control.Key)){area+=" / "+control.Value;break;}
            string property=key.Substring(key.LastIndexOf('.')+1);
            var properties=new Dictionary<string,string>{{"fontsize","字号"},{"width","宽度"},{"height","高度"},{"minwidth","最小宽度"},{"minheight","最小高度"},{"padding","内边距"},{"margin","外边距"},{"borderthickness","边框粗细"},{"cornerradius","圆角半径"},{"strokethickness","图标线条粗细"},{"fontweight","字体粗细"},{"fontfamily","字体名称"},{"lineheight","每行高度"},{"opacity","透明度"},{"horizontalalignment","水平对齐"},{"verticalalignment","垂直对齐"},{"textformattingmode","字体排版模式"},{"textrenderingmode","文字抗锯齿方式"},{"texthintingmode","字体提示方式"},{"shadowdepth","阴影偏移"},{"blurradius","阴影模糊半径"}};
            string label; if(!properties.TryGetValue(property,out label))label="样式参数 "+property;
            if(key.StartsWith("color."))return "全局调色板颜色；支持 #RRGGBB、#AARRGGBB 或颜色名称。相同色值会被多个控件共用。";
            if(key.StartsWith("thickness."))return area+"的"+label+"；1 个值应用于四边，2 个值为水平/垂直，4 个值为左/上/右/下。";
            if(key.StartsWith("fontstyle."))return area+"字体样式：Normal 正常，Italic 斜体。";
            if(key.StartsWith("weight."))return area+"字重：Normal 常规，SemiBold 半粗，Bold 粗体。";
            if(key.StartsWith("font."))return area+"使用的系统字体名称。";
            if(key.StartsWith("grid."))return area+"的行列尺寸；Auto 自动，* 按剩余空间分配，数值为固定尺寸。";
            if(key.StartsWith("number.doc.size."))return "解释区字号/行高的基准尺寸 "+property+"，实际值还会乘以 number.doc.scale。";
            if(key.StartsWith("corner."))return area+"的圆角半径，0 表示直角。";
            return area+"的"+label+"（"+key+"）。尺寸单位为 WPF 设备无关像素；透明度范围 0–1。";
        }
        public static void Decorate(Dictionary<string,object> root)
        {
            var notes=new Dictionary<string,object> {
                {"FontName","旧版界面字体兼容字段；当前显示字体请在 Styles 中修改。"},{"FontSize","旧版字号兼容字段，保留范围 12–28；当前显示字号由 Styles 决定。"},
                {"DefinitionFontName","旧版英文释义字体兼容字段；当前使用 Styles 中的 font.doc.serif。"},
                {"DelayMs","停止输入后自动查询的等待时间，单位毫秒，范围 300–3000。"},{"Provider","翻译来源：MyMemory 或 LibreTranslate。"},
                {"LibreUrl","LibreTranslate 完整 /translate 接口地址；公共服务器使用 HTTPS，本机可使用 HTTP。"},
                {"ApiKey","手动填写的翻译服务密钥，留空时读取 ProtectedApiKey；此字段填写后以明文保存，请勿公开。"},
                {"ProtectedApiKey","由 Windows 当前用户加密的旧版密钥；通常无需手动修改，清除时设为 null。"},
                {"ShowExamples","是否显示词典例句：true 显示，false 隐藏。"},{"ShowDerived","是否显示派生词及搭配：true 显示，false 隐藏。"},
                {"Window","上次关闭时保存的窗口位置、大小及最大化状态；一般由程序自动维护。"},
                {"DeepSeek","DeepSeek API 查询开关、密钥、接口地址和模型。启用后直接输入不会自动调用 API；点击查询按钮或解释中的单词可查询。"},
                {"Styles","按用途分组的显示样式；只修改样式键的值，保存后自动重新加载。未填写的键使用内置默认值。"},
                {"_说明","中文使用说明，不参与程序配置；可修改说明文字。JSON 不支持 // 注释。"},
                {"_配置版本","配置排版版本，供程序识别分组和说明格式，请保持为 3。"}
            };
            object old;var existing=root.TryGetValue("_说明",out old)?old as Dictionary<string,object>:null;
            if(existing!=null)foreach(var item in existing)notes[item.Key]=item.Value;
            foreach(var key in root.Keys)if(!notes.ContainsKey(key))notes[key]="保留的自定义字段，程序不会将其作为显示样式读取。";
            root["_说明"]=notes;notes["_配置版本"]="配置排版版本，请保持为 3。";root["_配置版本"]=3;
            object styles;if(root.TryGetValue("Styles",out styles)&&styles!=null)root["Styles"]=GroupStyles(FlattenStyles(styles));
            object deepRaw;
            var serializer=new JavaScriptSerializer();
            var deep=root.TryGetValue("DeepSeek",out deepRaw)?deepRaw as Dictionary<string,object>:null;
            if(deep==null){deep=serializer.Deserialize<Dictionary<string,object>>(serializer.Serialize(new DeepSeekOptions()));root["DeepSeek"]=deep;}
            deep["_说明"]=new Dictionary<string,object>{{"Enabled","是否启用 DeepSeek。true 时直接输入和历史导航只读缓存；点击查询按钮或解释中的单词可调用 API。"},{"ApiKey","DeepSeek API 密钥，保存在本地 JSON 中，请勿公开。"},{"Endpoint","完整 HTTPS Chat Completions 接口地址，默认 https://api.deepseek.com/chat/completions。"},{"Model","模型名称，默认 deepseek-flash；可按服务提供方文档修改。"},{"TimeoutSeconds","请求超时秒数，范围 10–300。"},{"MaxTokens","输出 token 上限，范围 512–16384；截断时可适当增大。"}};
            object window;var position=root.TryGetValue("Window",out window)?window as Dictionary<string,object>:null;
            if(position!=null)position["_说明"]=new Dictionary<string,object>{{"Left","普通窗口左侧坐标，可为负值（位于左侧副显示器时）。"},{"Top","普通窗口顶部坐标。"},{"Width","普通窗口宽度。"},{"Height","普通窗口高度。"},{"Maximized","下次启动是否最大化；true 最大化，false 普通窗口。"}};
        }
        public static void Upgrade(string path)
        {
            var serializer=new JavaScriptSerializer();
            var root=serializer.Deserialize<Dictionary<string,object>>(File.ReadAllText(path,Encoding.UTF8));
            if(root==null)throw new InvalidOperationException("配置文件为空。");
            object version;if(root.TryGetValue("_配置版本",out version)&&Convert.ToString(version)=="3")return;
            Decorate(root);
            File.WriteAllText(path+".tmp",SettingsStore.FormatJson(serializer.Serialize(root)),new UTF8Encoding(false));
            File.Replace(path+".tmp",path,path+".bak");
        }
    }
}




