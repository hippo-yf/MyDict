#pragma once
#include "core.h"
#include <QStringList>
namespace MyDict {
struct ImportOptions {
    QString input;
    int concurrency=1, limit=0, delayMs=1500, retries=2;
    bool forceRefresh=false, dryRun=false;
};
QStringList readImportWords(const QString &path);
// CLI only: drives an event loop, bounds requests and commits each completed word.
int importWordbook(Database database,QJsonObject settings,const ImportOptions &options);
}
