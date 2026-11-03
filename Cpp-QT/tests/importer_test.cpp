#include "importer.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QLockFile>
#include <QJsonObject>
#include <QElapsedTimer>
#include <stdexcept>

using namespace MyDict;
namespace {
void write(const QString &path,const QByteArray &data){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("fixture write");}
QJsonObject entry(QString word){return {{"Word",word},{"Pair","en|zh-CN"},{"Translation",QString::fromUtf8("测试翻译")},
    {"Summary",QString::fromUtf8("摘要")},{"LookupSource","DeepSeek"},{"TranslationProvider","DeepSeek"},
    {"QueryEndpoint","https://api.deepseek.com/chat/completions"},{"LookupModel","test-model"},
    {"SuccessfulQueryAtUtc",QString("2026-11-03T00:00:00.000Z")}};}
}
class ImporterTests: public QObject {
    Q_OBJECT
private slots:
    void numberedInputNormalizesAndDeduplicates(){
        QTemporaryDir temp;const auto path=temp.filePath("words.txt");
        write(path,QByteArray::fromHex("efbbbf")+QString::fromUtf8("1, Apple [ˈæpəl] 英译中\n2, apple [x] 重复\n3、well-known [x] 英译中\n4，go home\ninvalid line\n5, 123 [x] 不合法\n").toUtf8());
        const auto words=readImportWords(path);QCOMPARE(words,QStringList({"Apple","well-known","go home"}));
    }
    void dryRunUsesNoDatabaseAndNeedsNoKey(){
        QTemporaryDir temp;const auto path=temp.filePath("words.txt");write(path,"1, go [go] test\n");
        const auto dbPath=temp.filePath("not-created/words.sqlite");Database db(dbPath,"import-test");
        ImportOptions options;options.input=path;options.dryRun=true;options.concurrency=3;
        QCOMPARE(importWordbook(db,defaultSettings(),options),0);QVERIFY(!QFileInfo::exists(dbPath));
    }
    void cachedDeepSeekEntriesAreReusedAndFavorited(){
        QTemporaryDir temp;const auto path=temp.filePath("words.txt");write(path,"1, go [go] test\n2, home [home] test\n");
        Database db(temp.filePath("words.sqlite"),"import-test");db.initialize();db.store(entry("go"),false);db.store(entry("home"),false);
        auto settings=defaultSettings();auto ds=settings.value("DeepSeek").toObject();ds["ApiKey"]="not-real-unused";settings["DeepSeek"]=ds;
        ImportOptions options;options.input=path;options.delayMs=0;options.concurrency=2;
        QCOMPARE(importWordbook(db,settings,options),0);
        QVERIFY(db.isFavorite("go","en|zh-CN"));QVERIFY(db.isFavorite("home","en|zh-CN"));
        QCOMPARE(db.page({},true,0).total,2);QVERIFY(!db.canNavigate(-1));
    }
    void appLockProtectsRunningWindow(){
        QTemporaryDir temp;const auto path=temp.filePath("words.txt");write(path,"1, go [go] test\n");
        Database db(temp.filePath("words.sqlite"),"import-test");db.initialize();
        QLockFile lock(temp.filePath("app.lock"));QVERIFY(lock.tryLock());
        ImportOptions options;options.input=path;
        QVERIFY_EXCEPTION_THROWN(importWordbook(db,defaultSettings(),options),std::runtime_error);
    }
    void fatalConfigurationErrorStopsWaitingTasksImmediately(){
        QTemporaryDir temp;const auto path=temp.filePath("words.txt");write(path,"1, go [go] test\n2, home [home] test\n3, move [move] test\n");
        Database db(temp.filePath("words.sqlite"),"import-test");
        auto settings=defaultSettings();auto ds=settings.value("DeepSeek").toObject();ds["ApiKey"]="invalid key with whitespace";settings["DeepSeek"]=ds;
        ImportOptions options;options.input=path;options.concurrency=3;options.delayMs=30000;
        QElapsedTimer elapsed;elapsed.start();QCOMPARE(importWordbook(db,settings,options),1);
        QVERIFY(elapsed.elapsed()<2000);QCOMPARE(db.page({},false,0).total,0);
    }
    void invalidOptionsAndEmptyInput(){
        QTemporaryDir temp;const auto path=temp.filePath("words.txt");write(path,"unstructured text\n");
        QVERIFY_EXCEPTION_THROWN(readImportWords(path),std::runtime_error);
        Database db(temp.filePath("words.sqlite"),"import-test");ImportOptions options;options.input=path;options.limit=-1;
        QVERIFY_EXCEPTION_THROWN(importWordbook(db,defaultSettings(),options),std::runtime_error);
        options.limit=0;options.concurrency=33;QVERIFY_EXCEPTION_THROWN(importWordbook(db,defaultSettings(),options),std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(ImporterTests)
#include "importer_test.moc"
