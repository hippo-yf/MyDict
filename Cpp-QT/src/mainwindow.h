#pragma once
#include <QSpinBox>
#include "core.h"
#include <QMainWindow>
#include <QJsonObject>
#include <QSet>
#include <QThread>
#include <memory>
#include <atomic>
class QLockFile;
class QLineEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class QListWidget;
class QCheckBox;
class QTimer;
class QFileSystemWatcher;
class QCloseEvent;
class QScrollArea;
class QComboBox;
namespace MyDict {
class OnlineService;
class DailyPage;
class EntryBrowser;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString dataDirectory, QWidget *parent = nullptr);
    ~MainWindow() override;
signals:
    void onlineLookup(quint64 requestId, QString text, QJsonObject settings, bool deepSeek);
    void cancelOnline();
protected:
    void showEvent(QShowEvent *event) override;
#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;
#else
    bool nativeEvent(const QByteArray &type, void *message, long *result) override;
#endif
#endif
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    Settings settings;
    Database database;
    QString activeUsername;
    std::shared_ptr<QLockFile> profileLock;
    std::shared_ptr<std::atomic_bool> syncCanceled;
    QJsonObject currentEntry;
    QString messageTitle, messageText;
    QString fontCoverageKey;
    bool needsChineseFallback = false;
    QString selectedPart;
    ReviewQuestion review;
    quint64 reviewRevision = 0;
    bool reviewActive = false, reviewAnswered = false, reviewBusy = false, reviewAdvancePending = false;
    int reviewSelection = -1;
    quint64 requestId = 0, bookRevision = 0, dirtyGeneration = 0, favoriteRevision = 0;
    int pendingWrites = 0, offset = 0, total = 0;
    bool startupScheduled = false, startupReady = false;
    bool initialized = false, syncing = false, pendingClose = false, favorite = false, savingSettings = false, switchingIdentity = false;
    QSet<QString> removingWords;
    QSet<QString> favoriteWrites;
    QThread networkThread;
    OnlineService *online = nullptr;
    QWidget *titleBar = nullptr;
    QPoint dragStart;
    QRect dragGeometry;
    Qt::Edges resizeEdges;
    bool dragging = false;
    QStackedWidget *pages = nullptr;
    QPushButton *lookupTab = nullptr, *bookTab = nullptr, *back = nullptr, *forward = nullptr;
    QPushButton *query = nullptr, *deepSeek = nullptr, *star = nullptr, *copy = nullptr, *sync = nullptr;
    QLineEdit *input = nullptr, *filter = nullptr;
    QSpinBox *pageJump = nullptr;
    EntryBrowser *browser = nullptr;
    DailyPage *dailyPage = nullptr;
    QPushButton *dailyTab = nullptr;
    QListWidget *book = nullptr;
    QLabel *pageLabel = nullptr, *notice = nullptr, *emptyBook = nullptr;
    QPushButton *previous = nullptr, *next = nullptr;
    QLabel *reviewWord = nullptr, *reviewPrompt = nullptr;
    QComboBox *reviewMode = nullptr;
    QList<QPushButton *> reviewChoices;
    QPushButton *reviewUnfamiliar = nullptr, *reviewExit = nullptr;
    QScrollArea *reviewScroll = nullptr;
    QTimer *inputDelay = nullptr, *filterDelay = nullptr, *noticeTimer = nullptr, *syncTimer = nullptr, *reloadDelay = nullptr;
    QFileSystemWatcher *configWatcher = nullptr;
    qint64 firstDirtyAt = 0;
    void createUi();
    void initialize();
    void applySettings(bool restoreWindow = false);
    void updateFonts(bool includePhonetic);
    void showMessage(const QString &title, const QString &message);
    void showSourcePlaceholder(const QString &word, bool loading = false);
    void showSelection(const HistorySelection &selection);
    void persistWindow();
    void reloadConfig();
    void requestIdentityChange();
    void changeIdentity();
    void updateDeepSeek();
    bool useDeepSeek() const;
    void lookup(bool force, bool manual, bool link = false, bool cacheOnly = false);
    void launchOnline(quint64 revision, const QString &text, bool ai);
    void showEntry(const QJsonObject &entry, bool refreshFavorite = true);
private slots:
    void receiveEntry(quint64 revision, QJsonObject entry);
private:
    void updateHistory();
    void navigate(int direction);
    void favoriteCurrent();
    void refreshFavoriteState();
    void writeFavorite(QString word, QString pair, bool saved);
    void refreshBook();
    void startReview();
    void nextReview();
    void advanceReview();
    void chooseReviewAnswer(int selected);
    void saveReviewAnswer();
    void leaveReview(int page = 1);
    void setRowFavorite(BookRow row);

    void dirty();
    void writeFinished();
    void toast(const QString &message, bool error = false);
    void runSync(bool pullOnly = false);
    void exportBook();
    void importBook();
    void adjustTypography(double fontDelta, double lineDelta);
    void setStyle(const QString &key, double value);
};
}
