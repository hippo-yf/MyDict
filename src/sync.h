#pragma once
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <memory>
namespace MyDict {
class CacheSync {
public:
    static QString run(QString directory, QJsonObject settings, QString dbPath,
                       QString username, bool pullOnly = false,
                       std::shared_ptr<std::atomic_bool> canceled = {});
};
}
