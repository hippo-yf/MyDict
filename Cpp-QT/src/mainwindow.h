#pragma once
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
namespace MyDict {
class OnlineService;
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
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    Settings settings;
    Database database;
    QString activeUsername;
    std::shared_ptr<QLockFile> profileLock;
    std::shared_ptr<std::atomic_bool> syncCanceled;
    QJsonObject currentEntry;
    QString selectedPart;
    quint64 requestId = 0, bookRevision = 0, dirtyGeneration = 0;
    int pendingWrites = 0, offset = 0, total = 0;
    bool initialized = false, syncing = false, pendingClose = false, favorite = false, savingSettings = false, switchingIdentity = false;
    QSet<QString> removingWords;
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
    QCheckBox *favoritesOnly = nullptr;
    EntryBrowser *browser = nullptr;
    QListWidget *book = nullptr;
    QLabel *pageLabel = nullptr, *notice = nullptr, *emptyBook = nullptr;
    QPushButton *previous = nullptr, *next = nullptr;
    QTimer *inputDelay = nullptr, *filterDelay = nullptr, *noticeTimer = nullptr, *syncTimer = nullptr, *reloadDelay = nullptr;
    QFileSystemWatcher *configWatcher = nullptr;
    qint64 firstDirtyAt = 0;
    void createUi();
    void initialize();
    void applySettings(bool restoreWindow = false);
    void persistWindow();
    void reloadConfig();
    void requestIdentityChange();
    void changeIdentity();
    void updateDeepSeek();
    bool useDeepSeek() const;
    void lookup(bool force, bool manual, bool link = false);
    void launchOnline(quint64 revision, const QString &text, bool ai);
    void showEntry(const QJsonObject &entry, bool refreshFavorite = true);
    void receiveEntry(quint64 revision, QJsonObject entry);
    void updateHistory();
    void navigate(int direction);
    void favoriteCurrent();
    void refreshBook();
    void setRowFavorite(BookRow row);
    void removeRow(BookRow row);
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
