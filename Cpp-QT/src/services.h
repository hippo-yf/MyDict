#pragma once

#include <QObject>
#include <QJsonObject>
#include <QSharedPointer>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;

namespace MyDict {

// Move the service to its own QThread before lookup(). All networking and JSON
// processing then stay on that thread; emitted objects use Qt's value semantics.
class OnlineService final : public QObject {
    Q_OBJECT
public:
    explicit OnlineService(QObject *parent = nullptr);
    ~OnlineService() override;
    static QJsonObject parseDeepSeek(QString content, QString word, QJsonObject settings);
    static QJsonObject parseFreeDictionary(QString word, QByteArray json);
    static QString dictionaryPrompt();
    // The manager must live on this service's thread. Intended for deterministic
    // transport tests; call before the first lookup.
    void setNetworkAccessManagerForTests(QNetworkAccessManager *manager);

public slots:
    void lookup(quint64 requestId, QString text, QJsonObject settings, bool deepSeek);
    void cancel();

signals:
    void ready(quint64 requestId, QJsonObject entry);
    void failed(quint64 requestId, QString message);

private:
    struct Job;
    using JobPtr = QSharedPointer<Job>;
    using Response = std::function<void(int, QByteArray, QString)>;
    using Translation = std::function<void(QString, QString)>;
    QNetworkAccessManager *network_ = nullptr;
    JobPtr job_;
    bool current(const JobPtr &job) const;
    void request(const JobPtr &job, QUrl url, QByteArray body,
                 QList<QPair<QByteArray,QByteArray>> headers, int timeoutMs,
                 Response callback);
    void fail(const JobPtr &job, QString error);
    void finish(const JobPtr &job);
    void deepSeek(const JobPtr &job, int attempt = 0, QString previous = {});
    void ordinary(const JobPtr &job);
    void translate(const JobPtr &job, QString text, QString pair, Translation callback);
    void dictionary(const JobPtr &job, QString english);
    void translateSenses(const JobPtr &job);
};

}
