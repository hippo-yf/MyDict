#include "core.h"
#include "dailyword.h"
#include "importer.h"
#include "sync.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <QJsonObject>
#include <stdexcept>
#include <cstdio>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); Q_INIT_RESOURCE(resources);
    app.setOrganizationName("hippo-yf"); app.setApplicationName("MyDictQt"); app.setApplicationVersion(MYDICT_VERSION);
    QCommandLineParser p; p.addHelpOption(); p.addVersionOption();
    p.addOption({"data-dir",QStringLiteral("settings.json 所在目录"),"directory"});
    p.addOption({"import-json",QStringLiteral("导入 C# 导出的 mydict-portable-v1 NDJSON"),"file"});
    p.addOption({"export-csv",QStringLiteral("导出当前词库 CSV"),"file"});
    p.addOption({"import-wordbook",QStringLiteral("通过 DeepSeek 导入编号词本并收藏"),"file"});
    p.addOption({"concurrency",QStringLiteral("并发查询数 1–32"),"count","1"});
    p.addOption({"limit",QStringLiteral("仅处理前 N 个；0=全部"),"count","0"});
    p.addOption({"delay-ms",QStringLiteral("请求全局启动间隔毫秒"),"ms","1500"});
    p.addOption({"retries",QStringLiteral("临时故障重试次数 0–5"),"count","2"});
    p.addOption({"force-refresh",QStringLiteral("忽略已有 DeepSeek 缓存，重新付费查询")});
    p.addOption({"dry-run",QStringLiteral("仅预览词本，不调用 API")});
    p.addOption({"favorites-only",QStringLiteral("CSV 仅导出收藏")});
    p.addOption({"daily-word",QStringLiteral("生成或读取今日词汇（使用已配置 DeepSeek）")});
    p.addOption({"check",QStringLiteral("检查数据库并显示词条数")});
    p.addOption({"sync",QStringLiteral("手动同步 Qt SQLite 快照")});
    p.process(app);
    try {
        QString dir=p.value("data-dir");
        if(dir.isEmpty()) {
#ifdef Q_OS_WIN
            dir=app.applicationDirPath();
#else
            dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
        }
        MyDict::Settings settings(QDir(dir).absolutePath()); settings.load();
        MyDict::Database db(settings.databasePath(),settings.username());
        if(p.isSet("import-wordbook")) {
            auto integer=[&](const char* key){bool ok=false;const int v=p.value(key).toInt(&ok);if(!ok)throw std::runtime_error("参数必须为整数。");return v;};
            MyDict::ImportOptions o; o.input=p.value("import-wordbook");o.concurrency=integer("concurrency");o.limit=integer("limit");
            o.delayMs=integer("delay-ms");o.retries=integer("retries");o.forceRefresh=p.isSet("force-refresh");o.dryRun=p.isSet("dry-run");
            return MyDict::importWordbook(db,settings.data,o);
        }
        db.initialize();
        if(p.isSet("daily-word")){auto article=MyDict::DailyWord::generate(db,settings.data,std::make_shared<std::atomic_bool>(false));article=MyDict::DailyWord::enrichSummary(db,article,settings.data,std::make_shared<std::atomic_bool>(false));article=MyDict::DailyWord::enrichImage(db,article,settings.data,std::make_shared<std::atomic_bool>(false));fprintf(stdout,"Daily word: %s %s\n",qPrintable(article.value("Date").toString()),qPrintable(article.value("Word").toString()));return 0;}
        if(p.isSet("import-json")) {db.importEntries(p.value("import-json"));qInfo()<<"Import complete.";}
        if(p.isSet("export-csv")) {db.exportCsv(p.value("export-csv"),{},p.isSet("favorites-only"));qInfo()<<"Export complete.";}
        if(p.isSet("sync")) qInfo().noquote()<<MyDict::CacheSync::run(settings.directory,settings.data,db.path(),settings.username(),false);
        if(p.isSet("check")) qInfo().noquote()<<QStringLiteral("Qt SQLite 词条：%1；收藏：%2；revision：%3").arg(db.page({},false,0,1).total).arg(db.page({},true,0,1).total).arg(db.revision());
        if(!p.isSet("check")&&!p.isSet("import-json")&&!p.isSet("export-csv")&&!p.isSet("sync")) p.showHelp();
        return 0;
    }catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}
}
