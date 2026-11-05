#include "mainwindow.h"
#include "dailypage.h"
#include "entryrenderer.h"
#include "appfonts.h"
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
#include <QScrollArea>
#include <QComboBox>
#include <QSpinBox>
#include <QStyle>
#include <QMenu>
#include <QShortcut>
#include <QScreen>
#include <QWindow>
#include <QMouseEvent>
#include <QCloseEvent>
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
#include <QShowEvent>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif
#include <QSignalBlocker>
#include <QAbstractButton>
#include <QTextLayout>
#include <QGlyphRun>
#include <functional>
#include <exception>
#include <stdexcept>
#include <cmath>

namespace MyDict {
namespace {
// QPushButton normally measures only its own caption, ignoring child labels.
// Include the option/meaning layout and its wrapped height in geometry negotiation.
class ReviewChoiceButton : public QPushButton {
public:
    ReviewChoiceButton() {
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    QSize sizeHint() const override {
        return layout() ? layout()->totalSizeHint().expandedTo(QSize(0, 56)) : QPushButton::sizeHint();
    }
    QSize minimumSizeHint() const override {
        return QSize(0, layout() ? qMax(56, layout()->totalMinimumSize().height()) : 56);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        return layout() ? qMax(minimumSizeHint().height(), layout()->totalHeightForWidth(width)) : 56;
    }
};
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
Settings loadedSettings(QString directory){Settings s(directory);s.load();return s;}
bool hasChineseGlyphs(const QString &family) {
    // Shape a short sample through the normal system fallback chain. Checking
    // just the primary face would needlessly load Noto for Latin UI fonts on
    // systems that already have a suitable Chinese fallback installed.
    QTextLayout layout(QStringLiteral("拾词查询收藏释义"),QFont(AppFonts::resolve(family)));
    layout.beginLayout();
    auto line=layout.createLine();if(line.isValid())line.setLineWidth(10000);
    layout.endLayout();
    bool found=false;
    for(const auto &run:layout.glyphRuns())for(auto glyph:run.glyphIndexes()){
        found=true;if(glyph==0)return false;
    }
    return found;
}
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
    updateFonts(false);
    createUi(); applySettings(true);
    online = new OnlineService;
    online->moveToThread(&networkThread);
    connect(&networkThread, &QThread::finished, online, &QObject::deleteLater);
    connect(this, &MainWindow::onlineLookup, online, &OnlineService::lookup, Qt::QueuedConnection);
    connect(this, &MainWindow::cancelOnline, online, &OnlineService::cancel, Qt::QueuedConnection);
    connect(online, &OnlineService::ready, this, &MainWindow::receiveEntry);
    connect(online, &OnlineService::failed, this, [this](quint64 id, QString message) {
        if (id != requestId) return; query->setText(QStringLiteral("查询")); toast(message, true);
        if (currentEntry.isEmpty()) showMessage(QStringLiteral("暂未查到结果"), message);
    });

    configWatcher = new QFileSystemWatcher(this); configWatcher->addPath(settings.directory);
    if (QFile::exists(settings.path)) configWatcher->addPath(settings.path);
    connect(configWatcher, &QFileSystemWatcher::fileChanged, this, [this] { reloadDelay->start(180); });
    connect(configWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { reloadDelay->start(180); });
    connect(reloadDelay, &QTimer::timeout, this, &MainWindow::reloadConfig);
    qApp->installEventFilter(this);
    for(auto *child:findChildren<QWidget*>())child->setMouseTracking(true);
    setMouseTracking(true);
}
MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    if(syncCanceled)syncCanceled->store(true);
    if(dailyPage)dailyPage->cancel();
    emit cancelOnline(); networkThread.quit(); networkThread.wait();
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
#ifdef Q_OS_WIN
    // Windows 11 draws a light DWM outline even for a frameless window.
    const DWORD noBorder = 0xFFFFFFFE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 34, &noBorder, sizeof(noBorder));
#endif
    if(startupScheduled)return;
    startupScheduled=true;
    // Let the window paint before font registration, cached HTML and database work.
    QTimer::singleShot(80,this,[this]{
        if(pendingClose)return;
        startupReady=true;
        applySettings(false);
        networkThread.start();
        initialize();
    });
}

#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
#else
bool MainWindow::nativeEvent(const QByteArray &type, void *message, long *result) {
#endif
    auto *msg=static_cast<MSG*>(message);
    if(msg->message==WM_NCHITTEST&&!isMaximized()&&!isFullScreen()){
        RECT bounds;GetWindowRect(msg->hwnd,&bounds);
        const int x=GET_X_LPARAM(msg->lParam),y=GET_Y_LPARAM(msg->lParam);
        const int edge=qRound(7*devicePixelRatioF());
        const bool left=x>=bounds.left&&x<bounds.left+edge,right=x<bounds.right&&x>=bounds.right-edge;
        const bool top=y>=bounds.top&&y<bounds.top+edge,bottom=y<bounds.bottom&&y>=bounds.bottom-edge;
        if(left||right||top||bottom){
            *result=top?(left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP):bottom?(left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM):left?HTLEFT:HTRIGHT;
            return true;
        }
    }
    return QMainWindow::nativeEvent(type,message,result);
}
#endif

void MainWindow::createUi() {
    auto *root = new QWidget(this); root->setObjectName("root"); setCentralWidget(root);
    auto *layout = new QVBoxLayout(root); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    titleBar = new QWidget(root); titleBar->setObjectName("titleBar"); titleBar->setFixedHeight(42);
    auto *title = new QHBoxLayout(titleBar); title->setContentsMargins(7,0,4,0); title->setSpacing(2);
    lookupTab = new QPushButton(QStringLiteral("查词")); lookupTab->setObjectName("lookupTab"); lookupTab->setCheckable(true); lookupTab->setChecked(true);
    bookTab = new QPushButton(QStringLiteral("单词本")); bookTab->setObjectName("bookTab"); bookTab->setCheckable(true);
    lookupTab->setProperty("tab", true); bookTab->setProperty("tab", true);
    title->addWidget(lookupTab); title->addWidget(bookTab); dailyTab=new QPushButton("今日词汇");dailyTab->setObjectName("dailyTab");dailyTab->setProperty("tab",true);dailyTab->setCheckable(true);title->addWidget(dailyTab); title->addStretch();
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
    deepSeek = makeButton("deepSeekButton", QStringLiteral("切换查询和显示来源"), "whale"); deepSeek->setEnabled(false); deepSeek->setFixedWidth(34); bar->addWidget(deepSeek);
    query = new QPushButton(QStringLiteral("查询")); query->setObjectName("queryButton"); query->setMinimumWidth(54); query->setFixedHeight(34); query->setEnabled(false); bar->addWidget(query);
    star = makeButton("favoriteButton", QStringLiteral("收藏或取消收藏当前词"), "star"); star->setEnabled(false); bar->addWidget(star);
    copy = makeButton("copyButton", QStringLiteral("复制当前释义"), "copy"); copy->setEnabled(false); bar->addWidget(copy);
    lookupLayout->addLayout(bar);
    browser = new EntryBrowser; browser->setObjectName("entryBrowser"); browser->document()->setDocumentMargin(6); showMessage(QStringLiteral("拾词"), QStringLiteral("输入中文或英文，即可开始查询。")); lookupLayout->addWidget(browser,1);
    pages->addWidget(lookupPage);
    auto *bookPage = new QWidget; auto *bookLayout = new QVBoxLayout(bookPage); bookLayout->setContentsMargins(10,5,10,5); bookLayout->setSpacing(4);
    auto *bookBar = new QHBoxLayout; bookBar->setSpacing(6);
    filter = new QLineEdit; filter->setObjectName("bookFilter"); filter->setPlaceholderText(QStringLiteral("筛选单词或释义")); filter->setClearButtonEnabled(true); filter->setMinimumHeight(32); bookBar->addWidget(filter,1);
    auto *reviewStart = new QPushButton(QStringLiteral("复习"));
    reviewStart->setObjectName("reviewStart");
    bookBar->addWidget(reviewStart);
    connect(reviewStart, &QPushButton::clicked, this, &MainWindow::startReview);
    auto *exportButton = new QPushButton(QStringLiteral("导出")); exportButton->setObjectName("exportButton"); bookBar->addWidget(exportButton);
    auto *more = new QPushButton(QStringLiteral("⋯")); more->setObjectName("bookMore"); more->setFixedWidth(30); auto *menu = new QMenu(more); menu->addAction(QStringLiteral("导入词库文件…"), this, &MainWindow::importBook); more->setMenu(menu); bookBar->addWidget(more);
    bookLayout->addLayout(bookBar);
    book = new QListWidget; book->setObjectName("wordbook"); book->setSpacing(1); book->setUniformItemSizes(false); book->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); book->setFrameShape(QFrame::NoFrame); bookLayout->addWidget(book,1);
    emptyBook = new QLabel(QStringLiteral("暂无单词"), book); emptyBook->setObjectName("emptyBook"); emptyBook->setAlignment(Qt::AlignCenter); emptyBook->setAttribute(Qt::WA_TransparentForMouseEvents); emptyBook->hide();
    auto *pager = new QHBoxLayout; pager->addStretch(); previous = new QPushButton; previous->setObjectName("bookPrevious"); previous->setIcon(vectorIcon("back", QColor("#5C4BB7"))); previous->setFixedSize(28,26); pageLabel = new QLabel("1 / 1"); pageLabel->setObjectName("bookPageLabel"); next = new QPushButton; next->setObjectName("bookNext"); next->setIcon(vectorIcon("forward", QColor("#5C4BB7"))); next->setFixedSize(28,26); pager->addWidget(previous); pageJump=new QSpinBox; pageJump->setObjectName("bookPageJump"); pageJump->setRange(1,1); pageJump->setKeyboardTracking(false); pageJump->setMinimumWidth(70); pageJump->setAccessibleName(QStringLiteral("跳转页码")); pager->addWidget(pageJump); pager->addWidget(pageLabel); pager->addWidget(next); bookLayout->addLayout(pager);
    pages->addWidget(bookPage);
    auto *reviewPage = new QWidget;
    reviewPage->setObjectName("reviewPage");
    auto *reviewLayout = new QVBoxLayout(reviewPage);
    reviewLayout->setContentsMargins(14,10,14,10);
    reviewLayout->setSpacing(10);
    auto *reviewHeader = new QHBoxLayout;
    reviewMode = new QComboBox;
    reviewMode->setObjectName("reviewMode");
    reviewMode->setMinimumContentsLength(7);
    reviewMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    reviewMode->setAccessibleName(QStringLiteral("复习方式"));
    reviewMode->addItems({QStringLiteral("释义选择"), QStringLiteral("选词填空")});
    reviewHeader->addWidget(reviewMode);
    connect(reviewMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (pages->currentIndex() == 2 && initialized && !reviewBusy) nextReview();
    });
    reviewHeader->addStretch();
    reviewUnfamiliar = new QPushButton(QStringLiteral("不太熟"));
    reviewUnfamiliar->setObjectName("reviewUnfamiliar");
    reviewHeader->addWidget(reviewUnfamiliar);
    connect(reviewUnfamiliar, &QPushButton::clicked, this, [this] {
        if (reviewAnswered || review.token.isEmpty()) advanceReview();
        else chooseReviewAnswer(-1);
    });
    reviewExit = new QPushButton(QStringLiteral("退出复习 ×"));
    reviewExit->setObjectName("reviewExit");
    reviewHeader->addWidget(reviewExit);
    connect(reviewExit, &QPushButton::clicked, this, [this] { leaveReview(); });
    reviewLayout->addLayout(reviewHeader);
    reviewScroll = new QScrollArea;
    reviewScroll->setWidgetResizable(true);
    reviewScroll->setFrameShape(QFrame::NoFrame);
    auto *reviewContent = new QWidget;
    auto *questions = new QVBoxLayout(reviewContent);
    questions->setContentsMargins(4,4,4,4);
    questions->setSpacing(12);
    reviewWord = new QLabel;
    reviewWord->setObjectName("reviewWord");
    reviewWord->setTextFormat(Qt::PlainText);
    reviewWord->setWordWrap(true);
    reviewWord->setAlignment(Qt::AlignCenter);
    reviewWord->setMinimumHeight(62);
    questions->addWidget(reviewWord);
    reviewPrompt = new QLabel(QStringLiteral("选择正确的释义"));
    reviewPrompt->setAlignment(Qt::AlignCenter);
    reviewPrompt->setWordWrap(true);
    reviewPrompt->setTextFormat(Qt::PlainText);
    reviewPrompt->setObjectName("reviewPrompt");
    auto promptPolicy = reviewPrompt->sizePolicy();
    promptPolicy.setRetainSizeWhenHidden(true);
    reviewPrompt->setSizePolicy(promptPolicy);
    questions->addWidget(reviewPrompt);
    for (int i = 0; i < 4; ++i) {
        auto *choice = new ReviewChoiceButton;
        choice->setObjectName(QStringLiteral("reviewChoice%1").arg(i));
        choice->setProperty("reviewChoice", true);
        choice->setMinimumHeight(56);
        auto *choiceLayout = new QVBoxLayout(choice);
        choiceLayout->setSpacing(4);
        choiceLayout->setContentsMargins(14,10,14,10);
        auto *label = new QLabel;
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        choiceLayout->addWidget(label);
        auto *meaningLabel = new QLabel;
        meaningLabel->setObjectName("reviewChoiceMeaning");
        meaningLabel->setTextFormat(Qt::PlainText);
        meaningLabel->setWordWrap(true);
        meaningLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        meaningLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto meaningPolicy = meaningLabel->sizePolicy();
        meaningPolicy.setRetainSizeWhenHidden(true);
        meaningLabel->setSizePolicy(meaningPolicy);
        choiceLayout->addWidget(meaningLabel);
        meaningLabel->hide();
        questions->addWidget(choice);
        reviewChoices.append(choice);
        connect(choice, &QPushButton::clicked, this, [this,i] { chooseReviewAnswer(i); });
    }
    questions->addStretch();
    reviewScroll->setWidget(reviewContent);
    reviewLayout->addWidget(reviewScroll,1);
    pages->addWidget(reviewPage);
    dailyPage=new DailyPage;pages->addWidget(dailyPage);
    connect(dailyTab,&QPushButton::clicked,this,[this]{pages->setCurrentIndex(3);if(initialized){dailyPage->configure(database,settings.data);dailyPage->activate();}});
    connect(pages,&QStackedWidget::currentChanged,this,[this](int page){lookupTab->setChecked(page==0);bookTab->setChecked(page==1||page==2);dailyTab->setChecked(page==3);});
    connect(dailyPage,&DailyPage::lookupRequested,this,[this](QString word){pages->setCurrentIndex(0);{QSignalBlocker block(input);input->setText(word);}lookup(false,false,true);});
    connect(dailyPage,&DailyPage::workStarted,this,[this]{++pendingWrites;});
    connect(dailyPage,&DailyPage::workFinished,this,&MainWindow::writeFinished);
    connect(dailyPage,&DailyPage::changed,this,[this]{dirty();refreshFavoriteState();});
    notice = new QLabel(root); notice->setObjectName("notice"); notice->setWordWrap(true); notice->setAlignment(Qt::AlignCenter); notice->setAttribute(Qt::WA_TransparentForMouseEvents); notice->hide();

    inputDelay = new QTimer(this); inputDelay->setSingleShot(true);
    filterDelay = new QTimer(this); filterDelay->setSingleShot(true); filterDelay->setInterval(200);
    noticeTimer = new QTimer(this); noticeTimer->setSingleShot(true); noticeTimer->setInterval(3000);
    reloadDelay = new QTimer(this); reloadDelay->setSingleShot(true);
    syncTimer = new QTimer(this); syncTimer->setInterval(10000); syncTimer->start();
    connect(input, &QLineEdit::textChanged, this, [this] { ++requestId; emit cancelOnline(); inputDelay->start(qBound(300, settings.data.value("DelayMs").toInt(650), 3000)); query->setText(QStringLiteral("查询")); });
    connect(inputDelay, &QTimer::timeout, this, [this] { lookup(false,false); });
    connect(query, &QPushButton::clicked, this, [this] { lookup(true,true); });
    connect(input, &QLineEdit::returnPressed, this, [this] { lookup(true,true); });
    connect(deepSeek, &QPushButton::clicked, this, [this] {
        auto ai = settings.data.value("DeepSeek").toObject(); ai.insert("Enabled",!useDeepSeek()); settings.data.insert("DeepSeek",ai);
        try { settings.save(); } catch (const std::exception &e) { toast(QString::fromUtf8(e.what()), true); }
        updateDeepSeek();selectedPart.clear();lookup(false,false,false,true);
    });
    connect(star, &QPushButton::clicked, this, &MainWindow::favoriteCurrent);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(browser->toPlainText().remove(QChar::ObjectReplacementCharacter).trimmed()); toast(QStringLiteral("已复制释义")); });
    connect(back, &QPushButton::clicked, this, [this] { navigate(-1); }); connect(forward, &QPushButton::clicked, this, [this] { navigate(1); });
    connect(lookupTab, &QPushButton::clicked, this, [this] { pages->setCurrentIndex(0); lookupTab->setChecked(true); bookTab->setChecked(false); input->setFocus(); });
    connect(pageJump, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int page) { offset=(page-1)*50; refreshBook(); });
    connect(bookTab, &QPushButton::clicked, this, [this] { pages->setCurrentIndex(reviewActive ? 2 : 1); lookupTab->setChecked(false); bookTab->setChecked(true); if (!reviewActive && initialized) refreshBook(); });
    connect(filter, &QLineEdit::textChanged, this, [this] { offset=0; filterDelay->start(); }); connect(filterDelay, &QTimer::timeout, this, &MainWindow::refreshBook);
    connect(previous, &QPushButton::clicked, this, [this] { offset=qMax(0,offset-50); refreshBook(); }); connect(next, &QPushButton::clicked, this, [this] { if(offset+50<total) {offset+=50;refreshBook();} });
    connect(book, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!initialized) return;auto db=database;const auto word=item->data(Qt::UserRole+1).toString(),pair=item->data(Qt::UserRole+2).toString();const bool ai=useDeepSeek();inputDelay->stop();++requestId;emit cancelOnline();auto revision=requestId;query->setText(QStringLiteral("查询中"));
        ++pendingWrites;
        background(this,[db,word,pair,ai]()mutable{auto entry=db.findForSource(word,pair,ai);if(!entry.isEmpty())db.store(entry,true);return HistorySelection{word,pair,entry};},[this,revision](HistorySelection selection){writeFinished();if(revision!=requestId)return;showSelection(selection);updateHistory();pages->setCurrentIndex(0);lookupTab->setChecked(true);bookTab->setChecked(false);},[this,revision](QString e){writeFinished();if(revision==requestId){query->setText(QStringLiteral("查询"));toast(e,true);}});
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
    auto db=database;const bool ai=useDeepSeek();
    struct Initial {HistorySelection selection;bool dirty=false;};
    background(this,[db,ai]() mutable {db.initialize();return Initial{db.navigateForSource(0,ai),db.revision()>db.syncedRevision()};},[this,ai](Initial initial) {
        initialized=true;input->setEnabled(true);query->setEnabled(true);deepSeek->setEnabled(true);input->setFocus();updateHistory();
        dailyPage->configure(database,settings.data);if(pages->currentIndex()==3)dailyPage->activate();
        if(initial.dirty)dirty();
        if(!initial.selection.word.isEmpty()&&input->text().isEmpty()){showSelection(initial.selection);if(ai!=useDeepSeek())lookup(false,false,false,true);}
        if(pages->currentIndex()==1)refreshBook();
        if(settings.data.value("CacheSync").toObject().value("Enabled").toBool(false))QTimer::singleShot(800,this,[this]{if(!pendingClose)runSync(true);});
    },[this](QString e) {toast(e,true);showMessage(QStringLiteral("无法打开词库"),e);});
}

void MainWindow::updateFonts(bool includePhonetic) {
    if(!startupReady)return;
    // Only keys consumed by this UI/HTML renderer belong here. WPF-only keys,
    // compatibility FontName fields and unrelated font files are not loaded.
    const auto ui=styleValue(settings.data,"font.mainwindow.window.1.fontfamily","Microsoft YaHei UI");
    const auto body=styleValue(settings.data,"font.doc.body","Microsoft YaHei UI");
    QSet<QString> families{ui,body,
        styleValue(settings.data,"font.doc.serif","Georgia")};
    if(includePhonetic)families.insert(styleValue(settings.data,"font.doc.phonetic","Segoe UI"));
    QStringList directories{QDir(settings.directory).filePath("fonts")};
    directories.append(QDir(QCoreApplication::applicationDirPath()).filePath("fonts"));
#ifdef Q_OS_MAC
    directories.append(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../Resources/fonts"));
#endif
    const auto key=ui+"\n"+body+"\n"+directories.join('\n');
    const bool recheck=fontCoverageKey!=key;
    if(recheck){fontCoverageKey=key;needsChineseFallback=false;}
    if(needsChineseFallback)families.insert("Noto Sans SC");
    AppFonts::apply(families,directories);
    if(recheck&&(!hasChineseGlyphs(ui)||!hasChineseGlyphs(body))){
        // The bundled CJK face is necessary only when these actual text fonts
        // and the system fallback chain cannot render Chinese.
        needsChineseFallback=true;families.insert("Noto Sans SC");
        AppFonts::apply(families,directories);
    }
}
void MainWindow::showMessage(const QString &title,const QString &message) {
    messageTitle=title;messageText=message;updateFonts(false);
    browser->setHtml(EntryRenderer::message(title,message,settings.data));
}
void MainWindow::showSourcePlaceholder(const QString &word,bool loading) {
    currentEntry={};selectedPart.clear();favorite=false;star->setEnabled(false);copy->setEnabled(false);query->setText(QStringLiteral("查询"));
    star->setIcon(vectorIcon("star",QColor("#8A67C1")));
    if(word.isEmpty()){showMessage(QStringLiteral("拾词"),QStringLiteral("输入中文或英文，即可开始查询。"));return;}
    const auto source=useDeepSeek()?QStringLiteral("DeepSeek"):QStringLiteral("在线词典");
    showMessage(word,loading?QStringLiteral("正在读取%1缓存…").arg(source):QStringLiteral("暂无%1释义，点击查询或按回车获取。").arg(source));
}
void MainWindow::showSelection(const HistorySelection &selection) {
    {QSignalBlocker block(input);input->setText(selection.word);}
    if(selection.entry.isEmpty())showSourcePlaceholder(selection.word);else showEntry(selection.entry);
}

void MainWindow::applySettings(bool restoreWindow) {
    updateFonts(!currentEntry.value("Record").toObject().value("Phonetic").toString().isEmpty());
    auto value=[this](QString key,QString fallback){return styleValue(settings.data,key,fallback);};
    auto validColor=[&](QString key,QString fallback){QColor c(value(key,fallback));return c.isValid()?c.name():fallback;};
    const QString purple=validColor("color.c30234d","#30234D"), foreground=validColor("color.c272438","#272438"), backgroundColor=validColor("color.cf5f3fa","#F5F3FA"),accent=validColor("color.c5c4bb7","#5C4BB7");
    const QString family=AppFonts::resolve(value("font.mainwindow.window.1.fontfamily","Microsoft YaHei UI"));
    QFont uiFont(family);uiFont.setPixelSize(qRound(styleNumber(settings.data,"number.mainwindow.window.1.fontsize",14)));uiFont.setStyleStrategy(QFont::PreferAntialias);setFont(uiFont);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget#root{background:%1;color:%2;}"
        "QWidget#titleBar{background:%3;}"
        "QWidget#titleBar QPushButton{color:white;border:0;background:transparent;font-weight:700;padding:5px;}"
        "QWidget#titleBar QPushButton:hover{background:#58456F;}"
        "QWidget#titleBar QPushButton[tab=true]{border-radius:0;min-width:64px;font-size:13pt;border-bottom:2px solid transparent;padding:8px 10px;}"
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
        "QWidget#reviewPage QScrollArea,QWidget#reviewPage QScrollArea>QWidget>QWidget{background:transparent;}"
        "QLabel#reviewWord{font-size:%5px;font-weight:900;color:%4;}"
        "QLabel#reviewWord[sentenceQuestion=true]{font-size:%7px;font-weight:600;}"
        "QComboBox#reviewMode{background:#EEEAF8;color:%4;border:1px solid #D8D0E5;border-radius:5px;padding:4px 8px;}"
        "QPushButton[reviewChoice=true]{background:white;border:1px solid #D8D0E5;border-radius:8px;}"
        "QPushButton[reviewChoice=true]:hover{background:#F0EBFA;border-color:%4;}"
        "QPushButton[reviewChoice=true] QLabel{font-size:%6px;color:%2;background:transparent;}"
        "QPushButton#reviewUnfamiliar{background:#EEE4D2;color:#805B21;font-weight:700;}"
        "QToolTip{height:0px;opacity:0;}QMenu{background:white;color:%2;}"
        "QMenu::item:selected{background:#EDE7FA;}"
        ).arg(backgroundColor,foreground,purple,accent)
         .arg(qRound(32 * styleNumber(settings.data,"number.doc.scale",.9)))
         .arg(qRound(18 * styleNumber(settings.data,"number.doc.scale",.9)))
         .arg(qRound(23 * styleNumber(settings.data,"number.doc.scale",.9))));
    updateDeepSeek();
    if(dailyPage)dailyPage->configure(database,settings.data);
    if (reviewActive && !review.sentence.isEmpty())
        reviewWord->setText(EntryRenderer::reviewSentence(review.sentence, review.sentenceSegments, settings.data));
    if(!currentEntry.isEmpty()){int scroll=browser->verticalScrollBar()->value();browser->setHtml(EntryRenderer::render(currentEntry,settings.data,selectedPart));browser->verticalScrollBar()->setValue(scroll);}
    else showMessage(messageTitle,messageText);
    if(restoreWindow) {
        auto saved=settings.data.value("Window").toObject();
        int width=qRound(saved.value("Width").toDouble(styleNumber(settings.data,"number.mainwindow.window.1.width",650)));
        int height=qRound(saved.value("Height").toDouble(styleNumber(settings.data,"number.mainwindow.window.1.height",689)));
        QScreen *screen=QGuiApplication::primaryScreen();
        QPoint desired(saved.value("Left").toInt(100),saved.value("Top").toInt(100));
        for(auto *s:QGuiApplication::screens())if(s->availableGeometry().contains(desired)){screen=s;break;}
        QRect area=screen?screen->availableGeometry():QRect(0,0,1920,1080);
        int minWidth=qMin(area.width(),qRound(styleNumber(settings.data,"number.mainwindow.window.1.minwidth",480)));
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
    const auto oldSource=sourceSettings(settings.data);const bool oldAi=useDeepSeek();
    try {
        if(!settings.reload())return;
        if(settings.databasePath()!=database.path())requestIdentityChange();
        if(oldSource!=sourceSettings(settings.data)){++requestId;emit cancelOnline();query->setText(QStringLiteral("查询"));}
        applySettings(false);
        if(oldAi!=useDeepSeek()&&initialized&&!switchingIdentity)lookup(false,false,false,true);
    }catch(const std::exception &e){toast(QStringLiteral("设置未应用：")+QString::fromUtf8(e.what()),true);}
    rearm();
}
void MainWindow::requestIdentityChange(){
    if(switchingIdentity||pendingClose)return;
    dailyPage->cancel();
    switchingIdentity=true;initialized=false;++requestId;++bookRevision;emit cancelOnline();inputDelay->stop();filterDelay->stop();
    if (reviewActive) leaveReview(pages->currentIndex() == 0 ? 0 : 1);
    if(syncCanceled)syncCanceled->store(true);
    pages->setEnabled(false);currentEntry={};selectedPart.clear();book->clear();
    showMessage(QStringLiteral("正在切换词库"),settings.username());
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
        const bool ai=useDeepSeek();
        struct Initial{HistorySelection selection;bool dirty=false;};
        ++pendingWrites;
        background(this,[nextDatabase,nextLock,ai]()mutable{nextDatabase.initialize();return Initial{nextDatabase.navigateForSource(0,ai),nextDatabase.revision()>nextDatabase.syncedRevision()};},
            [this,nextDatabase,nextLock,targetPath,targetUser,ai](Initial initial){
                writeFinished();
                if(pendingClose)return;
                if(settings.databasePath()!=targetPath){QTimer::singleShot(0,this,&MainWindow::changeIdentity);return;}
                dailyPage->configure(nextDatabase,settings.data);
                database=nextDatabase;profileLock=nextLock;activeUsername=targetUser;offset=0;total=0;firstDirtyAt=0;dirtyGeneration=0;
                switchingIdentity=false;initialized=true;pages->setEnabled(true);input->setEnabled(true);query->setEnabled(true);deepSeek->setEnabled(true);star->setEnabled(false);copy->setEnabled(false);query->setText(QStringLiteral("查询"));
                currentEntry={};selectedPart.clear();
                showSelection(initial.selection);if(ai!=useDeepSeek())lookup(false,false,false,true);
                if(pages->currentIndex()==3)dailyPage->activate();
                if(initial.dirty)dirty();updateHistory();if(pages->currentIndex()==1)refreshBook();toast(QStringLiteral("已切换用户词库：")+targetUser);
                if(settings.data.value("CacheSync").toObject().value("Enabled").toBool())QTimer::singleShot(800,this,[this]{if(!pendingClose)runSync(true);});
            },[this](QString error){writeFinished();if(pendingClose)return;switchingIdentity=false;pages->setEnabled(true);toast(error,true);initialize();});
    }catch(const std::exception &e){switchingIdentity=false;pages->setEnabled(true);toast(QString::fromUtf8(e.what()),true);initialize();}
}
bool MainWindow::useDeepSeek() const {return settings.data.value("DeepSeek").toObject().value("Enabled").toBool(false);}
void MainWindow::updateDeepSeek(){
    const bool ai=useDeepSeek();const auto source=ai?QStringLiteral("DeepSeek"):QStringLiteral("在线词典");
    deepSeek->setIcon(vectorIcon(ai?"whale":"online",ai?QColor("#4D6BFE"):QColor(Qt::white)));
    deepSeek->setProperty("lookupSource",ai?"DeepSeek":"Online");
    deepSeek->setAccessibleName(QStringLiteral("查询与显示来源：%1；点击切换为%2").arg(source,ai?QStringLiteral("在线词典"):QStringLiteral("DeepSeek")));
    query->setAccessibleName(QStringLiteral("使用%1查询并更新释义").arg(source));
}

void MainWindow::lookup(bool force,bool manual,bool link,bool cacheOnly) {
    if(!initialized||pendingClose)return;
    inputDelay->stop();QString text=input->text().trimmed();
    ++requestId;emit cancelOnline();quint64 revision=requestId;
    if(text.isEmpty()){showSourcePlaceholder({});return;}
    const bool ai=useDeepSeek();
    if(cacheOnly&&!currentEntry.isEmpty()&&isDeepSeek(currentEntry)!=ai)showSourcePlaceholder(text,true);
    if(text.toUtf8().size()>500){if(cacheOnly)showSourcePlaceholder({});query->setText(QStringLiteral("查询"));toast(QStringLiteral("单次查询最多 500 个 UTF-8 字节，请缩短输入。"),true);return;}
    const QString pair=detectPair(text);
    if(removingWords.contains(identity(text,pair))){if(cacheOnly)showSourcePlaceholder({});query->setText(QStringLiteral("查询"));toast(QStringLiteral("正在移除该词，请稍后查询。"));return;}
    auto db=database;
    if(!cacheOnly)query->setText(QStringLiteral("查询中"));
    struct Cached{QJsonObject entry;bool existingAi=false;};
    background(this,[db,text,pair,ai,manual,force]{Cached c;if(!force||(ai&&!manual))c.entry=db.findForSource(text,pair,ai);if(ai&&manual&&force)c.existingAi=db.hasDeepSeek(text,pair);return c;},
        [this,revision,text,ai,manual,force,link,cacheOnly](Cached cached){
        if(revision!=requestId||pendingClose)return;
        if(cacheOnly){if(cached.entry.isEmpty())showSourcePlaceholder(text);else showEntry(cached.entry);return;}
        if(ai&&manual&&force&&cached.existingAi){
            QMessageBox box(QMessageBox::Question,QStringLiteral("再次查询 DeepSeek"),QStringLiteral("“%1”已有 DeepSeek 释义。再次查询将调用 API 并更新内容，是否继续？").arg(text),QMessageBox::Yes|QMessageBox::No,this);box.setDefaultButton(QMessageBox::No);box.button(QMessageBox::Yes)->setText(QStringLiteral("再次查询"));box.button(QMessageBox::No)->setText(QStringLiteral("取消"));
            if(box.exec()!=QMessageBox::Yes){if(revision==requestId)query->setText(QStringLiteral("查询"));return;}
            if(revision!=requestId)return;
        }
        const bool cachedAllowed=!force||(ai&&!manual);
        if(cachedAllowed&&!cached.entry.isEmpty()){
            auto db=database;QJsonObject entry=cached.entry;++pendingWrites;
            background(this,[db,entry]()mutable{db.store(entry,true);return true;},[this,revision,entry](bool){writeFinished();if(revision==requestId)showEntry(entry);updateHistory();},[this,revision](QString e){writeFinished();if(revision==requestId){query->setText(QStringLiteral("查询"));toast(e,true);}});return;
        }
        if(ai&&!manual&&!link){
            showSourcePlaceholder(text);
            return;
        }
        launchOnline(revision,text,ai);
    },[this,revision](QString e){if(revision==requestId){query->setText(QStringLiteral("查询"));toast(e,true);}});
}
void MainWindow::launchOnline(quint64 revision,const QString &text,bool ai){
    if(revision!=requestId)return;query->setText(QStringLiteral("查询中"));
    if(currentEntry.value("Word").toString()!=text){currentEntry={};selectedPart.clear();star->setEnabled(false);copy->setEnabled(false);showMessage(text,QStringLiteral("正在查询…"));}
    emit onlineLookup(revision,text,settings.data,ai);
}
void MainWindow::receiveEntry(quint64 revision,QJsonObject entry){
    if(revision!=requestId||pendingClose||removingWords.contains(identity(entry.value("Word").toString(),entry.value("Pair").toString())))return;
    auto db=database;const bool ai=useDeepSeek();++pendingWrites;
    background(this,[db,entry,ai]()mutable{db.store(entry,true);return db.findForSource(entry.value("Word").toString(),entry.value("Pair").toString(),ai);},[this,revision](QJsonObject stored){writeFinished();dirty();if(revision==requestId&&!stored.isEmpty())showEntry(stored);updateHistory();},[this,revision](QString e){writeFinished();if(revision==requestId){query->setText(QStringLiteral("查询"));toast(e,true);}});
}
void MainWindow::showEntry(const QJsonObject &entry,bool refreshFavorite){
    bool changed=currentEntry.value("Word")!=entry.value("Word");currentEntry=entry;if(changed)selectedPart.clear();
    updateFonts(!entry.value("Record").toObject().value("Phonetic").toString().isEmpty());
    if(!selectedPart.isEmpty()){bool found=false;for(auto meaning:entry.value("Record").toObject().value("Meanings").toArray())if(meaning.toObject().value("PartOfSpeech").toString()==selectedPart){found=true;break;}if(!found)selectedPart.clear();}
    browser->setHtml(EntryRenderer::render(entry,settings.data,selectedPart));query->setText(QStringLiteral("查询"));copy->setEnabled(true);star->setEnabled(false);
    if(refreshFavorite)refreshFavoriteState();
}
void MainWindow::refreshFavoriteState(){
    star->setEnabled(false);if(currentEntry.isEmpty())return;
    auto db=database;const auto word=currentEntry.value("Word").toString(),pair=currentEntry.value("Pair").toString();
    if(favoriteWrites.contains(identity(word,pair)))return;
    const auto id=requestId,version=favoriteRevision;
    background(this,[db,word,pair]{return db.isFavorite(word,pair);},[this,id,version,word,pair](bool saved){
        if(id!=requestId||version!=favoriteRevision||identity(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString())!=identity(word,pair))return;
        favorite=saved;star->setIcon(vectorIcon(saved?"star-filled":"star",QColor("#8A67C1")));star->setEnabled(true);
    },[this,id,version](QString e){if(id==requestId&&version==favoriteRevision)toast(e,true);});
}
void MainWindow::updateHistory(){if(!initialized)return;auto db=database;background(this,[db]{return QPair<bool,bool>(db.canNavigate(-1),db.canNavigate(1));},[this](QPair<bool,bool> state){back->setEnabled(state.first);forward->setEnabled(state.second);},[this](QString e){toast(e,true);});}
void MainWindow::navigate(int direction){
    if(!initialized||pendingClose)return;inputDelay->stop();++requestId;emit cancelOnline();auto id=requestId;auto db=database;const bool ai=useDeepSeek();query->setText(QStringLiteral("查询中"));
    background(this,[db,direction,ai]()mutable{return db.navigateForSource(direction,ai);},[this,id](HistorySelection selection){if(id!=requestId)return;query->setText(QStringLiteral("查询"));if(!selection.word.isEmpty())showSelection(selection);updateHistory();},[this,id](QString e){if(id==requestId){query->setText(QStringLiteral("查询"));toast(e,true);}});
}
void MainWindow::favoriteCurrent(){
    if(!initialized||currentEntry.isEmpty()||!star->isEnabled())return;
    writeFavorite(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString(),!favorite);
}
void MainWindow::writeFavorite(QString word,QString pair,bool saved){
    const auto key=identity(word,pair);if(favoriteWrites.contains(key))return;
    favoriteWrites.insert(key);if(identity(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString())==key){++favoriteRevision;star->setEnabled(false);}
    auto db=database;++pendingWrites;
    background(this,[db,word,pair,saved]()mutable{db.setFavorite(word,pair,saved);return true;},[this,key,saved](bool){
        favoriteWrites.remove(key);writeFinished();dirty();
        if(!currentEntry.isEmpty()&&identity(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString())==key){++favoriteRevision;favorite=saved;star->setIcon(vectorIcon(saved?"star-filled":"star",QColor("#8A67C1")));star->setEnabled(true);}
        if(pages->currentIndex()==1)refreshBook();
    },[this,key](QString e){favoriteWrites.remove(key);writeFinished();if(identity(currentEntry.value("Word").toString(),currentEntry.value("Pair").toString())==key){++favoriteRevision;refreshFavoriteState();}toast(e,true);if(pages->currentIndex()==1)refreshBook();});
}

void MainWindow::refreshBook(){
    if(!initialized||pendingClose)return;
    const auto revision=++bookRevision;auto db=database;QString search=filter->text().trimmed();bool only=true;int start=offset;
    background(this,[db,search,only,start]{return db.page(search,only,start,50);},[this,revision](BookPage page){
        if(revision!=bookRevision||pendingClose)return;total=page.total;
        if(total==0)offset=0;
        if(total>0&&offset>=total){offset=((total-1)/50)*50;refreshBook();return;}
        book->clear();
        double wordSize=styleNumber(settings.data,"number.book.word_size",12.6),translationSize=styleNumber(settings.data,"number.book.translation_size",10.8);
        int wordHeight=qMax(qRound(styleNumber(settings.data,"number.book.word_line_height",15.3)),qRound(wordSize+2));
        int translationHeight=qMax(qRound(styleNumber(settings.data,"number.book.translation_line_height",13.77)),qRound(translationSize+2));
        const QString family=AppFonts::resolve(styleValue(settings.data,"font.doc.body","Microsoft YaHei UI"));
        auto padding=styleValue(settings.data,"thickness.book.row_padding","8,2").split(',');
        int left=8,right=8,top=2,bottom=2;
        if(padding.size()==2){left=right=qBound(0,padding[0].toInt(),30);top=bottom=qBound(0,padding[1].toInt(),30);}
        else if(padding.size()==4){left=qBound(0,padding[0].toInt(),30);top=qBound(0,padding[1].toInt(),30);right=qBound(0,padding[2].toInt(),30);bottom=qBound(0,padding[3].toInt(),30);}
        int rowNumber=0;
        for(const auto &row:page.rows){
            auto *item=new QListWidgetItem;item->setData(Qt::UserRole,row.id);item->setData(Qt::UserRole+1,row.word);item->setData(Qt::UserRole+2,row.pair);
            auto *widget=new QWidget;widget->setObjectName("bookRow");widget->setStyleSheet("QWidget#bookRow{background:transparent;}");
            auto *rowLayout=new QHBoxLayout(widget);rowLayout->setContentsMargins(left,top,right,bottom);rowLayout->setSpacing(5);
            auto *number=new QLabel(QString::number(++rowNumber));number->setObjectName("bookRowNumber");number->setAlignment(Qt::AlignCenter);number->setFixedSize(26,24);number->setAttribute(Qt::WA_TransparentForMouseEvents);number->setStyleSheet("color:#6A568C;background:#EEE8F6;border-radius:4px;");rowLayout->addWidget(number,0,Qt::AlignVCenter);
            auto *text=new QWidget;auto *textLayout=new QVBoxLayout(text);textLayout->setContentsMargins(0,0,0,0);textLayout->setSpacing(0);
            auto *word=new QLabel(row.word);word->setTextFormat(Qt::PlainText);word->setMinimumWidth(0);word->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);word->setAttribute(Qt::WA_TransparentForMouseEvents);QFont wf(family);wf.setPixelSize(qRound(wordSize));wf.setWeight(QFont::DemiBold);word->setFont(wf);word->setFixedHeight(qMax(wordHeight,QFontMetrics(wf).height()));word->setStyleSheet("color:#34255C;background:transparent;");
            auto *summary=new QLabel(row.summary);summary->setTextFormat(Qt::PlainText);summary->setMinimumWidth(0);summary->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);summary->setAttribute(Qt::WA_TransparentForMouseEvents);QFont tf(family);tf.setPixelSize(qRound(translationSize));summary->setFont(tf);summary->setFixedHeight(qMax(translationHeight,QFontMetrics(tf).height()));summary->setStyleSheet("color:#777387;background:transparent;");
            textLayout->addWidget(word);textLayout->addWidget(summary);text->setAttribute(Qt::WA_TransparentForMouseEvents);rowLayout->addWidget(text,1);
            auto *sources=new QWidget;auto *sourceLayout=new QHBoxLayout(sources);sourceLayout->setContentsMargins(0,0,0,0);sourceLayout->setSpacing(5);
            if(row.hasAi){auto *icon=new QLabel;icon->setObjectName("aiSource");icon->setPixmap(vectorIcon("whale",QColor("#4D6BFE"),18).pixmap(18,18));icon->setFixedSize(18,24);icon->setAlignment(Qt::AlignCenter);icon->setAccessibleName(QStringLiteral("有 DeepSeek 释义"));sourceLayout->addWidget(icon,0,Qt::AlignVCenter);}
            if(row.hasOnline){auto *icon=new QLabel;icon->setObjectName("onlineSource");icon->setPixmap(vectorIcon("online",QColor("#8E83A7"),16).pixmap(16,16));icon->setFixedSize(18,24);icon->setAlignment(Qt::AlignCenter);icon->setAccessibleName(QStringLiteral("有在线词典释义"));sourceLayout->addWidget(icon,0,Qt::AlignVCenter);}
            sources->setAttribute(Qt::WA_TransparentForMouseEvents);rowLayout->addWidget(sources,0,Qt::AlignVCenter);
            auto *save=new QPushButton;save->setObjectName("rowFavorite");save->setProperty("wordId",row.id);save->setIcon(vectorIcon(row.favorite?"star-filled":"star",QColor("#8A67C1"),18));save->setIconSize(QSize(18,18));save->setFixedSize(26,26);save->setStyleSheet("QPushButton{background:transparent;padding:0;}QPushButton:hover{background:#E8DFF4;}");save->setAccessibleName(row.favorite?QStringLiteral("取消收藏 ")+row.word:QStringLiteral("收藏 ")+row.word);rowLayout->addWidget(save,0,Qt::AlignVCenter);
            connect(save,&QPushButton::clicked,this,[this,row,save]{save->setEnabled(false);setRowFavorite(row);});
            item->setSizeHint(QSize(0,qMax(26,word->height()+summary->height())+top+bottom));book->addItem(item);book->setItemWidget(item,widget);
        }
        const int count=qMax(1,(total+49)/50);{QSignalBlocker blocker(pageJump);pageJump->setRange(1,count);pageJump->setValue(offset/50+1);}pageLabel->setText(QString("/ %1 · %2").arg(count).arg(total));previous->setEnabled(offset>0);next->setEnabled(offset+50<total);
        emptyBook->setText(filter->text().isEmpty()?(QStringLiteral("暂无收藏")):QStringLiteral("没有匹配的单词"));emptyBook->setGeometry(book->rect());emptyBook->setVisible(page.rows.isEmpty());emptyBook->raise();
    },[this,revision](QString e){if(revision==bookRevision)toast(e,true);});
}
void MainWindow::setRowFavorite(BookRow row){
    if(!initialized||pendingClose)return;writeFavorite(row.word,row.pair,!row.favorite);
}
void MainWindow::leaveReview(int page) {
    reviewActive = false;
    ++reviewRevision;
    review = {};
    reviewBusy = false;
    reviewAnswered = false;
    reviewAdvancePending = false;
    pages->setCurrentIndex(page);
    lookupTab->setChecked(page == 0);
    bookTab->setChecked(page == 1);
    if (page == 0) input->setFocus();
    else if (initialized) refreshBook();
}

void MainWindow::startReview() {
    if (!initialized || switchingIdentity || pendingClose) {
        toast(QStringLiteral("词库正在加载，请稍后开始复习。"));
        return;
    }
    ++requestId;
    emit cancelOnline();
    inputDelay->stop();
    query->setText(QStringLiteral("查询"));
    reviewActive = true;
    pages->setCurrentIndex(2);
    lookupTab->setChecked(false);
    bookTab->setChecked(true);
    nextReview();
}

void MainWindow::nextReview() {
    if (!initialized || switchingIdentity || pendingClose || reviewBusy) return;
    const auto revision = ++reviewRevision;
    review = {};
    reviewSelection = -1;
    reviewAnswered = false;
    reviewAdvancePending = false;
    reviewBusy = true;
    reviewWord->setTextFormat(Qt::PlainText);
    reviewWord->setText(QStringLiteral("正在准备题目…"));
    reviewUnfamiliar->setEnabled(false);
    reviewMode->setEnabled(false);
    const bool sentenceMode = reviewMode->currentIndex() == 1;
    reviewPrompt->setText(sentenceMode ? QString() : QStringLiteral("选择正确的释义"));
    reviewPrompt->setVisible(!sentenceMode);
    for (auto *choice : reviewChoices) {
        choice->hide();
        choice->setStyleSheet({});
    }
    auto db = database;
    background(this, [db,sentenceMode] { return db.reviewQuestion(sentenceMode); }, [this,revision](ReviewQuestion question) {
        if (revision != reviewRevision || pendingClose) return;
        reviewBusy = false;
        review = question;
        reviewUnfamiliar->setEnabled(true);
        reviewMode->setEnabled(true);
        reviewWord->setTextFormat(question.sentence.isEmpty() ? Qt::PlainText : Qt::RichText);
        reviewWord->setTextInteractionFlags(Qt::NoTextInteraction);
        reviewWord->setText(question.sentence.isEmpty() ? question.word : EntryRenderer::reviewSentence(question.sentence, question.sentenceSegments, settings.data));
        reviewWord->setProperty("sentenceQuestion", !question.sentence.isEmpty());
        reviewWord->style()->unpolish(reviewWord);
        reviewWord->style()->polish(reviewWord);
        reviewWord->updateGeometry();
        if (!question.sentence.isEmpty()) reviewPrompt->setText(question.sentenceChinese);
        for (int i = 0; i < reviewChoices.size(); ++i) {
            auto *choice = reviewChoices.at(i);
            const auto text = QStringLiteral("%1.  %2").arg(QChar('A' + i)).arg(question.options.at(i));
            auto *optionLabel = choice->findChild<QLabel *>();
            optionLabel->setText(text);
            const bool cloze = !question.sentence.isEmpty();
            optionLabel->setAlignment(cloze ? Qt::AlignLeft | Qt::AlignTop : Qt::AlignCenter);
            choice->layout()->setContentsMargins(cloze ? 8 : 14, 10, cloze ? 8 : 14, 10);
            auto *meaningLabel = choice->findChild<QLabel *>("reviewChoiceMeaning");
            auto meaningPolicy = meaningLabel->sizePolicy();
            meaningPolicy.setRetainSizeWhenHidden(cloze);
            meaningLabel->setSizePolicy(meaningPolicy);
            meaningLabel->setText(question.sentence.isEmpty() ? QString() :
                question.meanings.value(i).trimmed().isEmpty() ? QStringLiteral("暂无摘要释义") : question.meanings.at(i));
            meaningLabel->hide();
            choice->setAccessibleName(text);
            choice->setEnabled(true);
            choice->show();
        }
        if (pages->currentIndex() == 2) reviewChoices.first()->setFocus();
        reviewScroll->verticalScrollBar()->setValue(0);
    }, [this,revision](QString error) {
        if (revision != reviewRevision || pendingClose) return;
        reviewBusy = false;
        reviewWord->setText(QStringLiteral("暂时无法出题\n点击页面重试"));
        reviewUnfamiliar->setEnabled(true);
        reviewMode->setEnabled(true);
        toast(error, true);
    });
}

void MainWindow::advanceReview() {
    if (pages->currentIndex() != 2 || pendingClose) return;
    if (reviewBusy) {
        reviewAdvancePending = reviewAnswered;
        return;
    }
    if (reviewAnswered && !review.token.isEmpty()) {
        reviewAdvancePending = true;
        saveReviewAnswer();
    } else {
        nextReview();
    }
}

void MainWindow::chooseReviewAnswer(int selected) {
    if (reviewBusy || reviewAnswered || review.token.isEmpty() || pendingClose) return;
    reviewAnswered = true;
    reviewSelection = selected;
    if (!review.sentence.isEmpty()) {
        reviewPrompt->setText(review.sentenceChinese);
        reviewPrompt->setVisible(!review.sentenceChinese.trimmed().isEmpty());
    }
    for (int i = 0; i < reviewChoices.size(); ++i) {
        auto *choice = reviewChoices.at(i);
        choice->setEnabled(false);
        if (!review.sentence.isEmpty()) {
            const auto meaning = review.meanings.value(i).trimmed();
            const auto text = QStringLiteral("%1.  %2\n%3").arg(QChar('A' + i)).arg(review.options.at(i),
                meaning.isEmpty() ? QStringLiteral("暂无摘要释义") : meaning);
            choice->findChild<QLabel *>("reviewChoiceMeaning")->show();
            choice->setAccessibleName(text);
        }
        if (i == review.correctIndex) {
            choice->setStyleSheet(QStringLiteral(
                "QPushButton[reviewChoice=true]{background:#E7F4EB;border:2px solid #3F8D59;}"
                "QLabel{color:#245E37;}"));
        } else if (i == selected) {
            choice->setStyleSheet(QStringLiteral(
                "QPushButton[reviewChoice=true]{background:#FCE9ED;border:2px solid #B94D64;}"
                "QLabel{color:#8F2941;}"));
        }
    }
    saveReviewAnswer();
}

void MainWindow::saveReviewAnswer() {
    if (reviewBusy || !reviewAnswered || review.token.isEmpty() || pendingClose) return;
    const bool correct = reviewSelection == review.correctIndex;
    reviewBusy = true;
    reviewMode->setEnabled(false);
    const auto revision = reviewRevision;
    const auto question = review;
    const auto selected = reviewSelection;
    auto db = database;
    ++pendingWrites;
    QTimer::singleShot(0, this, [this,revision,correct] {
        if (!correct && revision == reviewRevision)
            reviewScroll->ensureWidgetVisible(reviewChoices.at(review.correctIndex), 0, 8);
    });
    background(this, [db,question,selected]() mutable { return db.answerReview(question, selected); },
        [this,revision,correct](bool saved) {
            writeFinished();
            if (revision != reviewRevision || pendingClose) return;
            reviewBusy = false;
            reviewMode->setEnabled(true);
            review.token.clear();
            if (!saved) toast(QStringLiteral("该词已取消收藏或移除。"));
            if ((correct && review.sentence.isEmpty()) || reviewAdvancePending) nextReview();
        }, [this,revision](QString error) {
            writeFinished();
            if (revision != reviewRevision || pendingClose) return;
            reviewBusy = false;
            reviewMode->setEnabled(true);
            toast(QStringLiteral("权重保存失败，点击页面重试。\n") + error, true);
        });
}

void MainWindow::dirty(){if(!syncing)sync->setText(QStringLiteral("待同步"));++dirtyGeneration;if(!firstDirtyAt)firstDirtyAt=QDateTime::currentMSecsSinceEpoch();}
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
    background(this,[directory,config,dbPath,user,pullOnly,canceled]{auto message=CacheSync::run(directory,config,dbPath,user,pullOnly,canceled);Database db(dbPath,user);return Synced{message,db.revision()>db.syncedRevision()};},[this,generation](Synced result){
        syncing=false;sync->setEnabled(true);sync->setText(QStringLiteral("已同步"));
        if(result.pending||generation!=dirtyGeneration){if(!firstDirtyAt)dirty();}else firstDirtyAt=0;
        if(result.pending||generation!=dirtyGeneration)sync->setText(QStringLiteral("待同步"));
        refreshBook();updateHistory();
        if(pages->currentIndex()==3)dailyPage->activate();
        // Refresh only the chosen source, including a previously empty source
        // that has just arrived through sync. Do not interrupt a query/debounce.
        if(!input->text().trimmed().isEmpty()&&!inputDelay->isActive()&&query->text()==QStringLiteral("查询"))lookup(false,false,false,true);
    },[this](QString error){syncing=false;sync->setEnabled(true);sync->setText(QStringLiteral("重试同步"));toast(QStringLiteral("同步失败：")+error+QStringLiteral("\n本地词库已保存，可点击重试。"),true);});
}
void MainWindow::exportBook(){
    if(!initialized)return;QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出单词本"),QDir::homePath()+"/MyDict.csv",QStringLiteral("CSV 文件 (*.csv)"));if(path.isEmpty())return;auto db=database;QString search=filter->text().trimmed();bool only=true;
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
    if (pages->currentIndex() == 2
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
        && (widget == pages->widget(2) || pages->widget(2)->isAncestorOf(widget))
        && widget != reviewExit && !reviewExit->isAncestorOf(widget)
        && widget != reviewMode && !reviewMode->isAncestorOf(widget)
        && (reviewAnswered || (!reviewBusy && review.token.isEmpty()))) {
        advanceReview();
        return true;
    }
    if(event->type()==QEvent::MouseMove){
        auto *e=static_cast<QMouseEvent*>(event);QPoint global=globalPoint(e);
        if(dragging){QPoint delta=global-dragStart;QRect target=dragGeometry;if(resizeEdges){if(resizeEdges&Qt::LeftEdge)target.setLeft(qMin(dragGeometry.left()+delta.x(),target.right()-minimumWidth()+1));if(resizeEdges&Qt::RightEdge)target.setRight(qMax(dragGeometry.right()+delta.x(),target.left()+minimumWidth()-1));if(resizeEdges&Qt::TopEdge)target.setTop(qMin(dragGeometry.top()+delta.y(),target.bottom()-minimumHeight()+1));if(resizeEdges&Qt::BottomEdge)target.setBottom(qMax(dragGeometry.bottom()+delta.y(),target.top()+minimumHeight()-1));setGeometry(target);}else if(!isMaximized())move(dragGeometry.topLeft()+delta);return true;}
    }
    if(event->type()==QEvent::MouseButtonRelease&&dragging){dragging=false;resizeEdges={};widget->releaseMouse();return true;}
    return QMainWindow::eventFilter(watched,event);
}
void MainWindow::closeEvent(QCloseEvent *event){
    dailyPage->cancel();
    if(syncCanceled)syncCanceled->store(true);
    ++requestId;emit cancelOnline();inputDelay->stop();filterDelay->stop();
    if(pendingWrites>0){pendingClose=true;input->setEnabled(false);query->setEnabled(false);star->setEnabled(false);toast(QStringLiteral("正在保存本地词库…"));event->ignore();return;}
    try{persistWindow();}catch(const std::exception &e){pendingClose=false;input->setEnabled(initialized);query->setEnabled(initialized);star->setEnabled(initialized&&!currentEntry.isEmpty());toast(QString::fromUtf8(e.what()),true);event->ignore();return;}
    event->accept();
}
}
