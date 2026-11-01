using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Microsoft.Win32;

namespace MyDict
{
    public partial class MainWindow : Window
    {
        private readonly FavoriteStore store = new FavoriteStore();
        private List<Entry> entries = new List<Entry>();
        private Entry current;
        private readonly LookupCache cache=new LookupCache();
        private bool cacheWritable=true;
        private string cacheError;
        private AppSettings settings = new AppSettings();
        private FileSystemWatcher settingsWatcher;
        private readonly DispatcherTimer settingsReload = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(450) };
        private bool storageAvailable = true;
        private bool suppressInput;
        private bool composing;
        private WindowState lastVisibleState = WindowState.Normal;
        private long revision;
        private CancellationTokenSource request;
        private readonly DispatcherTimer debounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(650) };
        public MainWindow()
        {
            ThemeStyles.EnsureLoaded();
            InitializeComponent();
            ThemeStyles.Changed+=ReloadTheme;
            try { settings = SettingsStore.Load(); } catch (Exception ex) { SetState("!", "设置读取失败，暂用默认值：" + ex.Message); }
            ApplySettings();
            ResultView.AddHandler(EntryDocument.WordLookupEvent, new RoutedEventHandler(WordLookup));
            SourceInitialized += (s, e) => { WindowPlacement.Restore(this, settings.Window); if (WindowState != WindowState.Minimized) lastVisibleState = WindowState; };
            StateChanged += (s, e) => { if (WindowState != WindowState.Minimized) lastVisibleState = WindowState; };
            Closing += (s, e) => SaveWindowLayout();
            debounce.Tick += async (s, e) => { debounce.Stop(); if (!composing) await SearchAsync(); };
            TextCompositionManager.AddPreviewTextInputStartHandler(Query, (s, e) => { composing = true; InvalidateQuery(); });
            TextCompositionManager.AddPreviewTextInputHandler(Query, (s, e) => { composing = false; debounce.Stop(); debounce.Start(); });
            try { entries = store.Load(); }
            catch (Exception ex) { storageAvailable = false; SetState("!", "词本读取失败，已暂停写入：" + ex.Message); }
            RefreshList();
            ResultView.Document = EntryDocument.Message("", "输入中文或英文");
            try { cache.Load(); } catch(Exception ex) { cacheWritable=false; cacheError="缓存读取失败，已保护原文件："+ex.Message; SetState("!",cacheError); }
            UpdateHistoryButtons();RefreshList();
            if(cache.Current!=null) ShowCached(cache.Current,"已恢复上次查词");
            PreviewKeyDown += HistoryKeys;
            Loaded += (s, e) => Query.Focus();
            StartSettingsWatcher();
            Closed += (s, e) => { debounce.Stop(); request?.Cancel(); settingsReload.Stop(); settingsWatcher?.Dispose();ThemeStyles.Changed-=ReloadTheme; };
        }
        private void SetState(string icon, string hint)
        {
            StateIcon.Text = icon; StateIcon.ToolTip = hint; StateMark.ToolTip = hint;
            string data = icon == "✓" ? "M4,12 L9,17 20,6" : icon == "!" ? "M12,4 L12,13 M12,19 L12,19.1" : icon == "…" ? "M5,12 L5.1,12 M12,12 L12.1,12 M19,12 L19.1,12" : "";
            StateMark.Data = string.IsNullOrEmpty(data) ? null : System.Windows.Media.Geometry.Parse(data);
        }
        private void SetSaved(bool saved) { SaveGlyph.Fill = saved ? ThemeStyles.BrushFor("#9370DB") : ThemeStyles.BrushFor("Transparent"); }
        private void InvalidateQuery()
        {
            debounce.Stop(); revision++; request?.Cancel(); current = null;
            if (SaveButton != null) SaveButton.IsEnabled = false;
            if (SaveGlyph != null) SetSaved(false);
        }
        private void Query_Changed(object sender, TextChangedEventArgs e)
        {
            if (suppressInput || ResultView == null) return;
            InvalidateQuery();
            SetState("", "输入后自动查询");
            if (string.IsNullOrWhiteSpace(Query.Text)) { ResultView.Document = EntryDocument.Message("", "输入中文或英文"); return; }
            if (!composing) debounce.Start();
        }
        private void RefreshList()
        {
            if (FavoritesList == null || Filter == null || CountLabel == null) return;
            string filter = Filter.Text.Trim();
            var all=entries.Concat(cache.AllEntries).GroupBy(e=>(e.Word??"").Trim().ToLowerInvariant()+"|"+e.Pair).Select(g=>g.First()).ToList();
            var rows=all.Select(e=>new BookRow { Entry=e, IsFavorite=entries.Any(f=>string.Equals(f.Word,e.Word,StringComparison.OrdinalIgnoreCase)&&f.Pair==e.Pair) });
            if(OnlyFavorites?.IsChecked==true) rows=rows.Where(r=>r.IsFavorite);
            FavoritesList.ItemsSource=rows.Where(r=>r.Word.IndexOf(filter,StringComparison.OrdinalIgnoreCase)>=0 || r.Translation.IndexOf(filter,StringComparison.OrdinalIgnoreCase)>=0).ToList();
            CountLabel.Text=entries.Count+" 收藏 / "+all.Count+" 词";
            EmptyFavorites.Visibility=FavoritesList.Items.Count==0?Visibility.Visible:Visibility.Collapsed;
            EmptyFavorites.Text="暂无匹配词条";
        }
        public class BookRow
        {
            public Entry Entry { get; set; }
            public string Word { get { return Entry.Word??""; } }
            public string Translation { get { return Entry.Translation??""; } }
            public bool IsFavorite { get; set; }
            public string Star { get { return IsFavorite?"★":"☆"; } }
        }
        private void BookStar_Click(object sender,RoutedEventArgs e)
        {
            var row=(sender as FrameworkElement)?.DataContext as BookRow;if(row==null)return;
            var updated=entries.Where(f=>!(string.Equals(f.Word,row.Word,StringComparison.OrdinalIgnoreCase)&&f.Pair==row.Entry.Pair)).ToList();
            if(!row.IsFavorite)updated.Insert(0,row.Entry);
                        if(Persist(updated))
            {
                var source=new AppSettings { Provider=row.Entry.TranslationProvider??"MyMemory", LibreUrl=row.Entry.TranslationUrl??settings.LibreUrl };
                cache.Store(LookupCache.Key(row.Word,row.Entry.Pair??Translator.DetectPair(row.Word),source),row.Entry);SaveCache();RefreshList();
                if(current!=null)SetSaved(entries.Any(f=>string.Equals(f.Word,current.Word,StringComparison.OrdinalIgnoreCase)&&f.Pair==current.Pair));
            }
            e.Handled=true;
        }
        private void BookStar_Preview(object sender,MouseButtonEventArgs e)
        {
            // Stop the row selection from navigating away before the star button receives its click.
            e.Handled=true;BookStar_Click(sender,e);
        }
        private void FavoriteMode_Changed(object sender,RoutedEventArgs e) { RefreshList(); }
        private async Task SearchAsync(bool forceRefresh=false)
        {
            string word = Query.Text.Trim();
            if (word.Length == 0 || composing) return;
            string pair = Translator.DetectPair(word);
            string cacheKey=LookupCache.Key(word,pair,settings);
            var cached=forceRefresh?null:cache.Find(cacheKey);
            if(cached!=null) { ShowCached(cached,"本地缓存（Ctrl+R 可联网更新）"); Remember(cacheKey,cached); return; }
            long version = revision;
            var active = new CancellationTokenSource(); request = active;
            CancellationToken token = active.Token;
            SetState("…", "查询中");
            try
            {
                if (Encoding.UTF8.GetByteCount(word) > 500) throw new InvalidOperationException("输入过长，请缩短至 500 个 UTF-8 字节以内。");
                Task<DictionaryRecord> dictionaryTask = pair.StartsWith("en|") && DictionaryService.IsEnglishTerm(word) ? DictionaryService.LookupAsync(word, token) : null;
                string translated;
                bool translationAvailable = true;
                string translationError = null;
                try { translated = await Translator.TranslateConfiguredAsync(word, pair, settings, token); }
                catch (Exception ex) when (!token.IsCancellationRequested && dictionaryTask != null && (ex is HttpRequestException || ex is TaskCanceledException || ex is InvalidOperationException))
                { translated = "翻译暂不可用"; translationAvailable = false; translationError = ex is HttpRequestException ? "翻译服务连接失败" : ex.Message; }
                token.ThrowIfCancellationRequested();
                string note = "";
                if (dictionaryTask == null && pair.StartsWith("zh") && DictionaryService.IsEnglishTerm(translated.Trim().TrimEnd('.', '!', '?')))
                {
                    string candidate = translated.Trim().TrimEnd('.', '!', '?');
                    note = "英文词条：" + candidate;
                    dictionaryTask = DictionaryService.LookupAsync(candidate, token);
                }
                DictionaryRecord record = dictionaryTask == null ? null : await dictionaryTask;
                token.ThrowIfCancellationRequested();
                if (version != revision) return;
                current = new Entry { Word = word, Translation = translated, Pair = pair, SavedAt = DateTime.Now.ToString("yyyy-MM-dd HH:mm"), Record = record, DetailNote = note, Details = record?.Format(), TranslationProvider = settings.Provider, TranslationUrl = settings.Provider == "LibreTranslate" ? settings.LibreUrl : "https://mymemory.translated.net/" };
                ResultView.Document = EntryDocument.Build(current); ResultView.ScrollToHome();
                SaveButton.IsEnabled = false;
                if(record != null && record.Meanings.Count > 0)
                {
                    SetState("…", "正在逐条翻译中文释义");
                    var entry=current;
                    await SenseTranslator.TranslateAsync(record,settings,token,()=> { if(version==revision && ReferenceEquals(current,entry)) EntryDocument.Refresh(ResultView,entry); });
                    token.ThrowIfCancellationRequested();
                    if(version!=revision) return;
                    current.Details=record.Format();
                }
                SaveButton.IsEnabled = storageAvailable && (translationAvailable || (record != null && record.Meanings.Count > 0));
                if(translationAvailable || record?.Meanings.Count>0) Remember(cacheKey,current);
                bool missingChinese=record!=null && record.Meanings.SelectMany(m=>m.Senses).Any(s=>s.ChineseFailed);
                SetState(translationAvailable && !missingChinese ? "✓" : "!", missingChinese ? "部分中文释义未获取，可稍后重新查询" : translationAvailable ? "查询完成" : translationError ?? "翻译服务暂不可用，词典资料已显示");
                if(cacheError!=null)SetState("!",cacheError);
            }
            catch (OperationCanceledException) { if (version == revision && !token.IsCancellationRequested) SetState("!", "查询超时，修改输入可重试"); }
            catch (Exception ex)
            {
                if (version != revision || token.IsCancellationRequested) return;
                SetState("!", ex is HttpRequestException ? "网络连接失败" : ex.Message);
                ResultView.Document = EntryDocument.Message("", ex is HttpRequestException ? "网络连接失败，请稍后重试" : ex.Message);
            }
            finally { if (ReferenceEquals(request, active)) request = null; active.Dispose(); }
        }
        private bool Persist(List<Entry> updated)
        {
            if (!storageAvailable) { SetState("!", "词本读取失败，请检查 " + store.FilePath); return false; }
            try { store.Save(updated); entries = updated; RefreshList(); return true; }
            catch (Exception ex) { SetState("!", "保存失败：" + ex.Message); return false; }
        }
        private void Save_Click(object sender, RoutedEventArgs e)
        {
            if (current == null) return;
            var updated = entries.Where(x => !(string.Equals(x.Word, current.Word, StringComparison.OrdinalIgnoreCase) && x.Pair == current.Pair)).ToList(); updated.Insert(0, current);
            if (Persist(updated)) { SetSaved(true); SetState("✓", "已收藏"); }
        }
        private void Favorite_Selected(object sender, SelectionChangedEventArgs e)
        {
            var selected = (FavoritesList.SelectedItem as BookRow)?.Entry;
            if (selected == null) return;
            InvalidateQuery(); suppressInput = true;
            Query.Text = selected.Word; suppressInput = false;
            current = selected;
            ResultView.Document = EntryDocument.Build(selected); ResultView.ScrollToHome();
            SaveButton.IsEnabled = storageAvailable; SetSaved(true);
            MainTabs.SelectedIndex = 0; SetState("✓", "本地收藏");
            var sourceSettings=new AppSettings { Provider=selected.TranslationProvider??"MyMemory", LibreUrl=selected.TranslationUrl??settings.LibreUrl };
            Remember(LookupCache.Key(selected.Word,selected.Pair??Translator.DetectPair(selected.Word),sourceSettings),selected);
        }
        private void Tabs_Changed(object sender, SelectionChangedEventArgs e)
        {
            if (e.Source != MainTabs || Query == null) return;
            if (MainTabs.SelectedIndex == 0) Query.Focus();
        }
        private void Filter_Changed(object sender, TextChangedEventArgs e) { RefreshList(); }
        private void Copy_Click(object sender, RoutedEventArgs e)
        {
            if (current == null) return;
            try { Clipboard.SetText(new System.Windows.Documents.TextRange(ResultView.Document.ContentStart, ResultView.Document.ContentEnd).Text); SetState("✓", "已复制"); }
            catch (Exception ex) { SetState("!", ex.Message); }
        }
        private void Export_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new SaveFileDialog { Filter = "CSV 文件 (*.csv)|*.csv", FileName = "我的词本.csv" };
            if (dialog.ShowDialog() != true) return;
            try { File.WriteAllText(dialog.FileName, FavoriteStore.Csv(entries), new UTF8Encoding(true)); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "导出失败"); }
        }
        private void ApplySettings()
        {
            EntryDocument.Settings = settings;
            debounce.Interval = TimeSpan.FromMilliseconds(settings.DelayMs);
            if (current != null) EntryDocument.Refresh(ResultView,current);
        }
        private void ReloadTheme()
        {
            if(!string.IsNullOrEmpty(ThemeStyles.LastError)){SetState("!","JSON 样式无效，保留上一份样式："+ThemeStyles.LastError);return;}
            if(current!=null)EntryDocument.Refresh(ResultView,current);
            else ResultView.Document=EntryDocument.Message("","输入中文或英文");
            RefreshList();UpdateLayout();
            SetState("✓","JSON 设置和样式已重新加载");
        }
        private void StartSettingsWatcher()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(SettingsStore.FilePath));
                settingsReload.Tick += (s, e) => { settingsReload.Stop(); ReloadSettings(); };
                settingsWatcher = new FileSystemWatcher(Path.GetDirectoryName(SettingsStore.FilePath), Path.GetFileName(SettingsStore.FilePath)) { NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size };
                FileSystemEventHandler changed = (s, e) => ScheduleSettingsReload();
                settingsWatcher.Changed += changed; settingsWatcher.Created += changed;
                settingsWatcher.Renamed += (s, e) => ScheduleSettingsReload();
                settingsWatcher.EnableRaisingEvents = true;
            }
            catch (Exception ex) { SetState("!", "设置监控不可用：" + ex.Message); }
        }
        private void ScheduleSettingsReload()
        {
            if (Dispatcher.HasShutdownStarted) return;
            Dispatcher.BeginInvoke(new Action(() => { settingsReload.Stop(); settingsReload.Start(); }));
        }
        private void ReloadSettings()
        {
            if (!File.Exists(SettingsStore.FilePath)) return;
            try
            {
                var updated = SettingsStore.Load();
                bool sourceChanged = settings.Provider != updated.Provider || settings.LibreUrl != updated.LibreUrl || settings.ApiKey != updated.ApiKey;
                ThemeStyles.ValidateStyles(updated.Styles);
                settings = updated; EntryDocument.Settings=updated; ThemeStyles.ApplyStyles(updated.Styles); ApplySettings();
                if (sourceChanged) { InvalidateQuery(); if (!string.IsNullOrWhiteSpace(Query.Text)) debounce.Start(); }
                SetState("✓", "JSON 设置已加载");
            }
            catch (Exception ex) { SetState("!", "设置文件无效，已保留当前设置：" + ex.Message); }
        }
        private void Settings_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                if (!File.Exists(SettingsStore.FilePath)) SettingsStore.Save(settings);
                // Pass the path as a single quoted argument; edits are picked up after saving.
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("notepad.exe", "\"" + SettingsStore.FilePath + "\"") { UseShellExecute = true });
            }
            catch (Exception ex) { SetState("!", "无法打开设置文件：" + ex.Message); }
        }
        private void SaveWindowLayout()
        {
            try
            {
                var bounds = WindowState == WindowState.Normal ? new Rect(Left, Top, Width, Height) : RestoreBounds;
                SettingsStore.SaveWindow(new WindowLayout { Left = bounds.Left, Top = bounds.Top, Width = bounds.Width, Height = bounds.Height, Maximized = lastVisibleState == WindowState.Maximized });
            }
            catch (Exception ex) { System.Diagnostics.Trace.WriteLine("窗口位置保存失败，保留原设置：" + ex.Message); }
        }
        private async void WordLookup(object sender,RoutedEventArgs e)
        {
            var args=e as EntryDocument.WordLookupArgs;
            if(args==null || string.IsNullOrWhiteSpace(args.Word))return;
            InvalidateQuery(); suppressInput=true; Query.Text=args.Word; suppressInput=false;
            MainTabs.SelectedIndex=0; e.Handled=true;
            await SearchAsync();
        }
        private void UpdateHistoryButtons() { BackButton.IsEnabled=cache.CanBack;ForwardButton.IsEnabled=cache.CanForward; }
        private void SaveCache()
        {
            if(!cacheWritable)return;
            try { cache.Save();cacheError=null; } catch(Exception ex) { cacheError="缓存保存失败："+ex.Message;SetState("!",cacheError); }
        }
        private void Remember(string key,Entry entry) { cache.Record(key,entry);SaveCache();UpdateHistoryButtons();RefreshList(); }
        private void ShowCached(Entry entry,string hint)
        {
            InvalidateQuery();suppressInput=true;Query.Text=entry.Word;suppressInput=false;current=entry;
            ResultView.Document=EntryDocument.Build(entry);ResultView.ScrollToHome();
            SaveButton.IsEnabled=storageAvailable;
            SetSaved(entries.Any(e=>string.Equals(e.Word,entry.Word,StringComparison.OrdinalIgnoreCase)&&e.Pair==entry.Pair));
            MainTabs.SelectedIndex=0;SetState("✓",hint);
        }
        private void NavigateHistory(int delta)
        {
            var entry=cache.Move(delta);if(entry==null)return;
            ShowCached(entry,"历史查词 · 本地缓存");SaveCache();UpdateHistoryButtons();
        }
        private void Back_Click(object sender,RoutedEventArgs e) { NavigateHistory(-1); }
        private void Forward_Click(object sender,RoutedEventArgs e) { NavigateHistory(1); }
        private async void HistoryKeys(object sender,KeyEventArgs e)
        {
            if(Keyboard.Modifiers==ModifierKeys.Alt && (e.SystemKey==Key.Left || e.SystemKey==Key.Right)) {NavigateHistory(e.SystemKey==Key.Left?-1:1);e.Handled=true;}
            else if(Keyboard.Modifiers==ModifierKeys.Control && e.Key==Key.R) {InvalidateQuery();e.Handled=true;await SearchAsync(true);}
        }
        private void About_Click(object sender, RoutedEventArgs e) { new AboutWindow { Owner = this }.ShowDialog(); }
        private void DragWindow(object sender, MouseButtonEventArgs e) { if (e.ClickCount == 2) Maximize_Click(sender, e); else if (e.LeftButton == MouseButtonState.Pressed) DragMove(); }
        private void Minimize_Click(object sender, RoutedEventArgs e) { WindowState = WindowState.Minimized; }
        private void Maximize_Click(object sender, RoutedEventArgs e) { WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized; }
        private void Close_Click(object sender, RoutedEventArgs e) { Close(); }
    }
}
















