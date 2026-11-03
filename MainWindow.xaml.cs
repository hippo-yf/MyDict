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
        private WordDatabase database;
        private bool databaseReady;
        private Task storageTask=Task.CompletedTask;
        private int storageGeneration,bookRevision,bookPage;
        private long bookMatches;
        private bool currentIsFavorite;
        private readonly HashSet<string> removingWords=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        private Func<string,bool> confirmDeepSeekRefresh;
        private bool confirmingRefresh;
        private readonly DispatcherTimer noticeTimer=new DispatcherTimer{Interval=TimeSpan.FromSeconds(3)};
        private readonly HashSet<Task> pendingWrites=new HashSet<Task>();
        private readonly DispatcherTimer bookFilterTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(200)};
        private Entry current;
        private readonly DeferredSave deferredSave = new DeferredSave();
        private readonly DispatcherTimer cacheTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(10) };
        private long dataRevision;
        private bool closingAfterFlush;
        private bool closingInProgress;
        private bool settingsValid = true;
        private bool manualSyncRequested;
        private bool syncBusy;
        private bool windowClosed;
        private Func<WordDatabase,bool,Task> exchangeDatabase;
        private string profileDirectory;
        private bool cacheWritable=true;
        private AppSettings settings = new AppSettings();
        private DeepSeekService deepSeek=new DeepSeekService();
        private bool suppressDeepSeekToggle;
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
            exchangeDatabase=(db,upload)=>CreateCacheSync().ExchangeDatabaseAsync(db,CacheIdentity.DeviceId(settings.CacheSync.DeviceId),upload);
            confirmDeepSeekRefresh=word=>MessageBox.Show(this,"“"+word+"”已有 DeepSeek 释义，是否重新查询并更新？","重新查询 DeepSeek",MessageBoxButton.YesNo,MessageBoxImage.Question,MessageBoxResult.No)==MessageBoxResult.Yes;
            noticeTimer.Tick+=(s,e)=>{noticeTimer.Stop();ActionNotice.IsOpen=false;};
            ThemeStyles.Changed+=ReloadTheme;
            try { settings = SettingsStore.Load(); } catch (Exception ex) { settingsValid = false; settings.username = "recovery-" + Guid.NewGuid().ToString("N").Substring(0,16); settings.CacheSync.Enabled = false; SetState("!", "设置读取失败，暂用默认值并暂停缓存保存：" + ex.Message); }
            ApplySettings();
            ResultView.AddHandler(EntryDocument.WordLookupEvent, new RoutedEventHandler(WordLookup));
            SourceInitialized += (s, e) => { WindowPlacement.Restore(this, settings.Window); if (WindowState != WindowState.Minimized) lastVisibleState = WindowState; };
            StateChanged += (s, e) => { if (WindowState != WindowState.Minimized) lastVisibleState = WindowState; };
            Closing += FlushOnClosing;
            cacheTimer.Tick += async (s, e) => { if (deferredSave.IsDue(DateTime.UtcNow)) await FlushPendingAsync(); };
            cacheTimer.Start();
            debounce.Tick += async (s, e) => { debounce.Stop(); if (!composing) await SearchAsync(); };
            TextCompositionManager.AddPreviewTextInputStartHandler(Query, (s, e) => { composing = true; InvalidateQuery(); });
            TextCompositionManager.AddPreviewTextInputHandler(Query, (s, e) => { composing = false; debounce.Stop(); debounce.Start(); });
            bookFilterTimer.Tick+=(s,e)=>{bookFilterTimer.Stop();RefreshList();};
            ResultView.Document = EntryDocument.Message("", "输入中文或英文");
            BackButton.IsEnabled=false;ForwardButton.IsEnabled=false;SaveButton.IsEnabled=false;
            PreviewKeyDown += HistoryKeys;
            Loaded += async (s, e) => { Query.Focus(); await Dispatcher.Yield(DispatcherPriority.Background);storageTask=InitializeStorageAsync();await storageTask;if(databaseReady)await PullCacheAsync(); };
            StartSettingsWatcher();
            Closed += (s, e) => { windowClosed = true;noticeTimer.Stop();ActionNotice.IsOpen=false;bookRevision++;storageGeneration++;bookFilterTimer.Stop(); cacheTimer.Stop(); debounce.Stop(); request?.Cancel(); settingsReload.Stop(); settingsWatcher?.Dispose();ThemeStyles.Changed-=ReloadTheme; };
        }
        private void SetState(string icon, string hint)
        {
            StateIcon.Text = icon; StateIcon.ToolTip = hint; StateMark.ToolTip = hint;
            string data = icon == "✓" ? "M4,12 L9,17 20,6" : icon == "!" ? "M12,4 L12,13 M12,19 L12,19.1" : icon == "…" ? "M5,12 L5.1,12 M12,12 L12.1,12 M19,12 L19.1,12" : "";
            StateMark.Data = string.IsNullOrEmpty(data) ? null : System.Windows.Media.Geometry.Parse(data);
        }
        private void SetSaved(bool saved) { SaveGlyph.Fill = saved ? ThemeStyles.BrushFor("#9370DB") : ThemeStyles.BrushFor("Transparent");System.Windows.Automation.AutomationProperties.SetName(SaveButton,saved?"取消收藏单词":"收藏单词"); }
        private void ShowNotice(string message)
        {
            if(windowClosed)return;ActionNotice.PlacementTarget=DeepSeekCheck.IsVisible?(UIElement)DeepSeekCheck:SyncButton;ActionNoticeText.Text=message;ActionNotice.IsOpen=true;noticeTimer.Stop();noticeTimer.Start();
        }
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
        private async void RefreshList()
        {
            if(!databaseReady||windowClosed||FavoritesList==null)return;
            int generation=++bookRevision,page=bookPage;var db=database;string filter=Filter.Text.Trim();bool only=OnlyFavorites.IsChecked==true;
            try {
                var result=await Task.Run(()=>db.Page(filter,only,page));
                if(windowClosed||generation!=bookRevision||db!=database)return;
                bookMatches=result.Matches;
                if(page>0&&page*50>=result.Matches){bookPage=Math.Max(0,(int)((result.Matches-1)/50));RefreshList();return;}
                FavoritesList.ItemsSource=result.Rows.Select(row=>new BookRow{Id=row.Id,Entry=new Entry{Word=row.Word,Pair=row.Pair},Summary=row.Summary,IsFavorite=row.IsFavorite,Sources=BookSource.ForFlags(row.HasDeepSeek,row.HasOnline)}).ToList();
                CountLabel.Text=result.Favorites+" 收藏 / "+result.Total+" 词";
                EmptyFavorites.Visibility=result.Rows.Count==0?Visibility.Visible:Visibility.Collapsed;EmptyFavorites.Text="暂无匹配词条";
                BookPageLabel.Text=(page+1)+" / "+Math.Max(1,(result.Matches+49)/50);
                BookPrevious.IsEnabled=page>0;BookNext.IsEnabled=(page+1)*50<result.Matches;
            }catch(Exception ex){if(generation==bookRevision&&!windowClosed)SetState("!","读取单词本失败："+ex.Message);}
        }
        private void BookPrevious_Click(object sender,RoutedEventArgs e){if(bookPage>0){bookPage--;RefreshList();}}
        private void BookNext_Click(object sender,RoutedEventArgs e){if((bookPage+1)*50<bookMatches){bookPage++;RefreshList();}}
        public class BookRow
        {
            public long Id {get;set;}
            public Entry Entry { get; set; }
            public string Word { get { return Entry.Word??""; } }
            public string Summary {get;set;}
            public string Translation { get { return Summary??Entry.ShortTranslation; } }
            public static string SummaryFor(IEnumerable<Entry> entries) {var preferred=entries.Where(e=>!string.IsNullOrWhiteSpace(e.ShortTranslation)).OrderByDescending(LookupSources.IsDeepSeek).ThenByDescending(CacheMerge.QueryTime).FirstOrDefault();return preferred?.ShortTranslation??"";}
            public List<BookSource> Sources {get;set;} = new List<BookSource>();
            public bool IsFavorite { get; set; }
            public string Star { get { return IsFavorite?"★":"☆"; } }
            public bool CanRemove {get{return !IsFavorite;}}
        }
        private async void BookStar_Click(object sender,RoutedEventArgs e)
        {
            e.Handled=true;var row=(sender as FrameworkElement)?.DataContext as BookRow;if(row==null||!databaseReady)return;
            var db=database;bool favorite=!row.IsFavorite;
            try {await TrackWrite(Task.Run(()=>db.SetFavorite(row.Id,favorite)));if(db!=database||windowClosed)return;MarkCacheDirty();RefreshList();if(current!=null&&current.Word.Equals(row.Word,StringComparison.OrdinalIgnoreCase)&&current.Pair==row.Entry.Pair){currentIsFavorite=favorite;SetSaved(favorite);}}
            catch(Exception ex){SetState("!","保存收藏失败："+ex.Message);}
        }
        private void BookStar_Preview(object sender,MouseButtonEventArgs e)
        {
            // Stop the row selection from navigating away before the star button receives its click.
            e.Handled=true;BookStar_Click(sender,e);
        }
        private void BookRemove_Preview(object sender,MouseButtonEventArgs e){e.Handled=true;BookRemove_Click(sender,e);}
        private async void BookRemove_Click(object sender,RoutedEventArgs e)
        {
            e.Handled=true;var row=(sender as FrameworkElement)?.DataContext as BookRow;if(row==null||!databaseReady)return;
            if(row.IsFavorite){ShowNotice("请先取消收藏，再移除该词");return;}
            string removalKey=row.Word.Trim().Normalize()+"|"+row.Entry.Pair;if(!removingWords.Add(removalKey))return;
            bool matchesQuery=string.Equals(Query.Text.Trim(),row.Word,StringComparison.OrdinalIgnoreCase)&&Translator.DetectPair(Query.Text.Trim())==row.Entry.Pair;
            var displayed=current;bool wasSaveEnabled=SaveButton.IsEnabled;
            if(matchesQuery){debounce.Stop();revision++;request?.Cancel();SaveButton.IsEnabled=false;}
            var db=database;var button=sender as Button;if(button!=null)button.SetCurrentValue(IsEnabledProperty,false);
            try {
                bool removed=false;await TrackWrite(Task.Run(()=>{removed=db.RemoveWord(row.Id);}));
                if(db!=database||windowClosed)return;
                if(!removed){ShowNotice("该词已收藏或已移除，请刷新后重试");RefreshList();return;}
                if(string.Equals(Query.Text.Trim(),row.Word,StringComparison.OrdinalIgnoreCase)&&Translator.DetectPair(Query.Text.Trim())==row.Entry.Pair){InvalidateQuery();ResultView.Document=EntryDocument.Message("","该词已从单词本移除");}
                MarkCacheDirty();RefreshList();await UpdateHistoryButtonsAsync();ShowNotice("已移除 “"+row.Word+"”");
            }catch(Exception ex){SetState("!","移除失败："+ex.Message);ShowNotice("移除失败，请重试");}
            finally{removingWords.Remove(removalKey);if(!windowClosed&&current!=null&&current==displayed&&matchesQuery)SaveButton.IsEnabled=wasSaveEnabled;if(button!=null&&!windowClosed){var binding=button.GetBindingExpression(IsEnabledProperty);if(binding!=null)binding.UpdateTarget();else button.ClearValue(IsEnabledProperty);}}
        }
        private void FavoriteMode_Changed(object sender,RoutedEventArgs e) {bookPage=0;RefreshList();}
        private Task SearchAsync(bool forceRefresh=false) { return SearchCoreAsync(forceRefresh,false); }
        private async Task SearchCoreAsync(bool forceRefresh,bool manual)
        {
            string word = Query.Text.Trim();
            if (word.Length == 0 || composing || confirmingRefresh) return;
            long beforeRead=revision;await storageTask;if(!databaseReady||windowClosed||beforeRead!=revision)return;
            string pair = Translator.DetectPair(word);var db=database;
            if(removingWords.Contains(word.Normalize()+"|"+pair))return;
            WordSelection cached=null;
            try {if(!forceRefresh)cached=await Task.Run(()=>db.Find(word,pair));}catch(Exception ex){SetState("!","读取缓存失败："+ex.Message);return;}
            if(beforeRead!=revision||db!=database||windowClosed)return;
            if(cached!=null){ShowSelection(cached,"本地缓存（Ctrl+R 可联网更新）");await RememberAsync(cached.Entry,false);return;}
            if(settings.DeepSeek.Enabled&&!manual) { SetState("","点击查询按钮使用 DeepSeek"); return; }
            if(settings.DeepSeek.Enabled&&manual&&forceRefresh)
            {
                bool existing;
                try {existing=await Task.Run(()=>db.HasDeepSeek(word,pair));}catch(Exception ex){SetState("!","读取 DeepSeek 缓存失败："+ex.Message);return;}
                if(beforeRead!=revision||db!=database||windowClosed)return;
                if(existing){bool confirmed;confirmingRefresh=true;try{confirmed=confirmDeepSeekRefresh(word);}finally{confirmingRefresh=false;}if(!confirmed)return;}
                if(beforeRead!=revision||db!=database||windowClosed)return;
            }
            InvalidateQuery();long version = revision;
            var active = new CancellationTokenSource(); request = active;
            CancellationToken token = active.Token;
            var lookupSettings=new AppSettings {Provider=settings.Provider,LibreUrl=settings.LibreUrl,ApiKey=settings.ApiKey};
            var options=new DeepSeekOptions {Enabled=settings.DeepSeek.Enabled,ApiKey=settings.DeepSeek.ApiKey,Endpoint=settings.DeepSeek.Endpoint,Model=settings.DeepSeek.Model,TimeoutSeconds=settings.DeepSeek.TimeoutSeconds,MaxTokens=settings.DeepSeek.MaxTokens};
            var lookupService=deepSeek;
            SetState("…",settings.DeepSeek.Enabled?"DeepSeek 查询中":"查询中");
            SaveButton.IsEnabled=false;
            try
            {
                if (Encoding.UTF8.GetByteCount(word) > 500) throw new InvalidOperationException("输入过长，请缩短至 500 个 UTF-8 字节以内。");
                if(options.Enabled)
                {
                    var result=await Task.Run(()=>lookupService.LookupAsync(word,options,token),token);
                    token.ThrowIfCancellationRequested();if(version!=revision)return;
                    current=result;ResultView.Document=EntryDocument.Build(result);ResultView.ScrollToHome();
                    if(!await RememberAsync(result,true))return;SaveButton.IsEnabled=storageAvailable;SetState("✓","DeepSeek 查询已更新 · "+result.LookupModel);return;
                }
                Task<DictionaryRecord> dictionaryTask = pair.StartsWith("en|") && DictionaryService.IsEnglishTerm(word) ? Task.Run(()=>DictionaryService.LookupAsync(word, token),token) : null;
                string translated;
                bool translationAvailable = true;
                string translationError = null;
                try { translated = await Task.Run(()=>Translator.TranslateConfiguredAsync(word, pair, lookupSettings, token),token); }
                catch (Exception ex) when (!token.IsCancellationRequested && dictionaryTask != null && (ex is HttpRequestException || ex is TaskCanceledException || ex is InvalidOperationException))
                { translated = "翻译暂不可用"; translationAvailable = false; translationError = ex is HttpRequestException ? "翻译服务连接失败" : ex.Message; }
                token.ThrowIfCancellationRequested();
                string note = "";
                if (dictionaryTask == null && pair.StartsWith("zh") && DictionaryService.IsEnglishTerm(translated.Trim().TrimEnd('.', '!', '?')))
                {
                    string candidate = translated.Trim().TrimEnd('.', '!', '?');
                    note = "英文词条：" + candidate;
                    dictionaryTask = Task.Run(()=>DictionaryService.LookupAsync(candidate, token),token);
                }
                DictionaryRecord record = dictionaryTask == null ? null : await dictionaryTask;
                token.ThrowIfCancellationRequested();
                if (version != revision) return;
                current = new Entry { Word = word, Translation = translated, Pair = pair, SavedAt = DateTime.Now.ToString("yyyy-MM-dd HH:mm"), Record = record, DetailNote = note, Details = record?.Format(), LookupSource="Free Dictionary / "+lookupSettings.Provider, QueriedAtUtc=DateTime.UtcNow.ToString("o"), TranslationProvider = lookupSettings.Provider, TranslationUrl = lookupSettings.Provider == "LibreTranslate" ? lookupSettings.LibreUrl : "https://mymemory.translated.net/" };
                ResultView.Document = EntryDocument.Build(current); ResultView.ScrollToHome();
                SaveButton.IsEnabled = false;
                if(record != null && record.Meanings.Count > 0)
                {
                    SetState("…", "正在逐条翻译中文释义");
                    var entry=current;
                    var translatedRecord=await Task.Run(async ()=> { var copy=JsonFiles.Serializer().Deserialize<DictionaryRecord>(JsonFiles.Serializer().Serialize(record)); await SenseTranslator.TranslateAsync(copy,lookupSettings,token); return copy; },token);
                    token.ThrowIfCancellationRequested();
                    if(version!=revision) return;
                    record=translatedRecord; current.Record=record;
                    current.Details=record.Format();EntryDocument.Refresh(ResultView,current);
                }
                SaveButton.IsEnabled = storageAvailable && (translationAvailable || (record != null && record.Meanings.Count > 0));
                if(translationAvailable || record?.Meanings.Count>0) { current.SuccessfulQueryAtUtc=DateTime.UtcNow.ToString("o"); if(!await RememberAsync(current,true))return; }
                bool missingChinese=record!=null && record.Meanings.SelectMany(m=>m.Senses).Any(s=>s.ChineseFailed);
                SetState(translationAvailable && !missingChinese ? "✓" : "!", missingChinese ? "部分中文释义未获取，可稍后重新查询" : translationAvailable ? "查询完成" : translationError ?? "翻译服务暂不可用，词典资料已显示");

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
        private async Task TrackWrite(Task task)
        {
            pendingWrites.Add(task);try{await task;}finally{pendingWrites.Remove(task);}
        }
        private async void Save_Click(object sender,RoutedEventArgs e)
        {
            var entry=current;var db=database;if(entry==null||!databaseReady||!SaveButton.IsEnabled)return;SaveButton.IsEnabled=false;
            try {
                bool favorite=false;
                await TrackWrite(Task.Run(()=>{favorite=!db.IsFavorite(entry.Word,entry.Pair??Translator.DetectPair(entry.Word));db.Save(entry,favorite);}));
                if(db!=database||windowClosed)return;MarkCacheDirty();RefreshList();
                if(current==entry){currentIsFavorite=favorite;SetSaved(favorite);SetState("✓",favorite?"已收藏":"已取消收藏");}
            }catch(Exception ex){SetState("!","收藏保存失败："+ex.Message);}
            finally{if(!windowClosed&&current==entry)SaveButton.IsEnabled=storageAvailable;}
        }
        private async void Favorite_Selected(object sender,SelectionChangedEventArgs e)
        {
            var row=FavoritesList.SelectedItem as BookRow;if(row==null||!databaseReady)return;long version=revision;var db=database;
            try {var selected=await Task.Run(()=>db.Get(row.Id));if(version!=revision||db!=database||windowClosed||selected==null)return;ShowSelection(selected,"本地单词本");await RememberAsync(selected.Entry,false);}
            catch(Exception ex){SetState("!","读取释义失败："+ex.Message);}
        }
        private void Tabs_Changed(object sender, SelectionChangedEventArgs e)
        {
            if (e.Source != MainTabs || Query == null) return;
            if (MainTabs.SelectedIndex == 0) Query.Focus();
        }
        private void Filter_Changed(object sender, TextChangedEventArgs e) {bookPage=0;bookRevision++;bookFilterTimer.Stop();bookFilterTimer.Start();}
        private void Copy_Click(object sender, RoutedEventArgs e)
        {
            if (current == null) return;
            try { Clipboard.SetText(new System.Windows.Documents.TextRange(ResultView.Document.ContentStart, ResultView.Document.ContentEnd).Text); SetState("✓", "已复制"); }
            catch (Exception ex) { SetState("!", ex.Message); }
        }
        private async void Export_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new SaveFileDialog { Filter = "CSV 文件 (*.csv)|*.csv", FileName = "我的词本.csv" };
            if (dialog.ShowDialog() != true) return;
            try {var db=database;if(!databaseReady)return;await Task.Run(()=>db.ExportFavoritesCsv(dialog.FileName));}
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "导出失败"); }
        }
        private void ApplySettings()
        {
            EntryDocument.Settings = settings;
            debounce.Interval = TimeSpan.FromMilliseconds(settings.DelayMs);
            suppressDeepSeekToggle=true;DeepSeekCheck.IsChecked=settings.DeepSeek.Enabled;suppressDeepSeekToggle=false;
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
        private async void ReloadSettings()
        {
            settingsReload.Stop();
            try {
                var updated=SettingsStore.Load();ThemeStyles.ValidateStyles(updated.Styles);
                bool userChanged=updated.username!=settings.username;
                bool sourceChanged=updated.Provider!=settings.Provider||updated.DeepSeek.Enabled!=settings.DeepSeek.Enabled||updated.DeepSeek.Model!=settings.DeepSeek.Model||updated.DeepSeek.Endpoint!=settings.DeepSeek.Endpoint||updated.DeepSeek.ApiKey!=settings.DeepSeek.ApiKey;
                if(userChanged){InvalidateQuery();databaseReady=false;bookRevision++;FavoritesList.ItemsSource=null;current=null;}
                settings=updated;EntryDocument.Settings=updated;ThemeStyles.ApplyStyles(updated.Styles);ApplySettings();
                if(userChanged){storageTask=InitializeStorageAsync();await storageTask;if(databaseReady)await PullCacheAsync();}
                if(sourceChanged){InvalidateQuery();if(!string.IsNullOrWhiteSpace(Query.Text))debounce.Start();}
            }catch(Exception ex){SetState("!","设置重载失败："+ex.Message);}
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
        private async void Search_Click(object sender,RoutedEventArgs e)
        {
            await SearchCoreAsync(true,true);
        }
        private void DeepSeek_Changed(object sender,RoutedEventArgs e)
        {
            if(suppressDeepSeekToggle || ResultView==null)return;
            bool enabled=DeepSeekCheck.IsChecked==true,previous=settings.DeepSeek.Enabled;InvalidateQuery();settings.DeepSeek.Enabled=enabled;
            try {SettingsStore.SetDeepSeekEnabled(enabled);}catch(Exception ex){settings.DeepSeek.Enabled=previous;suppressDeepSeekToggle=true;DeepSeekCheck.IsChecked=previous;suppressDeepSeekToggle=false;SetState("!","DeepSeek 开关保存失败："+ex.Message);return;}
            if(!string.IsNullOrWhiteSpace(Query.Text))debounce.Start();
            string notice=enabled?"已开启 DeepSeek查词":"已关闭 DeepSeek查词";SetState("✓",notice);ShowNotice(notice);
        }
        private async void WordLookup(object sender,RoutedEventArgs e)
        {
            var args=e as EntryDocument.WordLookupArgs;
            if(args==null || string.IsNullOrWhiteSpace(args.Word))return;
            InvalidateQuery(); suppressInput=true; Query.Text=args.Word; suppressInput=false;
            MainTabs.SelectedIndex=0; e.Handled=true;
            await SearchCoreAsync(false,true);
        }
        private async Task UpdateHistoryButtonsAsync()
        {
            if(!databaseReady||windowClosed)return;var db=database;var history=await Task.Run(()=>db.History(0,false));if(db!=database||windowClosed)return;BackButton.IsEnabled=history.CanBack;ForwardButton.IsEnabled=history.CanForward;
        }
        private void MarkCacheDirty(){dataRevision++;deferredSave.Mark(DateTime.UtcNow);if(!syncBusy)SetSyncButton("待同步","#5C4BB7");}
        private void SetSyncButton(string text,string color){if(SyncButton!=null){SyncButton.Content=text;SyncButton.Background=ThemeStyles.BrushFor(color);}}
        private async void Sync_Click(object sender,RoutedEventArgs e){if(manualSyncRequested||!databaseReady)return;manualSyncRequested=true;try{await FlushCoreAsync(true);}finally{manualSyncRequested=false;}}
        private void DismissSyncError_Click(object sender,RoutedEventArgs e){SyncErrorPopup.IsOpen=false;}
        private async Task InitializeStorageAsync()
        {
            int generation=++storageGeneration;databaseReady=false;var configuration=settings;bookPage=0;CountLabel.Text="加载中…";
            ResultView.Document=EntryDocument.Message("","输入中文或英文");
            string profile=Path.Combine(Path.GetDirectoryName(SettingsStore.FilePath),"profiles",configuration.username);
            try {
                var db=await Task.Run(()=>{
                    var value=WordDatabase.OpenShared(profile,configuration.username);
                    string lookup=Path.Combine(profile,"lookup-cache.json"),favorite=Path.Combine(profile,"favorites.json");
                    if(!File.Exists(lookup)&&!File.Exists(favorite)&&value.Page("",false,0,1).Total==0){
                        string ownerPath=Path.Combine(Path.GetDirectoryName(SettingsStore.FilePath),".legacy-cache-owner");
                        if(!File.Exists(ownerPath))File.WriteAllText(ownerPath,configuration.username,new UTF8Encoding(false));
                        if(File.ReadAllText(ownerPath,Encoding.UTF8)==configuration.username){lookup=LookupCache.DefaultPath;favorite=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"MyDict","favorites.json");}
                    }
                    value.MigrateLegacy(lookup,favorite);return value;
                });
                if(generation!=storageGeneration||windowClosed)return;
                database=db;profileDirectory=profile;databaseReady=true;cacheWritable=storageAvailable=settingsValid;
                RefreshList();var history=await Task.Run(()=>db.History());if(generation!=storageGeneration||windowClosed)return;
                BackButton.IsEnabled=history.CanBack;ForwardButton.IsEnabled=history.CanForward;
                if(current==null&&string.IsNullOrWhiteSpace(Query.Text)&&history.Current!=null)ShowSelection(history.Current,"已恢复上次查词");
                if(await Task.Run(()=>db.PendingSync))MarkCacheDirty();
            }catch(Exception ex){if(generation==storageGeneration&&!windowClosed){cacheWritable=storageAvailable=false;CountLabel.Text="数据库不可用";SetState("!","SQLite 加载失败，旧数据已保留："+ex.Message);}}
        }
        private GiteeCacheSync CreateCacheSync()
        {
            string repositoryId;using(var hash=System.Security.Cryptography.SHA256.Create())repositoryId=BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(settings.CacheSync.Repository))).Replace("-","").Substring(0,16);
            return new GiteeCacheSync(Path.Combine(Path.GetDirectoryName(SettingsStore.FilePath),"cache-sync",repositoryId),settings.CacheSync);
        }
        private async Task PullCacheAsync(){if(databaseReady&&settings.CacheSync.Enabled)await ExchangeDatabaseAsync(false);}
        private Task FlushPendingAsync(){return FlushCoreAsync(false);}
        private async Task FlushCoreAsync(bool manual)
        {
            if(!databaseReady||syncBusy||windowClosed)return;
            if(!manual&&!settings.CacheSync.Enabled){deferredSave.Clear();return;}
            await ExchangeDatabaseAsync(true);
        }
        private async Task ExchangeDatabaseAsync(bool upload)
        {
            if(syncBusy||!databaseReady)return;var db=database;syncBusy=true;SyncButton.IsEnabled=false;SetSyncButton("同步中…","#8A7BBA");
            try {
                await exchangeDatabase(db,upload);
                if(db!=database||windowClosed)return;
                bool pending=await Task.Run(()=>db.PendingSync);
                if(pending){deferredSave.Clear();deferredSave.Mark(DateTime.UtcNow);}else deferredSave.Clear();
                SetSyncButton(pending?"待同步":upload?"已同步":"同步",pending?"#5C4BB7":upload?"#27834A":"#5C4BB7");
                SyncErrorPopup.IsOpen=false;RefreshList();
            }catch(Exception ex){if(db==database&&!windowClosed){SetSyncButton("同步失败","#C53939");SetState("!",ex.Message);SyncErrorText.Text=ex.Message+"\n本地 SQLite 数据已保存，可点击重试。";if(upload&&IsLoaded)SyncErrorPopup.IsOpen=true;deferredSave.Clear();deferredSave.Mark(DateTime.UtcNow);}}
            finally{syncBusy=false;if(!windowClosed)SyncButton.IsEnabled=true;}
        }
        private async void FlushOnClosing(object sender,System.ComponentModel.CancelEventArgs e)
        {
            if(closingAfterFlush)return;
            SaveWindowLayout();InvalidateQuery();
            if(pendingWrites.Count==0)return;
            e.Cancel=true;if(closingInProgress)return;closingInProgress=true;
            try{await Task.WhenAll(pendingWrites.ToArray());closingAfterFlush=true;_ = Dispatcher.BeginInvoke(new Action(Close));}
            catch(Exception ex){closingInProgress=false;SetState("!","数据写入失败："+ex.Message);}
        }
        private async Task<bool> RememberAsync(Entry entry,bool save)
        {
            var db=database;try {
                if(save){await TrackWrite(Task.Run(()=>db.Save(entry,null,true)));if(db!=database||windowClosed)return false;MarkCacheDirty();}
                else await TrackWrite(Task.Run(()=>db.Visit(entry)));
                if(db!=database||windowClosed)return false;
                if(current==entry){currentIsFavorite=await Task.Run(()=>db.IsFavorite(entry.Word,entry.Pair??Translator.DetectPair(entry.Word)));SetSaved(currentIsFavorite);}
                await UpdateHistoryButtonsAsync();if(save)RefreshList();return true;
            }catch(Exception ex){if(!windowClosed)SetState("!","数据库保存失败："+ex.Message);return false;}
        }
        private void ShowSelection(WordSelection selected,string hint)
        {
            var entry=selected.Entry;InvalidateQuery();suppressInput=true;Query.Text=entry.Word;suppressInput=false;current=entry;currentIsFavorite=selected.IsFavorite;
            ResultView.Document=EntryDocument.Build(entry);ResultView.ScrollToHome();SaveButton.IsEnabled=storageAvailable;SetSaved(currentIsFavorite);MainTabs.SelectedIndex=0;SetState("✓",hint);
        }
        private async void NavigateHistory(int delta)
        {
            if(!databaseReady)return;var db=database;long version=revision;var history=await Task.Run(()=>db.History(delta));if(db!=database||version!=revision||windowClosed)return;
            if(history.Current!=null)ShowSelection(history.Current,"历史查词 · SQLite");BackButton.IsEnabled=history.CanBack;ForwardButton.IsEnabled=history.CanForward;
        }
        private void Back_Click(object sender,RoutedEventArgs e){NavigateHistory(-1);}
        private void Forward_Click(object sender,RoutedEventArgs e){NavigateHistory(1);}
        private async void HistoryKeys(object sender,KeyEventArgs e)
        {
            if(e.Key==Key.Enter&&Query.IsKeyboardFocusWithin&&!composing&&Keyboard.Modifiers==ModifierKeys.None) {e.Handled=true;Search_Click(sender,e);return;}
            if(AdjustReading(e.Key,Keyboard.Modifiers)) {e.Handled=true;return;}
            if(Keyboard.Modifiers==ModifierKeys.Alt && (e.SystemKey==Key.Left || e.SystemKey==Key.Right)) {NavigateHistory(e.SystemKey==Key.Left?-1:1);e.Handled=true;}
            else if(Keyboard.Modifiers==ModifierKeys.Control && e.Key==Key.R) {InvalidateQuery();e.Handled=true;await SearchAsync(true);}
        }
        private bool AdjustReading(Key key,ModifierKeys modifiers)
        {
            if(modifiers!=ModifierKeys.Control&&modifiers!=(ModifierKeys.Control|ModifierKeys.Shift))return false;
            bool font=key==Key.OemPlus||key==Key.Add||key==Key.OemMinus||key==Key.Subtract;
            bool leading=key==Key.OemOpenBrackets||key==Key.OemCloseBrackets;
            if(!font&&!leading)return false;
            double direction=key==Key.OemMinus||key==Key.Subtract||key==Key.OemOpenBrackets?-1:1;
            string scaleKey=font?"number.doc.scale":"number.doc.leading_scale";
            double previous=ThemeStyles.Number(scaleKey,1),next=Math.Max(0.65,Math.Min(2.2,previous+direction*(font?0.05:0.1))),ratio=next/previous;
            var changes=new System.Collections.Generic.Dictionary<string,string>();
            changes[scaleKey]=next.ToString(System.Globalization.CultureInfo.InvariantCulture);
            foreach(string name in new[]{"number.book.word_size","number.book.translation_size","number.book.word_line_height","number.book.translation_line_height"})
                if(font||name.Contains("line_height"))changes[name]=(ThemeStyles.Number(name)*ratio).ToString(System.Globalization.CultureInfo.InvariantCulture);
            try {SettingsStore.PatchStyles(changes);settings.Styles=SettingsStore.Load().Styles;ThemeStyles.ApplyStyles(settings.Styles);}
            catch(Exception ex){SetState("!","保存阅读样式失败："+ex.Message);}
            return true;
        }
        private void About_Click(object sender, RoutedEventArgs e) { new AboutWindow { Owner = this }.ShowDialog(); }
        private void DragWindow(object sender, MouseButtonEventArgs e) { if (e.ClickCount == 2) Maximize_Click(sender, e); else if (e.LeftButton == MouseButtonState.Pressed) DragMove(); }
        private void Minimize_Click(object sender, RoutedEventArgs e) { WindowState = WindowState.Minimized; }
        private void Maximize_Click(object sender, RoutedEventArgs e) { WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized; }
        private void Close_Click(object sender, RoutedEventArgs e) { Close(); }
    }
}



















