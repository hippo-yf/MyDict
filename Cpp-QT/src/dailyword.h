#pragma once
#include "core.h"
#include <atomic>
#include <memory>
namespace MyDict {
class DailyWord {
public:
    static QString today(const QJsonObject &settings);
    static QJsonArray parseFeed(const QByteArray &xml,const QJsonObject &feed,const QDateTime &now);
    static QJsonObject validate(QJsonObject result,const QJsonArray &articles,const QStringList &recent,const QJsonObject &settings);
    static QJsonObject enrichSummary(Database db,QJsonObject article,QJsonObject settings,std::shared_ptr<std::atomic_bool> canceled);
    static QJsonObject enrichImage(Database db,QJsonObject article,QJsonObject settings,std::shared_ptr<std::atomic_bool> canceled);
    static QJsonArray validateSummary(QJsonArray rows,QString original);
    static QJsonObject generate(Database db,QJsonObject settings,std::shared_ptr<std::atomic_bool> canceled);
};
}
