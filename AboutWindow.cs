using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace MyDict
{
    public class AboutWindow : Window
    {
        public AboutWindow()
        {
            ThemeStyles.EnsureLoaded();
            Icon=new System.Windows.Media.Imaging.BitmapImage(new System.Uri("pack://application:,,,/MyDict;component/assets/app-icon.ico",System.UriKind.Absolute));
            Title = "关于拾词"; Width = ThemeStyles.Number("number.about.width",380); Height = ThemeStyles.Number("number.about.height",330); ResizeMode = ResizeMode.NoResize;
            WindowStartupLocation = WindowStartupLocation.CenterOwner; ShowInTaskbar = false;
            Background = ThemeStyles.BrushFor("#F5F3FA"); FontFamily = ThemeStyles.Font("font.about.body");
            var content = new StackPanel { Margin = ThemeStyles.Thickness("thickness.about.spacing.1") };
            content.Children.Add(new TextBlock { Text="拾词", FontSize=ThemeStyles.Number("number.about.size.30",30), FontWeight=ThemeStyles.Weight("weight.doc.semibold"), Foreground=ThemeStyles.BrushFor("#5C4BB7"), Margin=ThemeStyles.Thickness("thickness.about.spacing.2") });
            content.Children.Add(new TextBlock { Text="作者  Yance Feng & Codex", FontSize=ThemeStyles.Number("number.about.size.15",15), Margin=ThemeStyles.Thickness("thickness.about.spacing.3") });
            var version = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "1.3.0";
            content.Children.Add(new TextBlock { Text="Version " + version, FontSize=ThemeStyles.Number("number.about.size.14",14), Foreground=ThemeStyles.BrushFor("#777777"), Margin=ThemeStyles.Thickness("thickness.about.spacing.4") });
            content.Children.Add(new TextBlock { Text="中英查词 · 词形与释义 · 本地单词本", FontSize=ThemeStyles.Number("number.about.size.12",12), Foreground=ThemeStyles.BrushFor("#777777"), TextWrapping=TextWrapping.Wrap });
            var close = new Button { Content="关闭", HorizontalAlignment=HorizontalAlignment.Right, Margin=ThemeStyles.Thickness("thickness.about.spacing.5"), IsCancel=true };
            close.Click += (s,e) => Close(); content.Children.Add(close); Content=content;
            UseLayoutRounding=true; TextOptions.SetTextFormattingMode(this,TextFormattingMode.Ideal); TextOptions.SetTextRenderingMode(this,TextRenderingMode.ClearType);
        }
    }
}

