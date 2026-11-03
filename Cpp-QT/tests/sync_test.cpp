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
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QThread>
#include <QTimer>
#include <stdexcept>
#include <functional>
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
void editSql(QString path,std::function<void(QSqlDatabase &)> action){
    const auto name=QUuid::createUuid().toString();
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);if(!db.open())throw std::runtime_error("fixture sqlite");action(db);db.close();}
    QSqlDatabase::removeDatabase(name);
}
void oldQt(QString path){
    editSql(path,[](QSqlDatabase &db){QSqlQuery q(db);QVERIFY(q.exec("UPDATE metadata SET value='qt-json-v1' WHERE key='payload_format'"));
        QVERIFY(q.exec("SELECT source_key,payload FROM sources"));QList<QPair<QString,QByteArray>> rows;
        while(q.next())rows.append({q.value(0).toString(),q.value(1).toByteArray()});q.finish();
        for(const auto &row:rows){QVERIFY(q.prepare("UPDATE sources SET payload=? WHERE source_key=?"));
            q.addBindValue(QByteArray("MYDICT-QT-JSON1\n")+row.second.mid(13));q.addBindValue(row.first);QVERIFY(q.exec());}
        QVERIFY(q.exec("PRAGMA wal_checkpoint(TRUNCATE)"));});
}
void binaryCs(QString path){
    QDir().mkpath(QFileInfo(path).absolutePath());editSql(path,[](QSqlDatabase &db){QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"));
        QVERIFY(q.exec("INSERT INTO metadata VALUES('username','sync-test'),('schema','1')"));
        QVERIFY(q.exec("CREATE TABLE sources(payload BLOB)"));QVERIFY(q.exec("INSERT INTO sources VALUES(x'1f8b0800')"));});
}
void commitSeed(QString directory){
    git(directory,{"config","user.name","Sync fixture"});git(directory,{"config","user.email","test@example.invalid"});
    git(directory,{"add","--","users","qt-users"});git(directory,{"-c","commit.gpgsign=false","commit","-m","legacy fixture"});git(directory,{"push","origin","HEAD:main"});
}
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
        QVERIFY(tree.contains("users/sync-test/shared/qt-"));QCOMPARE(tree.split('\n').size(),2);QVERIFY(!tree.contains(".json"));QVERIFY(!tree.contains("qt-users/"));
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
    void sharedAndLegacySnapshotsMergeWithoutTouchingBinaryFiles(){
        QTemporaryDir temp;const auto repo=temp.filePath("remote.git"),seed=temp.filePath("seed");git(temp.path(),{"init","--bare","--initial-branch=main",repo});git(temp.path(),{"clone",repo,seed});
        const auto qtPath=QDir(seed).filePath("qt-users/sync-test/legacy.sqlite");Database qt(qtPath,"sync-test");qt.initialize();qt.store(entry("old-qt","old Qt"),false);qt.setFavorite("old-qt","en|zh-CN",true);oldQt(qtPath);
        Database cs(QDir(seed).filePath("users/sync-test/shared/cs-fixture.sqlite"),"sync-test");cs.initialize();cs.store(entry("from-csharp","shared C#"),false);
        Database top(QDir(seed).filePath("users/sync-test/old-json.sqlite"),"sync-test");top.initialize();top.store(entry("old-json","old JSON"),false);
        binaryCs(QDir(seed).filePath("users/sync-test/old-binary.sqlite"));commitSeed(seed);
        const auto before=git(repo,{"rev-parse","main:users/sync-test/old-binary.sqlite"});
        const auto local=temp.filePath("local");QDir().mkpath(local);Database db(QDir(local).filePath("words-shared.sqlite"),"sync-test");db.initialize();
        auto settings=config(repo,"a");const auto message=CacheSync::run(local,settings,db.path(),"sync-test",true);
        QVERIFY(!message.contains(QString::fromUtf8("旧 C#")));QCOMPARE(db.page({},false,0).total,3);QVERIFY(db.isFavorite("old-qt","en|zh-CN"));
        QCOMPARE(db.find("from-csharp","en|zh-CN")["Summary"].toString(),QString("shared C#"));
        const auto revision=db.revision();CacheSync::run(local,settings,db.path(),"sync-test",true);QCOMPARE(db.revision(),revision);
        db.store(entry("local-only","own shard restore"),false);CacheSync::run(local,settings,db.path(),"sync-test");
        QCOMPARE(git(repo,{"rev-parse","main:users/sync-test/old-binary.sqlite"}),before);
        const auto tree=git(repo,{"ls-tree","-r","--name-only","main"});QVERIFY(tree.contains("users/sync-test/shared/qt-"));QVERIFY(tree.contains("users/sync-test/shared/cs-fixture.sqlite"));QVERIFY(tree.contains("qt-users/sync-test/legacy.sqlite"));
        // A newly created local database with the same device seed must restore
        // its own uploaded shard rather than skipping it as an existing backup.
        QFile::remove(db.path());QFile::remove(db.path()+"-wal");QFile::remove(db.path()+"-shm");db.initialize();
        CacheSync::run(local,settings,db.path(),"sync-test",true);QCOMPARE(db.page({},false,0).total,4);
    }
    void binaryOnlyRepositoryExplainsCSharpUpgradeAndKeepsOriginal(){
        QTemporaryDir temp;const auto repo=temp.filePath("remote.git"),seed=temp.filePath("seed");git(temp.path(),{"init","--bare","--initial-branch=main",repo});git(temp.path(),{"clone",repo,seed});
        binaryCs(QDir(seed).filePath("users/sync-test/old-binary.sqlite"));QDir().mkpath(QDir(seed).filePath("qt-users"));
        git(seed,{"config","user.name","Sync fixture"});git(seed,{"config","user.email","test@example.invalid"});git(seed,{"add","users"});git(seed,{"-c","commit.gpgsign=false","commit","-m","binary fixture"});git(seed,{"push","origin","HEAD:main"});
        const auto before=git(repo,{"rev-parse","main:users/sync-test/old-binary.sqlite"});
        const auto local=temp.filePath("local");QDir().mkpath(local);Database db(QDir(local).filePath("words-shared.sqlite"),"sync-test");db.initialize();auto settings=config(repo,"a");
        auto note=CacheSync::run(local,settings,db.path(),"sync-test",true);QVERIFY(note.contains(QString::fromUtf8("新版 C#")));QCOMPARE(db.page({},false,0).total,0);
        CacheSync::run(local,settings,db.path(),"sync-test");QCOMPARE(git(repo,{"rev-parse","main:users/sync-test/old-binary.sqlite"}),before);
        QVERIFY(git(repo,{"ls-tree","-r","--name-only","main"}).contains("users/sync-test/shared/qt-"));
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
