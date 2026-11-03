#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <stdexcept>

int main(int argc,char **argv) {
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc,argv);
    Q_INIT_RESOURCE(resources);
    QCoreApplication::setOrganizationName("hippo-yf");
    QCoreApplication::setApplicationName("MyDictQt");
    QCoreApplication::setApplicationVersion(MYDICT_VERSION);
    app.setWindowIcon(QIcon(":/app-icon.png"));
    app.setStyle("Fusion");
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("拾词 MyDict — C++ / Qt"));
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir",QStringLiteral("设置、fonts 和 profiles 所在目录"),"directory"});
    parser.addOption({"smoke-test",QStringLiteral("离屏启动并在两秒后退出，仅供构建验证")});
    parser.process(app);
    QString directory=parser.value("data-dir");
    if(directory.isEmpty()) {
#ifdef Q_OS_WIN
        directory=QCoreApplication::applicationDirPath();
#else
        directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
    }
    directory=QDir(directory).absolutePath(); QDir().mkpath(directory);
    try {
        // One window per data directory. Importers additionally lock its profile.
        QLockFile lock(QDir(directory).filePath("mydict-window.lock")); lock.setStaleLockTime(0);
        if(!lock.tryLock(0)) throw std::runtime_error("该数据目录已由另一个拾词窗口使用。");
        MyDict::MainWindow window(directory); window.show();
        if(parser.isSet("smoke-test")) QTimer::singleShot(2000,&app,&QCoreApplication::quit);
        return app.exec();
    } catch(const std::exception &e) {
        QMessageBox::critical(nullptr,QStringLiteral("拾词无法启动"),QString::fromUtf8(e.what()));
        return 1;
    }
}
