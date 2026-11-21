#pragma once
#include "core.h"
#include <QWidget>
#include <atomic>
#include <memory>
#include <functional>
class QPushButton;class QComboBox;class QLabel;
namespace MyDict {
class EntryBrowser;
class DailyPage: public QWidget {
    Q_OBJECT
public:
    explicit DailyPage(QWidget *parent=nullptr);
    void configure(Database db,QJsonObject settings);
    void activate();
    void cancel();
    ~DailyPage() override;
signals:
    void lookupRequested(QString word);
    void workStarted();
    void workFinished();
    void changed();
private:
    std::shared_ptr<Database> database;
    QJsonObject settings,article;
    std::shared_ptr<std::atomic_bool> canceled;
    quint64 revision=0;
    bool busy=false,cacheLoaded=false;
    int historyOffset=0;
    QString autoDate;
    EntryBrowser *browser;
    QLabel *status;
    QPushButton *retry,*todayButton,*older,*newer;
    QComboBox *history;
    void run(std::function<QJsonObject()> job,std::function<void(QJsonObject)> done);
    void load(QString date={},bool generate=false);
    void loadHistory();
    void render();
    void setBusy(bool value);
};
}
