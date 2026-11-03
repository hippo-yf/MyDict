#include "sync.h"
#include "core.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QLockFile>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <stdexcept>
#include <thread>

using namespace MyDict;
namespace {
QString git(QString dir,QStringList args){
    QProcess p;p.setWorkingDirectory(dir);p.start("git",args);
    if(!p.waitForStarted(5000)||!p.waitForFinished(30000)||p.exitCode()!=0)
        throw std::runtime_error(("git fixture: "+QString::fromUtf8(p.readAllStandardError())).toUtf8().constData());
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
QJsonObject config(QString repo,QString device){
    auto settings=defaultSettings();auto sync=settings.value("CacheSync").toObject();
    sync["Repository"]=repo;sync["DeviceId"]=device;sync["PrivateKeyPath"]="";sync["KnownHostsPath"]="";
    settings["CacheSync"]=sync;return settings;
}
QJsonObject entry(QString word,QString summary="summary"){
    return {{"Word",word},{"Pair","en|zh-CN"},{"Translation",summary},{"Summary",summary},
        {"LookupSource","DeepSeek"},{"TranslationProvider","DeepSeek"},
        {"QueryEndpoint","https://api.deepseek.com/chat/completions"},{"LookupModel","test-model"},
        {"SuccessfulQueryAtUtc",QString("2026-11-03T00:00:00.000Z")}};
}
QString cacheRoot(QString dir,QString repository){
    auto hash=QString::fromLatin1(QCryptographicHash::hash(repository.toUtf8(),QCryptographicHash::Sha256).toHex().left(16));
    return QDir(dir).filePath("cache-sync/qt-"+hash);
}
void write(QString path,QByteArray bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw std::runtime_error("fixture write");}
struct EnvironmentGuard {
    QByteArray name,old;bool existed;
    EnvironmentGuard(QByteArray key,QByteArray value):name(key),old(qgetenv(key.constData())),existed(qEnvironmentVariableIsSet(key.constData())){qputenv(name.constData(),value);}
    ~EnvironmentGuard(){if(existed)qputenv(name.constData(),old);else qunsetenv(name.constData());}
};
}
class SyncTests:public QObject{
    Q_OBJECT
private slots:
    void alreadyCanceledDoesNotStartGitOrCreateFiles(){
        QTemporaryDir temp;const auto directory=temp.filePath("not-created");
        auto canceled=std::make_shared<std::atomic_bool>(true);QElapsedTimer elapsed;elapsed.start();
        QVERIFY_EXCEPTION_THROWN(CacheSync::run(directory,{},QDir(directory).filePath("words.sqlite"),"sync-test",false,canceled),std::runtime_error);
        QVERIFY(elapsed.elapsed()<1000);QVERIFY(!QFileInfo::exists(directory));
    }
    void cancellationKillsOnlyItsOwnWaitingGitProcess(){
        QTemporaryDir temp;const auto repo=temp.filePath("remote.git");git(temp.path(),{"init","--bare","--initial-branch=main",repo});
        Database db(temp.filePath("words.sqlite"),"sync-test");db.initialize();
        const auto binaries=temp.filePath("fake-bin");QDir().mkpath(binaries);
#ifdef Q_OS_WIN
        const auto fake=QDir(binaries).filePath("git.exe");
#else
        const auto fake=QDir(binaries).filePath("git");
#endif
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),fake));
        QFile::setPermissions(fake,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::ReadGroup|QFile::ExeGroup|QFile::ReadOther|QFile::ExeOther);
        EnvironmentGuard path("PATH",binaries.toUtf8()+QDir::listSeparator().toLatin1()+qgetenv("PATH"));
        EnvironmentGuard mode("MYDICT_TEST_FAKE_GIT_WAIT","1");
        auto canceled=std::make_shared<std::atomic_bool>(false);
        std::thread stop([canceled]{QThread::msleep(250);canceled->store(true);});
        QElapsedTimer elapsed;elapsed.start();QString message;
        try{CacheSync::run(temp.path(),config(repo,"a"),db.path(),"sync-test",false,canceled);}
        catch(const std::exception &e){message=QString::fromUtf8(e.what());}
        stop.join();QVERIFY2(message.contains(QString::fromUtf8("取消")),qPrintable(message));QVERIFY(elapsed.elapsed()<2000);
    }
    void twoDevicesConvergeAndPreserveDeletion(){
        QTemporaryDir temp;const auto repo=temp.filePath("remote.git");git(temp.path(),{"init","--bare","--initial-branch=main",repo});
        const auto aDir=temp.filePath("a"),bDir=temp.filePath("b");QDir().mkpath(aDir);QDir().mkpath(bDir);
        Database a(QDir(aDir).filePath("words.sqlite"),"sync-test"),b(QDir(bDir).filePath("words.sqlite"),"sync-test");a.initialize();b.initialize();
        a.store(entry("go"),true);a.setFavorite("go","en|zh-CN",true);
        const auto ac=config(repo,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),bc=config(repo,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        QVERIFY(!CacheSync::run(aDir,ac,a.path(),"sync-test").isEmpty());
        QCOMPARE(a.syncedRevision(),a.revision());
        CacheSync::run(bDir,bc,b.path(),"sync-test",true);QVERIFY(!b.find("go","en|zh-CN").isEmpty());QVERIFY(b.isFavorite("go","en|zh-CN"));
        b.setFavorite("go","en|zh-CN",false);const auto rows=b.page({},false,0);QCOMPARE(rows.total,1);QVERIFY(b.remove(rows.rows.at(0).id));
        CacheSync::run(bDir,bc,b.path(),"sync-test");CacheSync::run(aDir,ac,a.path(),"sync-test",true);
        QVERIFY(a.find("go","en|zh-CN").isEmpty());QVERIFY(!a.isFavorite("go","en|zh-CN"));
        CacheSync::run(aDir,ac,a.path(),"sync-test");CacheSync::run(bDir,bc,b.path(),"sync-test",true);
        QVERIFY(b.find("go","en|zh-CN").isEmpty());
        const auto tree=git(repo,{"ls-tree","-r","--name-only","main"});
        QVERIFY(tree.contains("qt-users/sync-test/"));QCOMPARE(tree.split('\n').size(),2);QVERIFY(!tree.contains(".json"));
    }
    void dirtyTrackedFileSurvivesRemotePull(){
        QTemporaryDir temp;const auto repo=temp.filePath("remote.git");git(temp.path(),{"init","--bare","--initial-branch=main",repo});
        const auto aDir=temp.filePath("a"),bDir=temp.filePath("b");QDir().mkpath(aDir);QDir().mkpath(bDir);
        Database a(QDir(aDir).filePath("words.sqlite"),"sync-test"),b(QDir(bDir).filePath("words.sqlite"),"sync-test");a.initialize();b.initialize();
        const auto ac=config(repo,"a"),bc=config(repo,"b");a.store(entry("one"),false);CacheSync::run(aDir,ac,a.path(),"sync-test");
        const auto root=cacheRoot(aDir,repo);
        // Keep the fixture deterministic across Git for Windows configurations;
        // otherwise autostash may legitimately check this text out as CRLF.
        git(root,{"config","core.autocrlf","false"});
        write(QDir(root).filePath("notes.txt"),"committed\n");
        git(root,{"add","notes.txt"});git(root,{"-c","commit.gpgsign=false","commit","-m","fixture notes"});git(root,{"push","origin","HEAD:main"});
        CacheSync::run(bDir,bc,b.path(),"sync-test",true);b.store(entry("two"),false);CacheSync::run(bDir,bc,b.path(),"sync-test");
        write(QDir(root).filePath("notes.txt"),"local unstaged edit\n");
        CacheSync::run(aDir,ac,a.path(),"sync-test",true);
        QFile f(QDir(root).filePath("notes.txt"));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("local unstaged edit\n"));
        QVERIFY(!a.find("two","en|zh-CN").isEmpty());
    }
    void syncLockAndInvalidRepositoryKeepLocalData(){
        QTemporaryDir temp;const auto repo=temp.filePath("remote.git");git(temp.path(),{"init","--bare","--initial-branch=main",repo});
        Database db(temp.filePath("words.sqlite"),"sync-test");db.initialize();db.store(entry("go"),false);
        const auto root=cacheRoot(temp.path(),repo);QDir().mkpath(root);QLockFile lock(QDir(root).filePath("sync.lock"));QVERIFY(lock.tryLock());
        QVERIFY_EXCEPTION_THROWN(CacheSync::run(temp.path(),config(repo,"a"),db.path(),"sync-test"),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(CacheSync::run(temp.path(),config("https://example.invalid/repo","a"),db.path(),"sync-test"),std::runtime_error);
        QVERIFY(!db.find("go","en|zh-CN").isEmpty());
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    // A private copy of this test executable acts as a sleeping Git subprocess;
    // no network, shell helper or unrelated process is involved in cancellation.
    if(qgetenv("MYDICT_TEST_FAKE_GIT_WAIT")=="1"&&QFileInfo(app.applicationFilePath()).completeBaseName()=="git"){
        QTimer::singleShot(30000,&app,&QCoreApplication::quit);return app.exec();
    }
    SyncTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "sync_test.moc"
