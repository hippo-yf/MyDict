#include "importer.h"
#include "services.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <QRegularExpression>
#include <QLockFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QPointer>
#include <functional>
#include <stdexcept>

namespace MyDict {
QStringList readImportWords(const QString &path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly|QIODevice::Text)) throw std::runtime_error("无法打开单词本输入文件。");
    QStringList words; QSet<QString> seen;
    const QRegularExpression numbered(QStringLiteral("^\\s*\\d+\\s*[,，.、]\\s*([^\\[\\t]+?)(?:\\s*\\[|\\t|$)"));
    const QRegularExpression valid(QStringLiteral("^[A-Za-z]+(?:[-' ][A-Za-z]+)*$"));
    while(!f.atEnd()) {
        QString line=QString::fromUtf8(f.readLine()).trimmed(); if(line.startsWith(QChar(0xfeff))) line.remove(0,1);
        const auto m=numbered.match(line); if(!m.hasMatch()) continue;
        const QString word=m.captured(1).trimmed(), key=normalizeWord(word);
        if(valid.match(word).hasMatch() && !seen.contains(key)) { words.append(word); seen.insert(key); }
    }
    if(words.isEmpty()) throw std::runtime_error("未找到编号词条。示例：1, word [音标] 英译中");
    return words;
}
int importWordbook(Database db,QJsonObject settings,const ImportOptions &options) {
    if(options.concurrency<1 || options.concurrency>32 || options.delayMs<0 || options.delayMs>300000 || options.retries<0 || options.retries>5 || options.limit<0)
        throw std::runtime_error("并发数为 1–32，重试为 0–5，间隔为 0–300000 毫秒，条数不能为负数。");
    auto words=readImportWords(options.input); const int unique=words.size();
    if(options.limit>0) words=words.mid(0,options.limit);
    qInfo().noquote()<<QStringLiteral("识别唯一词条：%1；本次：%2；并发：%3").arg(unique).arg(words.size()).arg(options.concurrency);
    if(options.dryRun) { qInfo().noquote()<<words.mid(0,12).join(" · "); return 0; }
    if(!QDir().mkpath(QFileInfo(db.path()).absolutePath())) throw std::runtime_error("无法创建导入词库目录。");
    QLockFile appLock(QDir(QFileInfo(db.path()).absolutePath()).filePath("app.lock")); appLock.setStaleLockTime(0);
    if(!appLock.tryLock(0)) throw std::runtime_error("请先关闭正在使用该词库的拾词窗口或其他导入进程。");
    db.initialize();
    const auto ds=settings.value("DeepSeek").toObject();
    if(ds.value("ApiKey").toString().trimmed().isEmpty()) throw std::runtime_error("请先在 settings.json 的 DeepSeek.ApiKey 填写密钥。");
    QEventLoop loop; QElapsedTimer clock; clock.start(); qint64 nextStart=0;
    int next=0,active=0,complete=0,errors=0,reused=0,consecutive=0;
    bool stopped=false; QString stopReason;
    QObject scope;
    struct Task { OnlineService *service=nullptr; QString word; int attempt=0; quint64 id=0;
        QPointer<QTimer> timer;bool finished=false; };
    QList<QSharedPointer<Task>> tasks;
    std::function<void()> pump;
    std::function<void()> stopWaiting;
    std::function<void(QSharedPointer<Task>,int)> start;
    auto done=[&](QSharedPointer<Task> task) {
        if(task->finished)return;task->finished=true;
        if(task->timer){task->timer->stop();task->timer->deleteLater();task->timer.clear();}
        tasks.removeOne(task); task->service->deleteLater(); --active; ++complete;
        qInfo().noquote()<<QStringLiteral("[%1/%2] %3 | 复用 %4 / 失败 %5").arg(complete).arg(words.size()).arg(task->word).arg(reused).arg(errors);
        QTimer::singleShot(0,&scope,[&]{pump();});
    };
    stopWaiting=[&]{const auto waiting=tasks;for(const auto &task:waiting)if(task->timer)done(task);};
    start=[&](QSharedPointer<Task> task,int backoff) {
        const qint64 at=qMax(clock.elapsed()+backoff,nextStart); nextStart=at+options.delayMs;
        auto timer=new QTimer(&scope);timer->setSingleShot(true);task->timer=timer;
        QObject::connect(timer,&QTimer::timeout,&scope,[&,task,timer]{
            timer->deleteLater();task->timer.clear();
            if(stopped) { done(task); return; }
            task->service->lookup(++task->id,task->word,settings,true);
        });
        timer->start(int(qBound<qint64>(qint64(0),at-clock.elapsed(),qint64(2147483647))));
    };
    pump=[&] {
        try { while(!stopped && next<words.size() && active<options.concurrency) {
            const auto word=words[next++];
            if(!options.forceRefresh) {
                const auto entry=db.findDeepSeek(word,detectPair(word));
                if(!entry.isEmpty()) { db.saveFavorite(entry); ++reused; ++complete; qInfo().noquote()<<QStringLiteral("[%1/%2] %3 | 复用缓存").arg(complete).arg(words.size()).arg(word); continue; }
            }
            auto task=QSharedPointer<Task>::create(); task->word=word; task->service=new OnlineService(&scope); tasks.append(task); ++active;
            QObject::connect(task->service,&OnlineService::ready,&scope,[&,task](quint64,QJsonObject entry) {
                try { db.saveFavorite(entry); consecutive=0; }
                catch(const std::exception &e) { stopped=true; stopReason=QString::fromUtf8(e.what()); ++errors; }
                done(task);
                if(stopped)stopWaiting();
            });
            QObject::connect(task->service,&OnlineService::failed,&scope,[&,task](quint64,QString message) {
                const bool fatal=message.contains("401")||message.contains("402")||message.contains("403")||message.contains(QStringLiteral("密钥"))||message.contains(QStringLiteral("余额"));
                const bool retryable=message.contains("429")||QRegularExpression("HTTP 5[0-9]{2}").match(message).hasMatch()||message.contains(QStringLiteral("超时"))||message.contains(QStringLiteral("网络"));
                if(!stopped && !fatal && retryable && task->attempt<options.retries) { ++task->attempt; start(task,qMin(30000,2000*task->attempt)); return; }
                ++errors; ++consecutive;
                try { db.recordImportError(task->word,message); }
                catch(const std::exception &e) { stopped=true; stopReason=QString::fromUtf8(e.what()); }
                qWarning().noquote()<<task->word+": "+message;
                if(fatal||consecutive>=5) { stopped=true; stopReason=message; }
                done(task);
                if(stopped)stopWaiting();
            });
            start(task,0);
        }
        } catch(const std::exception &e) { stopped=true; stopReason=QString::fromUtf8(e.what()); ++errors;stopWaiting(); }
        if(active==0 && (stopped||next>=words.size())) loop.quit();
    };
    QTimer::singleShot(0,&scope,[&]{pump();}); loop.exec();
    if(stopped) { qWarning().noquote()<<QStringLiteral("已停止，完成结果已保存在 SQLite。")+stopReason; return 1; }
    return errors ? 2:0;
}
}
