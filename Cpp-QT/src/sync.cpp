#include "sync.h"
#include "core.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSysInfo>
#include <stdexcept>

namespace MyDict {
namespace {
[[noreturn]] void error(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
QString hash(const QByteArray &data, int length) { return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(length)); }
// GIT_SSH_COMMAND is parsed by Git's POSIX shell, including Git for Windows.
// Double quotes inside single quotes are already literal; adding backslashes
// would change the value passed to OpenSSH's -o option parser.
QString quote(const QString &s) { QString x=s; x.replace('\'', QStringLiteral("'\\''")); return "'"+x+"'"; }
class Git {
public:
    QString directory; QProcessEnvironment environment;
    std::shared_ptr<std::atomic_bool> canceled;
    void checkCanceled() const {
        if(canceled && canceled->load()) error(QStringLiteral("已取消 Gitee 同步，本地 SQLite 已保留。"));
    }
    QString call(const QStringList &args, bool allowFailure=false) const {
        checkCanceled();
        QProcess p; p.setWorkingDirectory(directory); p.setProcessEnvironment(environment);
        p.start("git", args);
        QElapsedTimer elapsed;elapsed.start();
        while(p.state()==QProcess::Starting && !p.waitForStarted(100)) {
            if(canceled && canceled->load()) {p.kill();p.waitForFinished(500);checkCanceled();}
            if(elapsed.elapsed()>=5000) {p.kill();p.waitForFinished(500);error(QStringLiteral("无法启动 Git，请安装 Git 并加入 PATH。"));}
        }
        if(p.error()==QProcess::FailedToStart) error(QStringLiteral("无法启动 Git，请安装 Git 并加入 PATH。"));
        while(p.state()!=QProcess::NotRunning && !p.waitForFinished(100)) {
            if(canceled && canceled->load()) {p.kill();p.waitForFinished(500);checkCanceled();}
            if(elapsed.elapsed()>=45000) {p.kill();p.waitForFinished(500);error(QStringLiteral("Gitee 同步超时，本地 SQLite 已保留。"));}
        }
        checkCanceled();
        const auto output=QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        const auto message=QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (p.exitStatus()!=QProcess::NormalExit || p.exitCode()!=0) {
            if (allowFailure) return QString();
            error(QStringLiteral("Gitee 同步失败：")+message);
        }
        // A non-null empty string distinguishes success from failure.
        return output.isNull() ? QStringLiteral("") : output;
    }
    void rebase() const {
        call({"rebase","--autostash","origin/main"});
        if (!call({"diff","--name-only","--diff-filter=U"}).isEmpty())
            error(QStringLiteral("同步目录存在合并冲突，请处理后重试；本地数据已保留。"));
    }
};
}
QString CacheSync::run(QString directory,QJsonObject settings,QString dbPath,QString username,bool pullOnly,
                       std::shared_ptr<std::atomic_bool> canceled) {
    if(canceled && canceled->load()) error(QStringLiteral("已取消 Gitee 同步，本地 SQLite 已保留。"));
    const auto options=settings.value("CacheSync").toObject();
    const auto repository=options.value("Repository").toString();
    const bool local=QFileInfo(repository).isAbsolute() && QFileInfo(QDir(repository).filePath("HEAD")).isFile()
                     && QDir(QDir(repository).filePath("objects")).exists();
    if (!local && !QRegularExpression("^git@gitee\\.com:[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+\\.git$").match(repository).hasMatch())
        error(QStringLiteral("CacheSync.Repository 需为 Gitee SSH 仓库或本地 bare Git 仓库。"));
    if (!QRegularExpression("^[a-z0-9_-]{1,64}$").match(username).hasMatch()) error(QStringLiteral("username 无效。"));
    const auto root=QDir(directory).filePath("cache-sync/qt-"+hash(repository.toUtf8(),16));
    if (!QDir().mkpath(root)) error(QStringLiteral("无法创建同步目录。"));
    QLockFile lock(QDir(root).filePath("sync.lock")); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) error(QStringLiteral("另一项同步正在进行，请稍后重试。"));
    auto env=QProcessEnvironment::systemEnvironment(); env.insert("GIT_TERMINAL_PROMPT","0");
    QString ssh="ssh -o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes";
    for (const auto &key: {QStringLiteral("PrivateKeyPath"),QStringLiteral("KnownHostsPath")}) {
        QString path=options.value(key).toString();
        if(path.isEmpty()) continue;
        if(path.contains('\n') || path.contains('\r') || path.contains('"')) error(QStringLiteral("SSH 文件路径包含无效字符。"));
        if (QDir::isRelativePath(path)) path=QDir(directory).absoluteFilePath(path);
        if (!QFileInfo(path).isFile()) error(QStringLiteral("SSH 配置的文件不存在：")+key);
        path=QDir::fromNativeSeparators(path);
        if(key=="PrivateKeyPath") ssh+=" -o IdentitiesOnly=yes -i "+quote(path);
        else ssh+=" -o "+quote("UserKnownHostsFile=\""+path+"\"");
    }
    env.insert("GIT_SSH_COMMAND",ssh);
    Git git{root,env,canceled};
    if (!QDir(QDir(root).filePath(".git")).exists()) {
        git.call({"init","--initial-branch=main"}); git.call({"remote","add","origin",repository});
    }
    if (git.call({"config","--get","remote.origin.url"})!=repository) error(QStringLiteral("同步仓库地址不匹配。"));
    git.call({"config","user.name","MyDict Qt"}); git.call({"config","user.email","mydict@users.noreply.gitee.com"});
    Database db(dbPath,username); db.initialize();
    const auto folder=QDir(root).filePath("qt-users/"+username); QDir().mkpath(folder);
    auto pull=[&]() {
        git.call({"fetch","origin"});
        if (!git.call({"rev-parse","--verify","refs/remotes/origin/main"},true).isNull()) {
            if (!git.call({"rev-parse","--verify","HEAD"},true).isNull()) git.rebase();
            else git.call({"checkout","-B","main","origin/main"});
        }
        for (const auto &name: QDir(folder).entryList({"*.sqlite"},QDir::Files)) {
            git.checkCanceled();db.mergeFrom(QDir(folder).filePath(name));
        }
    };
    pull();
    if(pullOnly) return QStringLiteral("已拉取 Qt 词库，本地 SQLite 已保存。");
    QByteArray identity=QSysInfo::machineUniqueId()+"\n"+qgetenv("USER")+qgetenv("USERNAME")+"\n"+options.value("DeviceId").toString().toUtf8();
    const auto relative="qt-users/"+username+"/"+hash(identity,32)+".sqlite";
    // Read the revision before backup: any concurrent change remains pending.
    git.checkCanceled();const qint64 revision=db.revision(); db.backupTo(QDir(root).filePath(relative));
    git.call({"add","--",relative});
    if (!git.call({"diff","--cached","--name-only","--",relative}).isEmpty())
        git.call({"-c","commit.gpgsign=false","commit","-m","Sync Qt SQLite cache for "+username,"--only","--",relative});
    for(int attempt=0;attempt<3;++attempt) {
        if (!git.call({"push","origin","HEAD:main"},true).isNull()) { db.markSynced(revision); return QStringLiteral("同步完成，Qt 词库已保存到 Gitee。"); }
        pull();
        // Merge newly fetched content before publishing another local snapshot.
        db.backupTo(QDir(root).filePath(relative)); git.call({"add","--",relative});
        if (!git.call({"diff","--cached","--name-only","--",relative}).isEmpty())
            git.call({"-c","commit.gpgsign=false","commit","-m","Merge Qt SQLite snapshot","--only","--",relative});
    }
    error(QStringLiteral("Gitee 推送失败，本地数据及提交均已保留，可点击重试。"));
}
}
