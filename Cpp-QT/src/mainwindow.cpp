#include "mainwindow.h"
#include "entryrenderer.h"
#include "services.h"
#include "sync.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QApplication>
#include <QBoxLayout>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QCheckBox>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QScrollBar>
#include <QMenu>
#include <QShortcut>
#include <QScreen>
#include <QWindow>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QDirIterator>
#include <QSvgRenderer>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QDesktopServices>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSizeGrip>
#include <QSignalBlocker>
#include <QAbstractButton>
#include <functional>
#include <exception>
#include <stdexcept>
#include <cmath>

namespace MyDict {
namespace {
template<class T> struct AsyncResult { T value{}; QString error; };
template<class Work, class Done>
void background(QObject *owner, Work work, Done done, std::function<void(QString)> failed) {
    using T = decltype(work());
    const auto generation=owner->property("storageGeneration").toULongLong();
    auto *watcher = new QFutureWatcher<AsyncResult<T>>(owner);
    QObject::connect(watcher, &QFutureWatcher<AsyncResult<T>>::finished, owner, [owner,generation,watcher, done, failed] {
        auto result = watcher->result(); watcher->deleteLater();
        if(owner->property("storageGeneration").toULongLong()!=generation)return;
        if (!result.error.isEmpty()) failed(result.error); else done(result.value);
    });
    watcher->setFuture(QtConcurrent::run([work]() mutable {
        AsyncResult<T> result;
        try { result.value = work(); }
        catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
        catch (...) { result.error = QStringLiteral("操作未完成，请重试。"); }
        return result;
    }));
}
QString identity(QString word, QString pair) { return normalizeWord(word) + "|" + pair; }
QIcon vectorIcon(const QString &name, const QColor &color, int size = 24) {
    QPixmap pixmap(size * 2, size * 2); pixmap.setDevicePixelRatio(2); pixmap.fill(Qt::transparent);
    QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing); p.scale(size / 24., size / 24.);
    p.setPen(QPen(color, 2.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    if (name == "back") { p.drawLine(14, 5, 7, 12); p.drawLine(7, 12, 14, 19); }
    else if (name == "forward") { p.drawLine(10, 5, 17, 12); p.drawLine(17, 12, 10, 19); }
    else if (name == "close") { p.drawLine(6, 6, 18, 18); p.drawLine(18, 6, 6, 18); }
    else if (name == "minimize") p.drawLine(5, 16, 19, 16);
    else if (name == "maximize") p.drawRect(QRectF(5, 5, 14, 14));
    else if (name == "copy") { p.drawRoundedRect(QRectF(8, 8, 12, 13), 1, 1); p.drawPolyline(QPolygonF({QPointF(16,4),QPointF(4,4),QPointF(4,17)})); }
    else if (name == "about") { p.drawEllipse(QRectF(3, 3, 18, 18)); p.drawLine(12, 11, 12, 17); p.drawPoint(12, 7); }
    else if (name == "online") { p.drawEllipse(QRectF(3, 3, 18, 18)); p.drawEllipse(QRectF(8, 3, 8, 18)); p.drawLine(3, 12, 21, 12); }
    else if (name == "star" || name == "star-filled") {
        QPolygonF star; for (int i = 0; i < 10; ++i) { double a = (i * 36 - 90) * 3.141592653589793 / 180; double r = i % 2 ? 4.6 : 9.; star << QPointF(12 + r * std::cos(a), 12 + r * std::sin(a)); }
        if (name == "star-filled") p.setBrush(color); p.drawPolygon(star);
    } else if (name == "whale") {
        p.end(); QFile file(":/deepseek.svg");
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray svg = file.readAll();
            svg.replace("#4D6BFE", color.name().toUtf8()).replace("#4d6bfe", color.name().toUtf8()).replace("#536af5", color.name().toUtf8()).replace("currentColor", color.name().toUtf8());
            QSvgRenderer renderer(svg); QPainter svgPainter(&pixmap); renderer.render(&svgPainter, QRectF(1, 1, size - 2, size - 2));
        }
        return QIcon(pixmap);
    }
    return QIcon(pixmap);
}
QPoint globalPoint(QMouseEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->globalPosition().toPoint();
#else
    return e->globalPos();
#endif
}
void loadLocalFonts(const QString &directory) {
    QDirIterator it(directory + "/fonts", {"*.ttf", "*.otf", "*.ttc"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) QFontDatabase::addApplicationFont(it.next());
}
Settings loadedSettings(QString directory){Settings s(directory);s.load();return s;}
}

MainWindow::MainWindow(QString dataDirectory, QWidget *parent)
    : QMainWindow(parent), settings(loadedSettings(dataDirectory)), database(settings.databasePath(), settings.username()), activeUsername(settings.username()) {
    QDir().mkpath(QFileInfo(database.path()).absolutePath());
    profileLock=std::make_shared<QLockFile>(QFileInfo(database.path()).absolutePath()+"/app.lock");
    profileLock->setStaleLockTime(0);
    if(!profileLock->tryLock(0))throw std::runtime_error("该用户词库正在被另一程序或导入工具使用，请先关闭后重试。");
    setObjectName("MyDictWindow"); setWindowTitle(QStringLiteral("拾词 MyDict"));
    setWindowIcon(QIcon(":/app-icon.png"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    loadLocalFonts(settings.directory);
    if (settings.directory != QCoreApplication::applicationDirPath()) loadLocalFonts(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MAC
    loadLocalFonts(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../Resources"));
#endif
    createUi(); applySettings(true);
    online = new OnlineService;
    online->moveToThread(&networkThread);
    connect(&networkThread, &QThread::finished, online, &QObject::deleteLater);
    connect(this, &MainWindow::onlineLookup, online, &OnlineService::lookup, Qt::QueuedConnection);
    connect(this, &MainWindow::cancelOnline, online, &OnlineService::cancel, Qt::QueuedConnection);
    connect(online, &OnlineService::ready, this, &MainWindow::receiveEntry);
    connect(online, &OnlineService::failed, this, [this](quint64 id, QString message) {
        if (id != requestId) return; query->setText(QStringLiteral("查询")); toast(message, true);
        if (currentEntry.isEmpty()) browser->setHtml(EntryRenderer::message(QStringLiteral("暂未查到结果"), message, settings.data));
    });
    networkThread.start();
    configWatcher = new QFileSystemWatcher(this); configWatcher->addPath(settings.directory);
    if (QFile::exists(settings.path)) configWatcher->addPath(settings.path);
    connect(configWatcher, &QFileSystemWatcher::fileChanged, this, [this] { reloadDelay->start(180); });
    connect(configWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { reloadDelay->start(180); });
    connect(reloadDelay, &QTimer::timeout, this, &MainWindow::reloadConfig);
    qApp->installEventFilter(this);
    QTimer::singleShot(0, this, &MainWindow::initialize);
}
MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    if(syncCanceled)syncCanceled->store(true);
    emit cancelOnline(); networkThread.quit(); networkThread.wait();
}

void MainWindow::createUi() {
    auto *root = new QWidget(this); root->setObjectName("root"); setCentralWidget(root);
    auto *layout = new QVBoxLayout(root); layout->setContentsMargins(1,1,1,1); layout->setSpacing(0);
    titleBar = new QWidget(root); titleBar->setObjectName("titleBar"); titleBar->setFixedHeight(42);
    auto *title = new QHBoxLayout(titleBar); title->setContentsMargins(7,0,4,0); title->setSpacing(2);
    lookupTab = new QPushButton(QStringLiteral("查词")); lookupTab->setObjectName("lookupTab"); lookupTab->setCheckable(true); lookupTab->setChecked(true);
    bookTab = new QPushButton(QStringLiteral("单词本")); bookTab->setObjectName("bookTab"); bookTab->setCheckable(true);
    lookupTab->setProperty("tab", true); bookTab->setProperty("tab", true);
    title->addWidget(lookupTab); title->addWidget(bookTab); title->addStretch();
    sync = new QPushButton(QStringLiteral("同步")); sync->setObjectName("syncButton"); sync->setFixedHeight(28); title->addWidget(sync);
    auto titleButton = [title](const QString &name, const QString &label) {
        auto *button = new QPushButton; button->setObjectName(name); button->setAccessibleName(label); button->setIcon(vectorIcon(name, Qt::white)); button->setFixedSize(30,32); title->addWidget(button); return button;
    };
    auto *about = titleButton("about", QStringLiteral("关于拾词"));
    auto *minimize = titleButton("minimize", QStringLiteral("最小化"));
    auto *maximize = titleButton("maximize", QStringLiteral("最大化或还原"));
    auto *close = titleButton("close", QStringLiteral("关闭"));
    connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(maximize, &QPushButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(about, &QPushButton::clicked, this, [this] {
        QMessageBox box(QMessageBox::Information, QStringLiteral("关于拾词"), QStringLiteral("拾词 MyDict\n版本 ")+QStringLiteral(MYDICT_VERSION)+QStringLiteral(" · C++ / Qt\n作者 Yance Feng & Codex\n\n中英互译、跨平台共享单词本\nMIT License\nQt 为动态链接库，许可见 QT-NOTICES.md"), QMessageBox::Ok, this);
        box.setIconPixmap(windowIcon().pixmap(64,64)); box.exec();
    });
    layout->addWidget(titleBar);
    pages = new QStackedWidget; pages->setObjectName("pages"); layout->addWidget(pages, 1);
    auto *lookupPage = new QWidget; auto *lookupLayout = new QVBoxLayout(lookupPage); lookupLayout->setContentsMargins(10,5,10,7); lookupLayout->setSpacing(4);
    auto *bar = new QHBoxLayout; bar->setSpacing(4);
    auto makeButton = [this](QString name, QString accessible, QString icon) {
        auto *button = new QPushButton; button->setObjectName(name); button->setAccessibleName(accessible); button->setIcon(vectorIcon(icon, QColor("#5C4BB7"))); button->setFixedSize(30,32); return button;
    };
    back = makeButton("backButton", QStringLiteral("上一条查询"), "back"); forward = makeButton("forwardButton", QStringLiteral("下一条查询"), "forward");
    back->setEnabled(false); forward->setEnabled(false); bar->addWidget(back); bar->addWidget(forward);
    input = new QLineEdit; input->setObjectName("queryInput"); input->setPlaceholderText(QStringLiteral("输入中文或英文")); input->setClearButtonEnabled(true); input->setMinimumWidth(120); input->setMinimumHeight(34); input->setEnabled(false); bar->addWidget(input, 1);
    deepSeek = makeButton("deepSeekButton", QStringLiteral("切换 DeepSeek 查词"), "whale"); deepSeek->setCheckable(true); deepSeek->setFixedWidth(34); bar->addWidget(deepSeek);
    query = new QPushButton(QStringLiteral("查询")); query->setObjectName("queryButton"); query->setMinimumWidth(54); query->setFixedHeight(34); query->setEnabled(false); bar->addWidget(query);
    star = makeButton("favoriteButton", QStringLiteral("收藏或取消收藏当前词"), "star"); star->setEnabled(false); bar->addWidget(star);
    copy = makeButton("copyButton", QStringLiteral("复制当前释义"), "copy"); copy->setEnabled(false); bar->addWidget(copy);
    lookupLayout->addLayout(bar);
    browser = new EntryBrowser; browser->setObjectName("entryBrowser"); browser->document()->setDocumentMargin(6); browser->setHtml(EntryRenderer::message(QStringLiteral("拾词"), QStringLiteral("输入中文或英文，即可开始查询。"), settings.data)); lookupLayout->addWidget(browser,1);
    pages->addWidget(lookupPage);
    auto *bookPage = new QWidget; auto *bookLayout = new QVBoxLayout(bookPage); bookLayout->setContentsMargins(10,5,10,5); bookLayout->setSpacing(4);
    auto *bookBar = new QHBoxLayout; bookBar->setSpacing(6);
    filter = new QLineEdit; filter->setObjectName("bookFilter"); filter->setPlaceholderText(QStringLiteral("筛选单词或释义")); filter->setClearButtonEnabled(true); filter->setMinimumHeight(32); bookBar->addWidget(filter,1);
    favoritesOnly = new QCheckBox(QStringLiteral("仅收藏")); favoritesOnly->setObjectName("favoritesOnly"); bookBar->addWidget(favoritesOnly);
    auto *exportButton = new QPushButton(QStringLiteral("导出")); exportButton->setObjectName("exportButton"); bookBar->addWidget(exportButton);
    auto *more = new QPushButton(QStringLiteral("⋯")); more->setObjectName("bookMore"); more->setFixedWidth(30); auto *menu = new QMenu(more); menu->addAction(QStringLiteral("导入词库文件…"), this, &MainWindow::importBook); more->setMenu(menu); bookBar->addWidget(more);
    bookLayout->addLayout(bookBar);
    book = new QListWidget; book->setObjectName("wordbook"); book->setSpacing(1); book->setUniformItemSizes(false); book->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); book->setFrameShape(QFrame::NoFrame); bookLayout->addWidget(book,1);
    emptyBook = new QLabel(QStringLiteral("暂无单词"), book); emptyBook->setObjectName("emptyBook"); emptyBook->setAlignment(Qt::AlignCenter); emptyBook->setAttribute(Qt::WA_TransparentForMouseEvents); emptyBook->hide();
    auto *pager = new QHBoxLayout; pager->addStretch(); previous = new QPushButton; previous->setObjectName("bookPrevious"); previous->setIcon(vectorIcon("back", QColor("#5C4BB7"))); previous->setFixedSize(28,26); pageLabel = new QLabel("1 / 1"); pageLabel->setObjectName("bookPageLabel"); next = new QPushButton; next->setObjectName("bookNext"); next->setIcon(vectorIcon("forward", QColor("#5C4BB7"))); next->setFixedSize(28,26); pager->addWidget(previous); pager->addWidget(pageLabel); pager->addWidget(next); bookLayout->addLayout(pager);
    pages->addWidget(bookPage);
    notice = new QLabel(root); notice->setObjectName("notice"); notice->setWordWrap(true); notice->setAlignment(Qt::AlignCenter); notice->setAttribute(Qt::WA_TransparentForMouseEvents); notice->hide();
    auto *grip = new QSizeGrip(root); grip->setFixedSize(12,12); auto *bottom = new QHBoxLayout; bottom->setContentsMargins(0,0,1,0); bottom->addStretch(); bottom->addWidget(grip); layout->addLayout(bottom);
    inputDelay = new QTimer(this); inputDelay->setSingleShot(true);
    filterDelay = new QTimer(this); filterDelay->setSingleShot(true); filterDelay->setInterval(200);
    noticeTimer = new QTimer(this); noticeTimer->setSingleShot(true); noticeTimer->setInterval(3000);
    reloadDelay = new QTimer(this); reloadDelay->setSingleShot(true);
    syncTimer = new QTimer(this); syncTimer->setInterval(10000); syncTimer->start();
    connect(input, &QLineEdit::textChanged, this, [this] { ++requestId; emit cancelOnline(); inputDelay->start(qBound(300, settings.data.value("DelayMs").toInt(650), 3000)); query->setText(QStringLiteral("查询")); });
    connect(inputDelay, &QTimer::timeout, this, [this] { lookup(false,false); });
    connect(query, &QPushButton::clicked, this, [this] { lookup(true,true); });
    connect(input, &QLineEdit::returnPressed, this, [this] { lookup(true,true); });
    connect(deepSeek, &QPushButton::clicked, this, [this](bool checked) {
        auto ai = settings.data.value("DeepSeek").toObject(); ai.insert("Enabled",checked); settings.data.insert("DeepSeek",ai);
        try { settings.save(); } catch (const std::exception &e) { toast(QString::fromUtf8(e.what()), true); }
        ++requestId; emit cancelOnline(); updateDeepSeek(); toast(checked ? QStringLiteral("已开启 DeepSeek查词") : QStringLiteral("已关闭 DeepSeek查词"));
    });
    connect(star, &QPushButton::clicked, this, &MainWindow::favoriteCurrent);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(browser->toPlainText()); toast(QStringLiteral("已复制释义")); });
    connect(back, &QPushButton::clicked, this, [this] { navigate(-1); }); connect(forward, &QPushButton::clicked, this, [this] { navigate(1); });
    connect(lookupTab, &QPushButton::clicked, this, [this] { pages->setCurrentIndex(0); lookupTab->setChecked(true); bookTab->setChecked(false); input->setFocus(); });
    connect(bookTab, &QPushButton::clicked, this, [this] { pages->setCurrentIndex(1); lookupTab->setChecked(false); bookTab->setChecked(true); refreshBook(); });
    connect(filter, &QLineEdit::textChanged, this, [this] { offset=0; filterDelay->start(); }); connect(filterDelay, &QTimer::timeout, this, &MainWindow::refreshBook);
    connect(favoritesOnly, &QCheckBox::toggled, this, [this] { offset=0; refreshBook(); });
    connect(previous, &QPushButton::clicked, this, [this] { offset=qMax(0,offset-50); refreshBook(); }); connect(next, &QPushButton::clicked, this, [this] { if(offset+50<total) {offset+=50;refreshBook();} });
    connect(book, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!initialized) return; auto db=database; auto id=item->data(Qt::UserRole).toLongLong(); ++requestId; emit cancelOnline(); auto revision=requestId;
        ++pendingWrites;
        background(this,[db,id]()mutable{auto entry=db.get(id);if(!entry.isEmpty())db.store(entry,true);return entry;},[this,revision](QJsonObject entry){writeFinished();if(revision!=requestId||entry.isEmpty())return; showEntry(entry);updateHistory();pages->setCurrentIndex(0); lookupTab->setChecked(true);bookTab->setChecked(false);QSignalBlocker block(input);input->setText(entry.value("Word").toString());},[this](QString e){writeFinished();toast(e,true);});
    });
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportBook);
    connect(sync, &QPushButton::clicked, this, [this] { runSync(false); });
    connect(noticeTimer, &QTimer::timeout, notice, &QWidget::hide);
    connect(browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme()=="word") { QString word=QUrl::fromPercentEncoding(url.toString(QUrl::FullyEncoded).mid(5).toLatin1()); {QSignalBlocker block(input); input->setText(word);} lookup(false,false,true); }
        else if(url.scheme()=="pos") { selectedPart=QUrl::fromPercentEncoding(url.toString(QUrl::FullyEncoded).mid(4).toLatin1()); browser->setHtml(EntryRenderer::render(currentEntry,settings.data,selectedPart)); }
        else if(url.scheme()=="https"||url.scheme()=="http") QDesktopServices::openUrl(url);
    });
    connect(syncTimer, &QTimer::timeout, this, [this] { if(!firstDirtyAt||syncing||!settings.data.value("CacheSync").toObject().value("Enabled").toBool())return;if(QDateTime::currentMSecsSinceEpoch()-firstDirtyAt>=1800000)runSync(); });
    auto shortcut = [this](const QKeySequence &key, std::function<void()> action) { auto *s=new QShortcut(key,this);s->setContext(Qt::WindowShortcut);connect(s,&QShortcut::activated,this,action); };
    shortcut(QKeySequence("Alt+Left"),[this]{navigate(-1);});shortcut(QKeySequence("Alt+Right"),[this]{navigate(1);});shortcut(QKeySequence("Ctrl+R"),[this]{lookup(true,false);});
    shortcut(QKeySequence("Ctrl++"),[this]{adjustTypography(.05,0);});shortcut(QKeySequence("Ctrl+="),[this]{adjustTypography(.05,0);});shortcut(QKeySequence("Ctrl+-"),[this]{adjustTypography(-.05,0);});shortcut(QKeySequence("Ctrl+["),[this]{adjustTypography(0,-.1);});shortcut(QKeySequence("Ctrl+]"),[this]{adjustTypography(0,.1);});
}

void MainWindow::initialize() {
    auto db=database;
    struct Initial {QJsonObject entry;bool dirty=false;};
    background(this,[db]() mutable {db.initialize();return Initial{db.navigate(0),db.revision()>db.syncedRevision()};},[this](Initial initial) {
        initialized=true;input->setEnabled(true);query->setEnabled(true);input->setFocus();updateHistory();
        if(initial.dirty)dirty();
        if(!initial.entry.isEmpty()&&input->text().isEmpty()){QSignalBlocker block(input);input->setText(initial.entry.value("Word").toString());showEntry(initial.entry);}
        if(pages->currentIndex()==1)refreshBook();
        if(settings.data.value("CacheSync").toObject().value("Enabled").toBool(false))runSync(true);
    },[this](QString e) {toast(e,true);browser->setHtml(EntryRenderer::message(QStringLiteral("无法打开词库"),e,settings.data));});
}

void MainWindow::applySettings(bool restoreWindow) {
    auto value=[this](QString key,QString fallback){return styleValue(settings.data,key,fallback);};
    auto validColor=[&](QString key,QString fallback){QColor c(value(key,fallback));return c.isValid()?c.name():fallback;};
    const QString purple=validColor("color.c30234d","#30234D"), foreground=validColor("color.c272438","#272438"), backgroundColor=validColor("color.cf5f3fa","#F5F3FA"),accent=validColor("color.c5c4bb7","#5C4BB7");
    const QString family=value("font.mainwindow.window.1.fontfamily","Microsoft YaHei UI");
    QFont uiFont(family);uiFont.setPixelSize(qRound(styleNumber(settings.data,"number.mainwindow.window.1.fontsize",14)));uiFont.setStyleStrategy(QFont::PreferAntialias);setFont(uiFont);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget#root{background:%1;color:%2;}"
        "QWidget#titleBar{background:%3;}"
        "QWidget#titleBar QPushButton{color:white;border:0;background:transparent;font-weight:700;padding:5px;}"
        "QWidget#titleBar QPushButton:hover{background:#58456F;}"
        "QWidget#titleBar QPushButton[tab=true]{min-width:64px;font-size:13pt;border-bottom:2px solid transparent;padding:8px 10px;}"
        "QWidget#titleBar QPushButton[tab=true]:checked{border-bottom:2px solid #C4B5FD;background:#493564;}"
        "QWidget#titleBar QPushButton#syncButton{background:%4;border-radius:5px;min-width:48px;font-size:10pt;padding:3px 8px;}"
        "QPushButton{border:0;border-radius:5px;padding:4px 8px;background:#EEEAF8;color:%4;}"
        "QPushButton:hover{background:#E4DDEE;}QPushButton:pressed{background:#D6CDEE;}"
        "QPushButton:disabled{color:#B9B4C5;background:transparent;}"
        "QPushButton#queryButton{background:%4;color:white;font-weight:600;}"
        "QPushButton#deepSeekButton{background:#493564;padding:4px;}"
        "QPushButton#close:hover{background:#AC3C4A;}"
        "QLineEdit{background:#F7F5FB;border:1px solid #D8D0E5;border-radius:6px;padding:4px 6px;selection-background-color:#AA96D6;color:%2;}"
        "QLineEdit:focus{border:1px solid #7866C8;}"
        "QTextBrowser{background:white;border:1px solid #E4DFEE;border-radius:7px;selection-background-color:#CCDCED;}"
        "QListWidget{background:white;border:1px solid #E4DFEE;border-radius:7px;outline:none;}"
        "QListWidget::item{border-radius:5px;background:transparent;}"
        "QListWidget::item:selected{background:#EDE7FA;}QListWidget::item:hover{background:#F6F3FC;}"
        "QLabel#notice{border-radius:7px;padding:10px;font-size:11pt;background:#30234D;color:white;}"
        "QLabel#emptyBook{color:#92899F;background:transparent;}"
        "QToolTip{height:0px;opacity:0;}QMenu{background:white;color:%2;}"
        "QMenu::item:selected{background:#EDE7FA;}"
        ).arg(backgroundColor,foreground,purple,accent));
    updateDeepSeek();
    if(!currentEntry.isEmpty()){int scroll=browser->verticalScrollBar()->value();browser->setHtml(EntryRenderer::render(currentEntry,settings.data,selectedPart));browser->verticalScrollBar()->setValue(scroll);}
    if(restoreWindow) {
        auto saved=settings.data.value("Window").toObject();
        int width=qRound(saved.value("Width").toDouble(styleNumber(settings.data,"number.mainwindow.window.1.width",650)));
        int height=qRound(saved.value("Height").toDouble(styleNumber(settings.data,"number.mainwindow.window.1.height",689)));
        QScreen *screen=QGuiApplication::primaryScreen();
        QPoint desired(saved.value("Left").toInt(100),saved.value("Top").toInt(100));
        for(auto *s:QGuiApplication::screens())if(s->availableGeometry().contains(desired)){screen=s;break;}
        QRect area=screen?screen->availableGeometry():QRect(0,0,1920,1080);
        int minWidth=qMin(area.width(),qRound(styleNumber(settings.data,"number.mainwindow.window.1.minwidth",650)));
        int minHeight=qMin(area.height(),qRound(styleNumber(settings.data,"number.mainwindow.window.1.minheight",480)));
        setMinimumSize(qMax(420,minWidth),qMax(320,minHeight));
        width=qBound(minimumWidth(),width,qMax(minimumWidth(),area.width()));height=qBound(minimumHeight(),height,qMax(minimumHeight(),area.height()));
        int x=qBound(area.left(),desired.x(),qMax(area.left(),area.right()-width+1));int y=qBound(area.top(),desired.y(),qMax(area.top(),area.bottom()-height+1));
        setGeometry(x,y,width,height);if(saved.value("Maximized").toBool())setWindowState(windowState()|Qt::WindowMaximized);
    }
    if(pages->currentIndex()==1&&initialized)refreshBook();
}

void MainWindow::persistWindow() {
    QRect geometry=isMaximized()?normalGeometry():this->geometry();
    settings.data.insert("Window",QJsonObject{{"Left",geometry.left()},{"Top",geometry.top()},{"Width",geometry.width()},{"Height",geometry.height()},{"Maximized",isMaximized()}});
    settings.save();
}
void MainWindow::reloadConfig() {
    if(savingSettings)return;
    auto rearm=[this]{if(QFile::exists(settings.path)&&!configWatcher->files().contains(settings.path))configWatcher->addPath(settings.path);};
    rearm();
    auto sourceSettings=[](QJsonObject data){QJsonObject result;for(auto key:{"Provider","LibreUrl","ApiKey","ProtectedApiKey","username"})result.insert(key,data.value(key));QJsonObject ai;for(auto key:{"Enabled","ApiKey","Endpoint","Model","TimeoutSeconds","MaxTokens"})ai.insert(key,data.value("DeepSeek").toObject().value(key));result.insert("DeepSeek",ai);return result;};
    const auto oldSource=sourceSettings(settings.data);
    try {
        if(!settings.reload())return;
        if(settings.databasePath()!=database.path())requestIdentityChange();
        if(oldSource!=sourceSettings(settings.data)){++requestId;emit cancelOnline();query->setText(QStringLiteral("查询"));}
        applySettings(false);
    }catch(const std::exception &e){toast(QStringLiteral("设置未应用：")+QString::fromUtf8(e.what()),true);}
    rearm();
}
void MainWindow::requestIdentityChange(){
    if(switchingIdentity||pendingClose)return;
    switchingIdentity=true;initialized=false;++requestId;++bookRevision;emit cancelOnline();inputDelay->stop();filterDelay->stop();
    if(syncCanceled)syncCanceled->store(true);
    pages->setEnabled(false);currentEntry={};selectedPart.clear();book->clear();
    browser->setHtml(EntryRenderer::message(QStringLiteral("正在切换词库"),settings.username(),settings.data));
    toast(QStringLiteral("正在安全切换用户词库…"));
    QTimer::singleShot(0,this,&MainWindow::changeIdentity);
}
void MainWindow::changeIdentity(){
    if(pendingClose)return;
    if(pendingWrites>0||syncing){QTimer::singleShot(60,this,&MainWindow::changeIdentity);return;}
    const QString targetPath=settings.databasePath(),targetUser=settings.username();
    if(targetPath==database.path()){switchingIdentity=false;pages->setEnabled(true);initialize();return;}
    try{
        QDir().mkpath(QFileInfo(targetPath).absolutePath());
        auto nextLock=std::make_shared<QLockFile>(QFileInfo(targetPath).absolutePath()+"/app.lock");nextLock->setStaleLockTime(0);
        if(!nextLock->tryLock(0))throw std::runtime_error("目标用户词库正在被其他程序使用，已保留当前词库。");
        Database nextDatabase(targetPath,targetUser);
        // All old writes and their completions have drained before invalidating
        // outstanding reads. Each old job still owns its original database value.
        setProperty("storageGeneration",property("storageGeneration").toULongLong()+1);
        struct Initial{QJsonObject entry;bool dirty=false;};
        ++pendingWrites;
        background(this,[nextDatabase,nextLock]()mutable{nextDatabase.initialize();return Initial{nextDatabase.navigate(0),nextDatabase.revision()>nextDatabase.syncedRevision()};},
            [this,nextDatabase,nextLock,targetPath,targetUser](Initial initial){
                writeFinished();
                if(pendingClose)return;
                if(settings.databasePath()!=targetPath){QTimer::singleShot(0,this,&MainWindow::changeIdentity);return;}
                database=nextDatabase;profileLock=nextLock;activeUsername=targetUser;offset=0;total=0;firstDirtyAt=0;dirtyGeneration=0;
                switchingIdentity=false;initialized=true;pages->setEnabled(true);input->setEnabled(true);query->setEnabled(true);star->setEnabled(false);copy->setEnabled(false);query->setText(QStringLiteral("查询"));
                {QSignalBlocker block(input);input->setText(initial.entry.value("Word").toString());}
                currentEntry={};selectedPart.clear();
                if(initial.entry.isEmpty())browser->setHtml(EntryRenderer::message(QStringLiteral("拾词"),QStringLiteral("输入中文或英文，即可开始查询。"),settings.data));else showEntry(initial.entry);
                if(initial.dirty)dirty();updateHistory();if(pages->currentIndex()==1)refreshBook();toast(QStringLiteral("已切换用户词库：")+targetUser);
                if(settings.data.value("CacheSync").toObject().value("Enabled").toBool())runSync(true);
            },[this](QString error){writeFinished();if(pendingClose)return;switchingIdentity=false;pages->setEnabled(true);toast(error,true);initialize();});
    }catch(const std::exception &e){switchingIdentity=false;pages->setEnabled(true);toast(QString::fromUtf8(e.what()),true);initialize();}
}
bool MainWindow::useDeepSeek() const {return settings.data.value("DeepSeek").toObject().value("Enabled").toBool(false);}
void MainWindow::updateDeepSeek(){deepSeek->setChecked(useDeepSeek());deepSeek->setIcon(vectorIcon("whale",useDeepSeek()?QColor("#4D6BFE"):QColor(Qt::white)));deepSeek->setAccessibleName(useDeepSeek()?QStringLiteral("DeepSeek 已开启，点击关闭"):QStringLiteral("DeepSeek 已关闭，点击开启"));}

void MainWindow::lookup(bool force,bool manual,bool link) {
    if(!initialized||pendingClose)return;
    inputDelay->stop();QString text=input->text().trimmed();
    ++requestId;emit cancelOnline();quint64 revision=requestId;
    if(text.isEmpty()){currentEntry={};selectedPart.clear();favorite=false;star->setEnabled(false);copy->setEnabled(false);browser->setHtml(EntryRenderer::message(QStringLiteral("拾词"),QStringLiteral("输入中文或英文，即可开始查询。"),settings.data));return;}
    if(text.toUtf8().size()>500){toast(QStringLiteral("单次查询最多 500 个 UTF-8 字节，请缩短输入。"),true);return;}
    const QString pair=detectPair(text);
    if(removingWords.contains(identity(text,pair))){toast(QStringLiteral("正在移除该词，请稍后查询。"));return;}
    const bool ai=useDeepSeek(); auto db=database;
    struct Cached{QJsonObject entry;bool existingAi=false;};
    background(this,[db,text,pair,ai,manual,force]{Cached c;c.entry=db.find(text,pair);if(ai&&manual&&force)c.existingAi=db.hasDeepSeek(text,pair);return c;},
        [this,revision,text,ai,manual,force,link](Cached cached){
        if(revision!=requestId||pendingClose)return;
        if(ai&&manual&&force&&cached.existingAi){
            QMessageBox box(QMessageBox::Question,QStringLiteral("再次查询 DeepSeek"),QStringLiteral("“%1”已有 DeepSeek 释义。再次查询将调用 API 并更新内容，是否继续？").arg(text),QMessageBox::Yes|QMessageBox::No,this);box.setDefaultButton(QMessageBox::No);box.button(QMessageBox::Yes)->setText(QStringLiteral("再次查询"));box.button(QMessageBox::No)->setText(QStringLiteral("取消"));
            if(box.exec()!=QMessageBox::Yes)return;
            if(revision!=requestId)return;
        }
        const bool cachedAllowed=!force||(ai&&!manual);
        if(cachedAllowed&&!cached.entry.isEmpty()){
            auto db=database;QJsonObject entry=cached.entry;++pendingWrites;
            background(this,[db,entry]()mutable{db.store(entry,true);return true;},[this,revision,entry](bool){writeFinished();if(revision==requestId)showEntry(entry);updateHistory();},[this](QString e){writeFinished();toast(e,true);});return;
        }
        if(ai&&!manual&&!link){
            if(currentEntry.value("Word").toString()!=text){currentEntry={};selectedPart.clear();star->setEnabled(false);copy->setEnabled(false);browser->setHtml(EntryRenderer::message(text,QStringLiteral("点击查询或按回车，使用 DeepSeek 获取释义。"),settings.data));}
            return;
        }
        launchOnline(revision,text,ai);
    },[this,revision](QString e){if(revision==requestId)toast(e,true);});
}
void MainWindow::launchOnline(quint64 revision,const QString &text,bool ai){
    if(revision!=requestId)return;query->setText(QStringLiteral("查询中"));
    if(currentEntry.value("Word").toString()!=text){currentEntry={};selectedPart.clear();star->setEnabled(false);copy->setEnabled(false);browser->setHtml(EntryRenderer::message(text,QStringLiteral("正在查询…"),settings.data));}
    emit onlineLookup(revision,text,settings.data,ai);
}
void MainWindow::receiveEntry(quint64 revision,QJsonObject entry){
    if(revision!=requestId||pendingClose||removingWords.contains(identity(entry.value("Word").toString(),entry.value("Pair").toString())))return;
    auto db=database;++pendingWrites;
    background(this,[db,entry]()mutable{db.store(entry,true);return db.find(entry.value("Word").toString(),entry.value("Pair").toString());},[this,revision](QJsonObject stored){writeFinished();dirty();if(revision==requestId&&!stored.isEmpty())showEntry(stored);updateHistory();},[this,revision](QString e){writeFinished();if(revision==requestId){query->setText(QStringLiteral("查询"));toast(e,true);}});
}
void MainWindow::showEntry(const QJsonObject &entry,bool refreshFavorite){
    bool changed=currentEntry.value("Word")!=entry.value("Word");currentEntry=entry;if(changed)selectedPart.clear();
    if(!selectedPart.isEmpty()){bool found=false;for(auto meaning:entry.value("Record").toObject().value("Meanings").toArray())if(meaning.toObject().value("PartOfSpeech").toString()==selectedPart){found=true;break;}if(!found)selectedPart.clear();}
    browser->setHtml(EntryRenderer::render(entry,settings.data,selectedPart));query->setText(QStringLiteral("查询"));copy->setEnabled(true);star->setEnabled(false);
    if(refreshFavorite){auto db=database;auto word=entry.value("Word").toString(),pair=entry.value("Pair").toString();auto id=requestId;background(this,[db,word,pair]{return db.isFavorite(word,pair);},[this,id,word,pair](bool saved){if(id!=requestId||identity(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString())!=identity(word,pair))return;favorite=saved;star->setIcon(vectorIcon(saved?"star-filled":"star",QColor("#8A67C1")));star->setEnabled(true);},[this](QString e){toast(e,true);});}
}
void MainWindow::updateHistory(){if(!initialized)return;auto db=database;background(this,[db]{return QPair<bool,bool>(db.canNavigate(-1),db.canNavigate(1));},[this](QPair<bool,bool> state){back->setEnabled(state.first);forward->setEnabled(state.second);},[this](QString e){toast(e,true);});}
void MainWindow::navigate(int direction){if(!initialized||pendingClose)return;inputDelay->stop();++requestId;emit cancelOnline();auto id=requestId;auto db=database;background(this,[db,direction]()mutable{return db.navigate(direction);},[this,id](QJsonObject entry){if(id!=requestId||entry.isEmpty())return;{QSignalBlocker block(input);input->setText(entry.value("Word").toString());}showEntry(entry);updateHistory();},[this](QString e){toast(e,true);});}
void MainWindow::favoriteCurrent(){
    if(!initialized||currentEntry.isEmpty()||!star->isEnabled())return;auto entry=currentEntry;auto db=database;bool desired=!favorite;auto revision=requestId;star->setEnabled(false);++pendingWrites;
    background(this,[db,entry,desired]()mutable{db.setFavorite(entry.value("Word").toString(),entry.value("Pair").toString(),desired);return true;},[this,revision,desired](bool){writeFinished();dirty();if(revision==requestId){favorite=desired;star->setIcon(vectorIcon(desired?"star-filled":"star",QColor("#8A67C1")));star->setEnabled(true);}if(pages->currentIndex()==1)refreshBook();},[this](QString e){writeFinished();star->setEnabled(!currentEntry.isEmpty());toast(e,true);});
}

void MainWindow::refreshBook(){
    if(!initialized||pendingClose)return;
    const auto revision=++bookRevision;auto db=database;QString search=filter->text().trimmed();bool only=favoritesOnly->isChecked();int start=offset;
    background(this,[db,search,only,start]{return db.page(search,only,start,50);},[this,revision](BookPage page){
        if(revision!=bookRevision||pendingClose)return;total=page.total;
        if(total>0&&offset>=total){offset=((total-1)/50)*50;refreshBook();return;}
        book->clear();
        double wordSize=styleNumber(settings.data,"number.book.word_size",12.6),translationSize=styleNumber(settings.data,"number.book.translation_size",10.8);
        int wordHeight=qMax(qRound(styleNumber(settings.data,"number.book.word_line_height",15.3)),qRound(wordSize+2));
        int translationHeight=qMax(qRound(styleNumber(settings.data,"number.book.translation_line_height",13.77)),qRound(translationSize+2));
        const QString family=styleValue(settings.data,"font.doc.body","Microsoft YaHei UI");
        auto padding=styleValue(settings.data,"thickness.book.row_padding","8,2").split(',');
        int left=8,right=8,top=2,bottom=2;
        if(padding.size()==2){left=right=qBound(0,padding[0].toInt(),30);top=bottom=qBound(0,padding[1].toInt(),30);}
        else if(padding.size()==4){left=qBound(0,padding[0].toInt(),30);top=qBound(0,padding[1].toInt(),30);right=qBound(0,padding[2].toInt(),30);bottom=qBound(0,padding[3].toInt(),30);}
        for(const auto &row:page.rows){
            auto *item=new QListWidgetItem;item->setData(Qt::UserRole,row.id);
            auto *widget=new QWidget;widget->setObjectName("bookRow");widget->setStyleSheet("QWidget#bookRow{background:transparent;}");
            auto *rowLayout=new QHBoxLayout(widget);rowLayout->setContentsMargins(left,top,right,bottom);rowLayout->setSpacing(5);
            auto *text=new QWidget;auto *textLayout=new QVBoxLayout(text);textLayout->setContentsMargins(0,0,0,0);textLayout->setSpacing(0);
            auto *word=new QLabel(row.word);word->setTextFormat(Qt::PlainText);word->setMinimumWidth(0);word->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);word->setAttribute(Qt::WA_TransparentForMouseEvents);QFont wf(family);wf.setPixelSize(qRound(wordSize));wf.setWeight(QFont::DemiBold);word->setFont(wf);word->setFixedHeight(qMax(wordHeight,QFontMetrics(wf).height()));word->setStyleSheet("color:#34255C;background:transparent;");
            auto *summary=new QLabel(row.summary);summary->setTextFormat(Qt::PlainText);summary->setMinimumWidth(0);summary->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);summary->setAttribute(Qt::WA_TransparentForMouseEvents);QFont tf(family);tf.setPixelSize(qRound(translationSize));summary->setFont(tf);summary->setFixedHeight(qMax(translationHeight,QFontMetrics(tf).height()));summary->setStyleSheet("color:#777387;background:transparent;");
            textLayout->addWidget(word);textLayout->addWidget(summary);text->setAttribute(Qt::WA_TransparentForMouseEvents);rowLayout->addWidget(text,1);
            auto *sources=new QWidget;auto *sourceLayout=new QHBoxLayout(sources);sourceLayout->setContentsMargins(0,0,0,0);sourceLayout->setSpacing(5);
            if(row.hasAi){auto *icon=new QLabel;icon->setObjectName("aiSource");icon->setPixmap(vectorIcon("whale",QColor("#4D6BFE"),18).pixmap(18,18));icon->setFixedSize(18,24);icon->setAlignment(Qt::AlignCenter);icon->setAccessibleName(QStringLiteral("有 DeepSeek 释义"));sourceLayout->addWidget(icon,0,Qt::AlignVCenter);}
            if(row.hasOnline){auto *icon=new QLabel;icon->setObjectName("onlineSource");icon->setPixmap(vectorIcon("online",QColor("#8E83A7"),16).pixmap(16,16));icon->setFixedSize(18,24);icon->setAlignment(Qt::AlignCenter);icon->setAccessibleName(QStringLiteral("有在线词典释义"));sourceLayout->addWidget(icon,0,Qt::AlignVCenter);}
            sources->setAttribute(Qt::WA_TransparentForMouseEvents);rowLayout->addWidget(sources,0,Qt::AlignVCenter);
            auto *save=new QPushButton;save->setObjectName("rowFavorite");save->setProperty("wordId",row.id);save->setIcon(vectorIcon(row.favorite?"star-filled":"star",QColor("#8A67C1"),18));save->setIconSize(QSize(18,18));save->setFixedSize(26,26);save->setStyleSheet("QPushButton{background:transparent;padding:0;}QPushButton:hover{background:#E8DFF4;}");save->setAccessibleName(row.favorite?QStringLiteral("取消收藏 ")+row.word:QStringLiteral("收藏 ")+row.word);rowLayout->addWidget(save,0,Qt::AlignVCenter);
            auto *remove=new QPushButton;remove->setObjectName("rowRemove");remove->setProperty("wordId",row.id);remove->setIcon(vectorIcon("close",QColor("#847A94"),14));remove->setIconSize(QSize(14,14));remove->setFixedSize(24,26);remove->setEnabled(!row.favorite);remove->setAccessibleName(QStringLiteral("移除未收藏的单词 ")+row.word);remove->setStyleSheet("QPushButton{background:transparent;padding:0;}QPushButton:hover{background:#F5DEDF;}QPushButton:disabled{background:transparent;}");rowLayout->addWidget(remove,0,Qt::AlignVCenter);
            connect(save,&QPushButton::clicked,this,[this,row,save]{save->setEnabled(false);setRowFavorite(row);});connect(remove,&QPushButton::clicked,this,[this,row,remove]{remove->setEnabled(false);removeRow(row);});
            item->setSizeHint(QSize(0,qMax(26,word->height()+summary->height())+top+bottom));book->addItem(item);book->setItemWidget(item,widget);
        }
        const int count=qMax(1,(total+49)/50);pageLabel->setText(QString("%1 / %2 · %3").arg(offset/50+1).arg(count).arg(total));previous->setEnabled(offset>0);next->setEnabled(offset+50<total);
        emptyBook->setText(filter->text().isEmpty()?(favoritesOnly->isChecked()?QStringLiteral("暂无收藏"):QStringLiteral("暂无单词")):QStringLiteral("没有匹配的单词"));emptyBook->setGeometry(book->rect());emptyBook->setVisible(page.rows.isEmpty());emptyBook->raise();
    },[this,revision](QString e){if(revision==bookRevision)toast(e,true);});
}
void MainWindow::setRowFavorite(BookRow row){
    if(!initialized||pendingClose)return;auto db=database;++pendingWrites;
    background(this,[db,row]()mutable{db.setFavorite(row.word,row.pair,!row.favorite);return true;},[this,row](bool){writeFinished();dirty();refreshBook();if(identity(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString())==identity(row.word,row.pair)){favorite=!row.favorite;star->setIcon(vectorIcon(favorite?"star-filled":"star",QColor("#8A67C1")));}},[this](QString e){writeFinished();toast(e,true);refreshBook();});
}
void MainWindow::removeRow(BookRow row){
    if(!initialized||pendingClose)return;
    if(row.favorite){toast(QStringLiteral("请先取消收藏，再移除该词。"));refreshBook();return;}
    QString key=identity(row.word,row.pair);removingWords.insert(key);
    // Cancel before enqueueing the DELETE. A reply arriving later cannot recreate
    // this word; the database also rejects data older than the deletion tombstone.
    if(identity(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString())==key||identity(input->text(),detectPair(input->text()))==key){
        ++requestId;emit cancelOnline();inputDelay->stop();currentEntry={};selectedPart.clear();star->setEnabled(false);copy->setEnabled(false);query->setText(QStringLiteral("查询"));browser->setHtml(EntryRenderer::message(QStringLiteral("已移除"),QStringLiteral("重新查询可再次添加该词。"),settings.data));
    }
    auto db=database;++pendingWrites;
    background(this,[db,row]()mutable{return db.remove(row.id);},[this,key](bool removed){removingWords.remove(key);writeFinished();if(removed){dirty();toast(QStringLiteral("已移除该词及所有来源释义"));}else toast(QStringLiteral("该词已收藏或已不存在，请先取消收藏。"));refreshBook();updateHistory();},[this,key](QString e){removingWords.remove(key);writeFinished();toast(e,true);refreshBook();});
}
void MainWindow::dirty(){++dirtyGeneration;if(!firstDirtyAt)firstDirtyAt=QDateTime::currentMSecsSinceEpoch();}
void MainWindow::writeFinished(){pendingWrites=qMax(0,pendingWrites-1);if(pendingClose&&pendingWrites==0)QTimer::singleShot(0,this,[this]{close();});}
void MainWindow::toast(const QString &message,bool error){
    notice->setText(message);notice->setStyleSheet(QString("QLabel#notice{background:%1;color:white;border-radius:7px;padding:10px;font-size:11pt;}").arg(error?"#9E3948":"#30234D"));
    int w=qMin(width()-32,470);notice->setFixedWidth(w);notice->adjustSize();notice->move((width()-w)/2,48);notice->show();notice->raise();noticeTimer->start();
}
void MainWindow::runSync(bool pullOnly){
    if(!initialized||syncing||pendingClose)return;
    if(settings.databasePath()!=database.path()){toast(QStringLiteral("请重新启动，应用新用户身份后再同步。"),true);return;}
    syncing=true;sync->setEnabled(false);sync->setText(QStringLiteral("同步中"));QString directory=settings.directory,dbPath=database.path(),user=activeUsername;QJsonObject config=settings.data;const auto generation=dirtyGeneration;syncCanceled=std::make_shared<std::atomic_bool>(false);auto canceled=syncCanceled;
    struct Synced{QString message;bool pending=false;};
    background(this,[directory,config,dbPath,user,pullOnly,canceled]{auto message=CacheSync::run(directory,config,dbPath,user,pullOnly,canceled);Database db(dbPath,user);return Synced{message,db.revision()>db.syncedRevision()};},[this,generation](Synced result){syncing=false;sync->setEnabled(true);sync->setText(QStringLiteral("同步"));if(result.pending||generation!=dirtyGeneration){if(!firstDirtyAt)dirty();}else firstDirtyAt=0;toast(result.message.isEmpty()?QStringLiteral("同步完成"):result.message);refreshBook();updateHistory();if(!currentEntry.isEmpty()){auto db=database;auto entry=currentEntry;auto revision=requestId;background(this,[db,entry]{return db.find(entry.value("Word").toString(),entry.value("Pair").toString());},[this,revision](QJsonObject value){if(revision!=requestId)return;if(!value.isEmpty())showEntry(value);else{currentEntry={};selectedPart.clear();star->setEnabled(false);copy->setEnabled(false);browser->setHtml(EntryRenderer::message(QStringLiteral("已移除"),QStringLiteral("该词已从同步词库中移除。"),settings.data));}},[this](QString e){toast(e,true);});}},[this](QString error){syncing=false;sync->setEnabled(true);sync->setText(QStringLiteral("重试同步"));toast(QStringLiteral("同步失败：")+error+QStringLiteral("\n本地词库已保存，可点击重试。"),true);});
}
void MainWindow::exportBook(){
    if(!initialized)return;QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出单词本"),QDir::homePath()+"/MyDict.csv",QStringLiteral("CSV 文件 (*.csv)"));if(path.isEmpty())return;auto db=database;QString search=filter->text().trimmed();bool only=favoritesOnly->isChecked();
    background(this,[db,path,search,only]{db.exportCsv(path,search,only);return true;},[this](bool){toast(QStringLiteral("已导出单词本"));},[this](QString e){toast(e,true);});
}
void MainWindow::importBook(){
    if(!initialized||pendingClose)return;QString path=QFileDialog::getOpenFileName(this,QStringLiteral("导入词库导出文件"),QDir::homePath(),QStringLiteral("MyDict 迁移文件 (*.ndjson *.jsonl);;所有文件 (*)"));if(path.isEmpty())return;auto db=database;++pendingWrites;
    background(this,[db,path]()mutable{db.importEntries(path);return true;},[this](bool){writeFinished();dirty();toast(QStringLiteral("导入完成"));refreshBook();updateHistory();},[this](QString e){writeFinished();toast(e,true);refreshBook();});
}
void MainWindow::setStyle(const QString &key,double value){
    QJsonObject groups=settings.data.value("Styles").toObject();bool found=false;
    for(auto i=groups.begin();i!=groups.end();++i){if(!i.value().isObject())continue;auto group=i.value().toObject();if(group.contains(key)){group.insert(key,QString::number(value,'f',3));groups.insert(i.key(),group);found=true;break;}}
    if(!found){QString groupName=key.contains(".book.")?QStringLiteral("02_单词本"):QStringLiteral("03_词条与释义");auto group=groups.value(groupName).toObject();group.insert(key,QString::number(value,'f',3));groups.insert(groupName,group);}settings.data.insert("Styles",groups);
}
void MainWindow::adjustTypography(double fontDelta,double lineDelta){
    const double oldScale=styleNumber(settings.data,"number.doc.scale",.9),newScale=qBound(.65,oldScale+fontDelta,2.2);
    const double oldLeading=styleNumber(settings.data,"number.doc.leading_scale",.85),newLeading=qBound(.65,oldLeading+lineDelta,2.2);
    setStyle("number.doc.scale",newScale);setStyle("number.doc.leading_scale",newLeading);
    for(const auto &key:QStringList{"number.book.word_size","number.book.translation_size","number.book.word_line_height","number.book.translation_line_height"}){
        bool height=key.contains("line_height");double fallback=key.contains("translation")?(height?13.77:10.8):(height?15.3:12.6);
        double ratio=fontDelta!=0?newScale/oldScale:(height?newLeading/oldLeading:1.);setStyle(key,qBound(6.,styleNumber(settings.data,key,fallback)*ratio,72.));
    }
    try{settings.save();}catch(const std::exception &e){toast(QString::fromUtf8(e.what()),true);}applySettings(false);
}

bool MainWindow::eventFilter(QObject *watched,QEvent *event){
    auto *widget=qobject_cast<QWidget*>(watched);
    if(!widget||widget->window()!=this)return QMainWindow::eventFilter(watched,event);
    if(event->type()==QEvent::ToolTip)return true;
    if(event->type()==QEvent::Resize&&watched==book&&emptyBook)emptyBook->setGeometry(book->rect());
    if(event->type()==QEvent::MouseButtonDblClick&&watched==titleBar){auto *e=static_cast<QMouseEvent*>(event);if(e->button()==Qt::LeftButton){isMaximized()?showNormal():showMaximized();return true;}}
    if(event->type()==QEvent::MouseButtonPress){
        auto *e=static_cast<QMouseEvent*>(event);if(e->button()!=Qt::LeftButton)return false;QPoint global=globalPoint(e),local=mapFromGlobal(global);Qt::Edges edges;
        if(!isMaximized()){if(local.x()<5)edges|=Qt::LeftEdge;if(local.x()>width()-6)edges|=Qt::RightEdge;if(local.y()<5)edges|=Qt::TopEdge;if(local.y()>height()-6)edges|=Qt::BottomEdge;}
        bool titleDrag=watched==titleBar;
        if(edges||titleDrag){
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            if(windowHandle()&&(edges?windowHandle()->startSystemResize(edges):windowHandle()->startSystemMove()))return true;
#endif
            dragging=true;resizeEdges=edges;dragStart=global;dragGeometry=geometry();widget->grabMouse();return true;
        }
    }
    if(event->type()==QEvent::MouseMove){
        auto *e=static_cast<QMouseEvent*>(event);QPoint global=globalPoint(e);
        if(dragging){QPoint delta=global-dragStart;QRect target=dragGeometry;if(resizeEdges){if(resizeEdges&Qt::LeftEdge)target.setLeft(qMin(dragGeometry.left()+delta.x(),target.right()-minimumWidth()+1));if(resizeEdges&Qt::RightEdge)target.setRight(qMax(dragGeometry.right()+delta.x(),target.left()+minimumWidth()-1));if(resizeEdges&Qt::TopEdge)target.setTop(qMin(dragGeometry.top()+delta.y(),target.bottom()-minimumHeight()+1));if(resizeEdges&Qt::BottomEdge)target.setBottom(qMax(dragGeometry.bottom()+delta.y(),target.top()+minimumHeight()-1));setGeometry(target);}else if(!isMaximized())move(dragGeometry.topLeft()+delta);return true;}
    }
    if(event->type()==QEvent::MouseButtonRelease&&dragging){dragging=false;resizeEdges={};widget->releaseMouse();return true;}
    return QMainWindow::eventFilter(watched,event);
}
void MainWindow::closeEvent(QCloseEvent *event){
    if(syncCanceled)syncCanceled->store(true);
    ++requestId;emit cancelOnline();inputDelay->stop();filterDelay->stop();
    if(pendingWrites>0){pendingClose=true;input->setEnabled(false);query->setEnabled(false);star->setEnabled(false);toast(QStringLiteral("正在保存本地词库…"));event->ignore();return;}
    try{persistWindow();}catch(const std::exception &e){pendingClose=false;input->setEnabled(initialized);query->setEnabled(initialized);star->setEnabled(initialized&&!currentEntry.isEmpty());toast(QString::fromUtf8(e.what()),true);event->ignore();return;}
    event->accept();
}
}
