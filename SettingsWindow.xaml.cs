using System;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace MyDict
{
    public partial class SettingsWindow : Window
    {
        public AppSettings Result { get; private set; }
        private readonly AppSettings original;
        public SettingsWindow(AppSettings settings)
        {
            original = settings;
            InitializeComponent();
            FontPicker.ItemsSource = AppFonts.FamilyNames.Concat(Fonts.SystemFontFamilies.Select(f => f.Source)).Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(f => f).ToList();
            FontPicker.SelectionChanged += (s,e) => Preview();
            SizeInput.TextChanged += (s,e) => Preview();
            ProviderPicker.SelectionChanged += (s,e) => { if (LibrePanel != null) LibrePanel.Visibility = ProviderPicker.SelectedIndex == 1 ? Visibility.Visible : Visibility.Collapsed; };
            Fill(settings);
        }
        private void Fill(AppSettings s)
        {
            FontPicker.SelectedItem = s.FontName;
            if (FontPicker.SelectedIndex < 0) FontPicker.SelectedItem = "Microsoft YaHei UI";
            SizeInput.Text = s.FontSize.ToString(CultureInfo.CurrentCulture); DelayInput.Text = s.DelayMs.ToString();
            ExamplesCheck.IsChecked = s.ShowExamples; DerivedCheck.IsChecked = s.ShowDerived;
            ProviderPicker.SelectedIndex = s.Provider == "LibreTranslate" ? 1 : 0;
            LibrePanel.Visibility = ProviderPicker.SelectedIndex == 1 ? Visibility.Visible : Visibility.Collapsed;
            UrlInput.Text = s.LibreUrl; KeyInput.Password = s.ApiKey ?? ""; Preview();
        }
        private void Preview()
        {
            if (FontPreview == null) return;
            if (FontPicker.SelectedItem != null) FontPreview.FontFamily = AppFonts.Resolve((string)FontPicker.SelectedItem);
            double size; if (double.TryParse(SizeInput.Text, out size) && size >= 12 && size <= 28) FontPreview.FontSize = size;
        }
        private void Defaults_Click(object sender, RoutedEventArgs e) { Fill(new AppSettings()); ErrorText.Text = ""; }
        private void Save_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                double size; int delay;
                if (!double.TryParse(SizeInput.Text, out size) || !int.TryParse(DelayInput.Text, out delay)) throw new InvalidOperationException("字号和延迟请输入有效数字。");
                var result = new AppSettings { FontName = FontPicker.SelectedItem as string, FontSize = size, DelayMs = delay, Provider = ProviderPicker.SelectedIndex == 1 ? "LibreTranslate" : "MyMemory", LibreUrl = UrlInput.Text.Trim(), ApiKey = KeyInput.Password.Trim(), ShowExamples = ExamplesCheck.IsChecked == true, ShowDerived = DerivedCheck.IsChecked == true };
                result.username = original.username; result.CacheSync = original.CacheSync; result.DeepSeek = original.DeepSeek; result.Styles = original.Styles; result.Window = original.Window;
                result.Validate(); SettingsStore.Save(result); Result = result; DialogResult = true;
            }
            catch (Exception ex) { ErrorText.Text = "无法保存设置：" + ex.Message; }
        }
    }
}
