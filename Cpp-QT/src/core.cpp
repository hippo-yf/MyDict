#include "core.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace MyDict {
namespace {
constexpr qint64 EpochTicks = Q_INT64_C(621355968000000000);
constexpr qint64 TicksPerSecond = Q_INT64_C(10000000);
const QString PayloadFormat = QStringLiteral("qt-json-v1");
const QByteArray PayloadPrefix = QByteArrayLiteral("MYDICT-QT-JSON1\n");

[[noreturn]] void fail(const QString &message) {
    throw std::runtime_error(message.toUtf8().constData());
}
QString nonNull(QString value) { return value.isNull() ? QStringLiteral("") : value; }
QString isoNow() {
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz")) + QStringLiteral("0000Z");
}
qint64 timeTicks(const QString &text) {
    if (text.isEmpty()) return 0;
    // Retain .NET's 100 ns resolution instead of rounding seven decimal places to ms.
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.(\\d{1,7}))?(Z|[+-]\\d{2}:\\d{2})$"));
    auto match = pattern.match(text);
    if (match.hasMatch()) {
        auto dt = QDateTime::fromString(match.captured(1) + match.captured(3), Qt::ISODate);
        if (!dt.isValid()) return 0;
        auto fraction = match.captured(2).leftJustified(7, QLatin1Char('0'));
        return dt.toSecsSinceEpoch() * TicksPerSecond + EpochTicks + fraction.toLongLong();
    }
    auto dt = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(text, Qt::ISODate);
    return dt.isValid() ? dt.toMSecsSinceEpoch() * 10000 + EpochTicks : 0;
}
QString canonicalTime(QString at) {
    qint64 ticks = timeTicks(at);
    if (ticks <= 0) return QString();
    qint64 since = ticks - EpochTicks;
    qint64 seconds = since / TicksPerSecond;
    qint64 remainder = since % TicksPerSecond;
    if (remainder < 0) { remainder += TicksPerSecond; --seconds; }
    return QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"))
        + QLatin1Char('.') + QString::number(remainder).rightJustified(7, QLatin1Char('0')) + QLatin1Char('Z');
}
qint64 entryTime(const QJsonObject &entry) {
    for (const auto &key : {"SuccessfulQueryAtUtc", "QueriedAtUtc", "SavedAt"}) {
        qint64 value = timeTicks(entry.value(QLatin1String(key)).toString());
        if (value > 0) return value;
    }
    return 0;
}
QString entryPair(const QJsonObject &entry) {
    auto pair = entry.value(QStringLiteral("Pair")).toString();
    return pair.isEmpty() ? detectPair(entry.value(QStringLiteral("Word")).toString()) : pair;
}
QJsonObject mergeObjects(const QJsonObject &base, const QJsonObject &overrides) {
    auto result = base;
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (it.value().isObject() && result.value(it.key()).isObject())
            result[it.key()] = mergeObjects(result.value(it.key()).toObject(), it.value().toObject());
        else result[it.key()] = it.value();
    }
    return result;
}
QString deviceUsername() {
    auto machine = QSysInfo::machineUniqueId();
    if (machine.isEmpty()) machine = QSysInfo::machineHostName().toUtf8();
    QString account = qEnvironmentVariable("USER");
    if (account.isEmpty()) account = qEnvironmentVariable("USERNAME");
    auto hash = QCryptographicHash::hash(machine + '\n' + account.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("device-") + QString::fromLatin1(hash.left(24));
}
void validateSettings(QJsonObject &data) {
    auto user = data.value(QStringLiteral("username")).toString().trimmed();
    if (user.isEmpty()) { user = deviceUsername(); data[QStringLiteral("username")] = user; }
    const QRegularExpression validUser(QStringLiteral("^[a-z0-9_-]{1,64}$"));
    const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$"));
    if (!validUser.match(user).hasMatch() || reserved.match(user).hasMatch())
        fail(QStringLiteral("username 只能使用 1–64 位小写字母、数字、下划线和连字符，且不能为系统保留名称。"));
    auto delay = data.value(QStringLiteral("DelayMs"));
    if (!delay.isDouble() || delay.toInt() < 300 || delay.toInt() > 3000)
        fail(QStringLiteral("查询延迟 DelayMs 范围为 300–3000 毫秒。"));
    auto fontSize = data.value(QStringLiteral("FontSize"));
    if (!fontSize.isDouble() || fontSize.toDouble() < 12 || fontSize.toDouble() > 28)
        fail(QStringLiteral("旧版兼容字号 FontSize 范围为 12–28。"));
    if (data.value(QStringLiteral("FontName")).toString().trimmed().isEmpty())
        fail(QStringLiteral("FontName 不能为空。"));
    QString provider = data.value(QStringLiteral("Provider")).toString();
    if (provider != QStringLiteral("MyMemory") && provider != QStringLiteral("LibreTranslate"))
        fail(QStringLiteral("Provider 必须为 MyMemory 或 LibreTranslate。"));
    if (provider == QStringLiteral("LibreTranslate")) {
        QUrl url(data.value(QStringLiteral("LibreUrl")).toString());
        bool loopback = url.host() == QStringLiteral("localhost") || url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("::1");
        if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || !(url.scheme() == QStringLiteral("https") || (url.scheme() == QStringLiteral("http") && loopback)))
            fail(QStringLiteral("LibreUrl 需为 HTTPS 完整地址；仅本机服务可使用 HTTP。"));
    }
    if (!data.value(QStringLiteral("Styles")).isObject()) fail(QStringLiteral("Styles 必须是对象。"));
    if (!data.value(QStringLiteral("DeepSeek")).isObject() || !data.value(QStringLiteral("CacheSync")).isObject())
        fail(QStringLiteral("DeepSeek 与 CacheSync 必须是对象。"));
    auto ai = data.value(QStringLiteral("DeepSeek")).toObject();
    QUrl endpoint(ai.value(QStringLiteral("Endpoint")).toString());
    if (!endpoint.isValid() || endpoint.host().isEmpty() || endpoint.scheme() != QStringLiteral("https") || !endpoint.userInfo().isEmpty())
        fail(QStringLiteral("DeepSeek.Endpoint 必须为无用户名和密码的 HTTPS 地址。"));
    auto timeout = ai.value(QStringLiteral("TimeoutSeconds")).toInt();
    auto maxTokens = ai.value(QStringLiteral("MaxTokens")).toInt();
    if (timeout < 10 || timeout > 300 || maxTokens < 512 || maxTokens > 16384)
        fail(QStringLiteral("DeepSeek 超时应为 10–300 秒，MaxTokens 应为 512–16384。"));
    if (ai.value(QStringLiteral("Model")).toString().trimmed().isEmpty()) fail(QStringLiteral("DeepSeek.Model 不能为空。"));
    auto sync = data.value(QStringLiteral("CacheSync")).toObject();
    if (!QRegularExpression(QStringLiteral("^git@gitee\\.com:[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+\\.git$")).match(sync.value(QStringLiteral("Repository")).toString()).hasMatch())
        fail(QStringLiteral("CacheSync.Repository 必须为 Gitee SSH 仓库地址。"));
    QString device = sync.value(QStringLiteral("DeviceId")).toString();
    if (device.isEmpty()) {
        sync[QStringLiteral("DeviceId")] = QUuid::createUuid().toString(QUuid::Id128);
        data[QStringLiteral("CacheSync")] = sync;
    } else if (!QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match(device).hasMatch())
        fail(QStringLiteral("CacheSync.DeviceId 必须为 32 位小写十六进制。"));
    for (const auto &key : {"PrivateKeyPath", "KnownHostsPath"}) {
        auto value = sync.value(QLatin1String(key)).toString();
        if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('"')))
            fail(QStringLiteral("SSH 路径包含无效字符。"));
    }
}

class Connection {
public:
    QString name;
    QSqlDatabase db;
    explicit Connection(const QString &path, bool readOnly = false) : name(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(readOnly ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=10000") : QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
        if (!db.open()) {
            db = QSqlDatabase(); QSqlDatabase::removeDatabase(name);
            fail(QStringLiteral("无法打开 SQLite 数据库，请检查路径、权限和 Qt SQLite 驱动。"));
        }
    }
    ~Connection() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
};
QSqlQuery query(QSqlDatabase &db, const QString &sql, const QVariantList &parameters = {}) {
    QSqlQuery result(db);
    if (!result.prepare(sql)) fail(QStringLiteral("SQLite 语句准备失败：") + result.lastError().text());
    for (const auto &parameter : parameters) result.addBindValue(parameter);
    if (!result.exec()) fail(QStringLiteral("SQLite 操作失败：") + result.lastError().text());
    return result;
}
QVariant scalar(QSqlDatabase &db, const QString &sql, const QVariantList &parameters = {}) {
    auto q = query(db, sql, parameters);
    return q.next() ? q.value(0) : QVariant();
}
QString meta(QSqlDatabase &db, const QString &key) {
    return scalar(db, QStringLiteral("SELECT value FROM metadata WHERE key=?"), {key}).toString();
}
void setMeta(QSqlDatabase &db, const QString &key, const QString &value) {
    query(db, QStringLiteral("INSERT OR REPLACE INTO metadata(key,value) VALUES(?,?)"), {key, value});
}
void dirty(QSqlDatabase &db) {
    query(db, QStringLiteral("UPDATE metadata SET value=CAST(value AS INTEGER)+1 WHERE key='revision'"));
}
void verify(QSqlDatabase &db, const QString &user) {
    if (meta(db, QStringLiteral("payload_format")) != PayloadFormat)
        fail(QStringLiteral("此文件不是 Qt JSON 词库。旧 C# 二进制词库须先运行导出迁移工具，已保护原文件。"));
    if (meta(db, QStringLiteral("schema")) != QStringLiteral("1")) fail(QStringLiteral("不支持此 SQLite schema 版本。"));
    if (meta(db, QStringLiteral("username")) != user) fail(QStringLiteral("数据库 username 不匹配，已保护原文件。"));
}
class Transaction {
public:
    QSqlDatabase &db;
    bool committed = false;
    explicit Transaction(QSqlDatabase &connection) : db(connection) { query(db, QStringLiteral("BEGIN IMMEDIATE")); }
    ~Transaction() { if (!committed) { QSqlQuery rollback(db); rollback.exec(QStringLiteral("ROLLBACK")); } }
    void commit() { query(db, QStringLiteral("COMMIT")); committed = true; }
};
QByteArray encode(const QJsonObject &entry) {
    return PayloadPrefix + QJsonDocument(entry).toJson(QJsonDocument::Compact);
}
QJsonObject decode(const QByteArray &bytes) {
    if (!bytes.startsWith(PayloadPrefix)) fail(QStringLiteral("词条 payload 格式不受支持。"));
    if (bytes.size() > 32 * 1024 * 1024) fail(QStringLiteral("词条内容超过 32 MiB 安全限制。"));
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes.mid(PayloadPrefix.size()), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) fail(QStringLiteral("SQLite 词条 JSON 损坏。"));
    return doc.object();
}
qint64 wordId(QSqlDatabase &db, const QString &word, const QString &pair) {
    return scalar(db, QStringLiteral("SELECT id FROM words WHERE normalized=? AND pair=?"), {normalizeWord(word), pair}).toLongLong();
}
qint64 deletedAt(QSqlDatabase &db, const QString &word, const QString &pair) {
    return scalar(db, QStringLiteral("SELECT deleted_ticks FROM deleted_words WHERE normalized=? AND pair=?"), {normalizeWord(word), pair}).toLongLong();
}
bool favoriteEvent(QSqlDatabase &db, const QString &word, const QString &pair, bool favorite, QString at) {
    at = canonicalTime(at);
    if (at.isEmpty()) return false;
    auto normalized = normalizeWord(word);
    auto old = query(db, QStringLiteral("SELECT favorite,updated_at FROM favorite_events WHERE normalized=? AND pair=?"), {normalized, pair});
    bool exists = old.next();
    bool oldFavorite = exists && old.value(0).toBool();
    QString oldAt = exists ? old.value(1).toString() : QString();
    old.finish();
    bool changed = !exists || at > oldAt || (at == oldAt && !favorite && oldFavorite);
    if (changed) query(db, QStringLiteral("INSERT OR REPLACE INTO favorite_events(normalized,pair,favorite,updated_at) VALUES(?,?,?,?)"), {normalized, pair, favorite ? 1 : 0, at});
    else { at = oldAt; favorite = oldFavorite; }
    if (timeTicks(at) > deletedAt(db, normalized, pair)) {
        auto row = query(db, QStringLiteral("UPDATE words SET favorite=?,favorite_at=? WHERE normalized=? AND pair=? AND (favorite_at<? OR (favorite_at=? AND favorite=1 AND ?=0))"), {favorite ? 1 : 0, at, normalized, pair, at, at, favorite ? 1 : 0});
        changed |= row.numRowsAffected() > 0;
    }
    return changed;
}
qint64 ensureWord(QSqlDatabase &db, const QString &word, const QString &pair) {
    qint64 id = wordId(db, word, pair);
    if (id > 0) return id;
    id = std::max(meta(db, QStringLiteral("word_sequence")).toLongLong(), scalar(db, QStringLiteral("SELECT COALESCE(MAX(id),0) FROM words")).toLongLong()) + 1;
    query(db, QStringLiteral("INSERT INTO words(id,normalized,pair,word) VALUES(?,?,?,?)"), {id, normalizeWord(word), pair, word});
    setMeta(db, QStringLiteral("word_sequence"), QString::number(id));
    auto pending = query(db, QStringLiteral("SELECT favorite,updated_at FROM favorite_events WHERE normalized=? AND pair=?"), {normalizeWord(word), pair});
    bool found = pending.next(); bool favorite = found && pending.value(0).toBool(); QString at = found ? pending.value(1).toString() : QString(); pending.finish();
    if (found) favoriteEvent(db, word, pair, favorite, at);
    return id;
}
bool storeCore(QSqlDatabase &db, QJsonObject entry, qint64 intent = 0) {
    QString word = entry.value(QStringLiteral("Word")).toString().trimmed();
    if (word.isEmpty()) fail(QStringLiteral("词条 Word 不能为空。"));
    QString pair = entryPair(entry);
    if (pair != QStringLiteral("en|zh-CN") && pair != QStringLiteral("zh-CN|en")) fail(QStringLiteral("词条翻译方向无效。"));
    entry[QStringLiteral("Word")] = word;
    entry[QStringLiteral("Pair")] = pair;
    qint64 ticks = entryTime(entry);
    qint64 deletion = deletedAt(db, word, pair);
    if (deletion > 0 && std::max(ticks, intent) <= deletion) return false;
    QString key = sourceKey(entry);
    QByteArray bytes = encode(entry);
    if (bytes.size() > 32 * 1024 * 1024) fail(QStringLiteral("词条内容超过 32 MiB 限制。"));
    qint64 id = ensureWord(db, word, pair);
    auto old = query(db, QStringLiteral("SELECT ticks,payload FROM sources WHERE source_key=?"), {key});
    bool exists = old.next();
    bool changed = !exists || ticks > old.value(0).toLongLong() || (ticks == old.value(0).toLongLong() && bytes > old.value(1).toByteArray());
    old.finish();
    if (!changed) return false;
    QString provider = entry.value(QStringLiteral("TranslationProvider")).toString(QStringLiteral("MyMemory"));
    query(db, QStringLiteral("INSERT OR REPLACE INTO sources(source_key,word_id,provider,is_ai,summary,ticks,payload) VALUES(?,?,?,?,?,?,?)"), {key, id, provider, isDeepSeek(entry) ? 1 : 0, shortTranslation(entry), ticks, bytes});
    query(db, QStringLiteral("UPDATE words SET word=CASE WHEN last_ticks<=? THEN ? ELSE word END,last_ticks=MAX(last_ticks,?),summary=COALESCE((SELECT summary FROM sources WHERE word_id=? ORDER BY is_ai DESC,ticks DESC,source_key LIMIT 1),'') WHERE id=?"), {ticks, word, ticks, id, id});
    return true;
}
void visit(QSqlDatabase &db, const QString &key) {
    if (scalar(db, QStringLiteral("SELECT COUNT(*) FROM sources WHERE source_key=?"), {key}).toLongLong() == 0) return;
    qint64 cursor = meta(db, QStringLiteral("cursor")).toLongLong();
    if (scalar(db, QStringLiteral("SELECT source_key FROM history WHERE position=?"), {cursor}).toString() == key) return;
    query(db, QStringLiteral("DELETE FROM history WHERE position>?"), {cursor});
    query(db, QStringLiteral("INSERT INTO history(position,source_key) VALUES(?,?)"), {cursor + 1, key});
    setMeta(db, QStringLiteral("cursor"), QString::number(cursor + 1));
}
void removeContent(QSqlDatabase &db, qint64 id) {
    qint64 cursor = meta(db, QStringLiteral("cursor")).toLongLong();
    auto removed = query(db, QStringLiteral("DELETE FROM history WHERE source_key IN (SELECT source_key FROM sources WHERE word_id=?)"), {id});
    bool historyChanged = removed.numRowsAffected() > 0; removed.finish();
    query(db, QStringLiteral("DELETE FROM sources WHERE word_id=?"), {id});
    query(db, QStringLiteral("DELETE FROM words WHERE id=?"), {id});
    if (!historyChanged) return;
    qint64 previous = scalar(db, QStringLiteral("SELECT COUNT(*) FROM history WHERE position<=?"), {cursor}).toLongLong() - 1;
    query(db, QStringLiteral("CREATE TEMP TABLE compact_history(position INTEGER PRIMARY KEY AUTOINCREMENT,source_key TEXT NOT NULL)"));
    query(db, QStringLiteral("INSERT INTO compact_history(source_key) SELECT source_key FROM history ORDER BY position"));
    query(db, QStringLiteral("DELETE FROM history"));
    query(db, QStringLiteral("INSERT INTO history(position,source_key) SELECT position-1,source_key FROM compact_history ORDER BY position"));
    query(db, QStringLiteral("DROP TABLE compact_history"));
    qint64 last = scalar(db, QStringLiteral("SELECT COALESCE(MAX(position),-1) FROM history")).toLongLong();
    setMeta(db, QStringLiteral("cursor"), QString::number(last < 0 ? -1 : std::max(Q_INT64_C(0), std::min(previous, last))));
}
bool deleteCore(QSqlDatabase &db, const QString &word, const QString &pair, qint64 ticks) {
    auto normalized = normalizeWord(word);
    if (deletedAt(db, normalized, pair) >= ticks) return false;
    query(db, QStringLiteral("INSERT OR REPLACE INTO deleted_words(normalized,pair,deleted_ticks) VALUES(?,?,?)"), {normalized, pair, ticks});
    auto row = query(db, QStringLiteral("SELECT id,last_ticks,favorite_at FROM words WHERE normalized=? AND pair=?"), {normalized, pair});
    qint64 id = 0;
    if (row.next() && std::max(row.value(1).toLongLong(), timeTicks(row.value(2).toString())) <= ticks) id = row.value(0).toLongLong();
    row.finish();
    if (id > 0) removeContent(db, id);
    return true;
}
QJsonObject selectEntry(QSqlDatabase &db, const QString &where, const QVariantList &parameters) {
    auto row = query(db, QStringLiteral("SELECT s.payload FROM words w JOIN sources s ON s.word_id=w.id WHERE ") + where + QStringLiteral(" ORDER BY s.ticks DESC,s.is_ai DESC,s.source_key LIMIT 1"), parameters);
    return row.next() ? decode(row.value(0).toByteArray()) : QJsonObject();
}
QString likeWhere() {
    return QStringLiteral(" WHERE (?=0 OR w.favorite=1) AND (?='' OR instr(lower(w.word),?)>0 OR instr(lower(w.summary),?)>0)");
}
QVariantList filterValues(QString filter, bool favoritesOnly) {
    filter = filter.trimmed().toLower();
    return {favoritesOnly ? 1 : 0, nonNull(filter), nonNull(filter), nonNull(filter)};
}
QString csvValue(QString value) {
    if (!value.isEmpty() && QStringLiteral("=+-@\t\r\n").contains(value.at(0))) value.prepend(QLatin1Char('\''));
    return QLatin1Char('"') + value.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
}
} // namespace

QString detectPair(QString text) {
    for (auto ch : text) if ((ch.unicode() >= 0x3400 && ch.unicode() <= 0x9fff) || (ch.unicode() >= 0xf900 && ch.unicode() <= 0xfaff)) return QStringLiteral("zh-CN|en");
    return QStringLiteral("en|zh-CN");
}
QString normalizeWord(QString text) { return text.trimmed().normalized(QString::NormalizationForm_C).toLower(); }
bool isDeepSeek(const QJsonObject &entry) {
    return entry.value(QStringLiteral("LookupSource")).toString().compare(QStringLiteral("DeepSeek"), Qt::CaseInsensitive) == 0
        || entry.value(QStringLiteral("TranslationProvider")).toString().compare(QStringLiteral("DeepSeek"), Qt::CaseInsensitive) == 0;
}
QString sourceKey(QJsonObject entry) {
    QString identity = entryPair(entry) + QLatin1Char('|') + normalizeWord(entry.value(QStringLiteral("Word")).toString());
    if (isDeepSeek(entry)) return QStringLiteral("DeepSeek|") + entry.value(QStringLiteral("QueryEndpoint")).toString() + QLatin1Char('|') + entry.value(QStringLiteral("LookupModel")).toString() + QLatin1Char('|') + identity;
    QString provider = entry.value(QStringLiteral("TranslationProvider")).toString(QStringLiteral("MyMemory"));
    return provider + QLatin1Char('|') + (provider == QStringLiteral("LibreTranslate") ? entry.value(QStringLiteral("TranslationUrl")).toString() : QString()) + QLatin1Char('|') + identity;
}
QString shortTranslation(QJsonObject entry) {
    QString text = entry.value(QStringLiteral("Summary")).toString().trimmed();
    if (text.isEmpty()) text = entry.value(QStringLiteral("Translation")).toString().trimmed();
    text.replace(QLatin1Char('\r'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.size() > 80 ? text.left(79) + QChar(0x2026) : nonNull(text);
}
qint64 utcTicks() { return QDateTime::currentMSecsSinceEpoch() * 10000 + EpochTicks; }

QJsonObject defaultSettings() {
    QJsonObject ai{{QStringLiteral("Enabled"), false}, {QStringLiteral("ApiKey"), QStringLiteral("")}, {QStringLiteral("Endpoint"), QStringLiteral("https://api.deepseek.com/chat/completions")}, {QStringLiteral("Model"), QStringLiteral("deepseek-flash")}, {QStringLiteral("TimeoutSeconds"), 90}, {QStringLiteral("MaxTokens"), 8192}};
    QJsonObject sync{{QStringLiteral("Enabled"), true}, {QStringLiteral("Repository"), QStringLiteral("git@gitee.com:hippo-yf/MyDict-Cache.git")}, {QStringLiteral("PrivateKeyPath"), QStringLiteral("")}, {QStringLiteral("KnownHostsPath"), QStringLiteral("")}, {QStringLiteral("DeviceId"), QStringLiteral("")}};
    QJsonObject result{{QStringLiteral("username"), QStringLiteral("")}, {QStringLiteral("DeepSeek"), ai}, {QStringLiteral("CacheSync"), sync}, {QStringLiteral("FontName"), QStringLiteral("Microsoft YaHei UI")}, {QStringLiteral("FontSize"), 14}, {QStringLiteral("DefinitionFontName"), QStringLiteral("Georgia")}, {QStringLiteral("DelayMs"), 650}, {QStringLiteral("Provider"), QStringLiteral("MyMemory")}, {QStringLiteral("LibreUrl"), QStringLiteral("https://libretranslate.com/translate")}, {QStringLiteral("ApiKey"), QStringLiteral("")}, {QStringLiteral("ProtectedApiKey"), QJsonValue::Null}, {QStringLiteral("ShowExamples"), true}, {QStringLiteral("ShowDerived"), true}, {QStringLiteral("Window"), QJsonValue::Null}, {QStringLiteral("Styles"), QJsonObject()}, {QStringLiteral("_配置版本"), 3}};
    QFile resource(QStringLiteral(":/settings.example.json"));
    if (resource.open(QIODevice::ReadOnly)) {
        auto object = QJsonDocument::fromJson(resource.readAll()).object();
        if (!object.isEmpty()) result = mergeObjects(result, object);
    }
    return result;
}
Settings::Settings(QString dir) : directory(QDir(dir).absolutePath()), path(QDir(directory).filePath(QStringLiteral("settings.json"))), data(defaultSettings()) {}
void Settings::load() {
    QFile file(path);
    if (!file.exists()) { data = defaultSettings(); validateSettings(data); save(); return; }
    if (!file.open(QIODevice::ReadOnly)) fail(QStringLiteral("无法读取 settings.json。"));
    if (file.size() > 4 * 1024 * 1024) fail(QStringLiteral("settings.json 超过 4 MiB 限制。"));
    QByteArray bytes = file.readAll();
    // On Windows an open reader can prevent QSaveFile from replacing the file.
    // Identity generation may save immediately, so release it before validation.
    file.close();
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) fail(QStringLiteral("settings.json 不是有效 JSON：") + error.errorString());
    auto candidate = mergeObjects(defaultSettings(), doc.object());
    validateSettings(candidate);
    bool identityChanged = doc.object().value(QStringLiteral("username")).toString().isEmpty()
        || doc.object().value(QStringLiteral("CacheSync")).toObject().value(QStringLiteral("DeviceId")).toString().isEmpty();
    data = candidate; loadedBytes = bytes;
    if (identityChanged) save();
}
void Settings::save() {
    auto candidate = data; validateSettings(candidate);
    if (!QDir().mkpath(directory)) fail(QStringLiteral("无法创建设置目录。"));
    QByteArray bytes = QJsonDocument(candidate).toJson(QJsonDocument::Indented);
    QFile original(path);
    if (original.exists()) {
        if (!original.open(QIODevice::ReadOnly)) fail(QStringLiteral("无法备份原设置。"));
        QSaveFile backup(path + QStringLiteral(".bak"));
        if (!backup.open(QIODevice::WriteOnly)) fail(QStringLiteral("无法创建 settings.json.bak。"));
        auto old = original.readAll();
        if (backup.write(old) != old.size() || !backup.commit()) fail(QStringLiteral("保存设置备份失败。"));
        original.close();
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) fail(QStringLiteral("保存 settings.json 失败，原文件已保留。"));
    data = candidate; loadedBytes = bytes;
}
bool Settings::reload() {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    if (file.size() > 4 * 1024 * 1024) fail(QStringLiteral("settings.json 超过 4 MiB 限制。"));
    auto bytes = file.readAll(); file.close();
    if (bytes == loadedBytes) return false;
    load(); return true;
}
QString Settings::username() const { return data.value(QStringLiteral("username")).toString(); }
QString Settings::databasePath() const { return QDir(directory).filePath(QStringLiteral("profiles/") + username() + QStringLiteral("/words-qt.sqlite")); }

Database::Database(QString path, QString username) : filePath(QFileInfo(path).absoluteFilePath()), user(username) {}
QString Database::path() const { return filePath; }
void Database::initialize() {
    if (!QRegularExpression(QStringLiteral("^[a-z0-9_-]{1,64}$")).match(user).hasMatch()) fail(QStringLiteral("数据库 username 无效。"));
    if (QFileInfo::exists(filePath) && QFileInfo(filePath).size() > 0) {
        Connection check(filePath, true);
        verify(check.db, user); // Verify BEFORE any pragma/schema writes on an existing file.
    }
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) fail(QStringLiteral("无法创建数据库目录。"));
    Connection c(filePath);
    query(c.db, QStringLiteral("PRAGMA journal_mode=WAL"));
    query(c.db, QStringLiteral("PRAGMA synchronous=NORMAL"));
    Transaction tx(c.db);
    const QStringList schema{
        QStringLiteral("CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS words(id INTEGER PRIMARY KEY,normalized TEXT NOT NULL,pair TEXT NOT NULL,word TEXT NOT NULL,favorite INTEGER NOT NULL DEFAULT 0,favorite_at TEXT NOT NULL DEFAULT '',last_ticks INTEGER NOT NULL DEFAULT 0,summary TEXT NOT NULL DEFAULT '',UNIQUE(normalized,pair))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS sources(source_key TEXT PRIMARY KEY,word_id INTEGER NOT NULL REFERENCES words(id),provider TEXT NOT NULL,is_ai INTEGER NOT NULL,summary TEXT NOT NULL,ticks INTEGER NOT NULL,payload BLOB NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS favorite_events(normalized TEXT NOT NULL,pair TEXT NOT NULL,favorite INTEGER NOT NULL,updated_at TEXT NOT NULL,PRIMARY KEY(normalized,pair))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS deleted_words(normalized TEXT NOT NULL,pair TEXT NOT NULL,deleted_ticks INTEGER NOT NULL,PRIMARY KEY(normalized,pair))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS history(position INTEGER PRIMARY KEY,source_key TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS import_errors(id INTEGER PRIMARY KEY,word TEXT NOT NULL,error TEXT NOT NULL,at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS source_word ON sources(word_id,ticks DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS word_recent ON words(last_ticks DESC,id DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS word_favorite ON words(favorite,last_ticks DESC,id DESC)")};
    for (const auto &sql : schema) query(c.db, sql);
    setMeta(c.db, QStringLiteral("username"), user); setMeta(c.db, QStringLiteral("schema"), QStringLiteral("1"));
    setMeta(c.db, QStringLiteral("payload_format"), PayloadFormat);
    query(c.db, QStringLiteral("INSERT OR IGNORE INTO metadata(key,value) VALUES('revision','0'),('synced','0'),('cursor','-1'),('word_sequence','0')"));
    qint64 highest = scalar(c.db, QStringLiteral("SELECT COALESCE(MAX(id),0) FROM words")).toLongLong();
    if (meta(c.db, QStringLiteral("word_sequence")).toLongLong() < highest) setMeta(c.db, QStringLiteral("word_sequence"), QString::number(highest));
    tx.commit();
}
QJsonObject Database::find(QString word, QString pair) const {
    Connection c(filePath, true); verify(c.db, user);
    return selectEntry(c.db, QStringLiteral("w.normalized=? AND w.pair=?"), {normalizeWord(word), pair});
}
QJsonObject Database::findDeepSeek(QString word, QString pair) const {
    Connection c(filePath, true); verify(c.db, user);
    return selectEntry(c.db, QStringLiteral("w.normalized=? AND w.pair=? AND s.is_ai=1"), {normalizeWord(word), pair});
}
QJsonObject Database::get(qint64 id) const {
    Connection c(filePath, true); verify(c.db, user); return selectEntry(c.db, QStringLiteral("w.id=?"), {id});
}
bool Database::hasDeepSeek(QString word, QString pair) const {
    Connection c(filePath, true); verify(c.db, user);
    return scalar(c.db, QStringLiteral("SELECT EXISTS(SELECT 1 FROM words w JOIN sources s ON s.word_id=w.id WHERE w.normalized=? AND w.pair=? AND s.is_ai=1)"), {normalizeWord(word), pair}).toBool();
}
bool Database::isFavorite(QString word, QString pair) const {
    Connection c(filePath, true); verify(c.db, user);
    return scalar(c.db, QStringLiteral("SELECT favorite FROM words WHERE normalized=? AND pair=?"), {normalizeWord(word), pair}).toBool();
}
void Database::store(QJsonObject entry, bool addHistory) {
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    if (storeCore(c.db, entry)) dirty(c.db);
    if (addHistory) visit(c.db, sourceKey(entry));
    tx.commit();
}
void Database::setFavorite(QString word, QString pair, bool favorite) {
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    auto row = query(c.db, QStringLiteral("SELECT favorite,favorite_at FROM words WHERE normalized=? AND pair=?"), {normalizeWord(word), pair});
    if (!row.next() || row.value(0).toBool() == favorite) { row.finish(); tx.commit(); return; }
    qint64 latest = timeTicks(row.value(1).toString()); row.finish();
    auto at = isoNow();
    if (timeTicks(at) <= latest) {
        // Clock skew must not make a deliberate local action older than the event it replaces.
        auto current = latest + 1;
        auto dt = QDateTime::fromMSecsSinceEpoch((current - EpochTicks) / 10000, Qt::UTC);
        at = dt.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss")) + QLatin1Char('.') + QString::number((current - EpochTicks) % TicksPerSecond).rightJustified(7, QLatin1Char('0')) + QLatin1Char('Z');
    }
    if (favoriteEvent(c.db, word, pair, favorite, at)) dirty(c.db);
    tx.commit();
}
void Database::saveFavorite(QJsonObject entry) {
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    QString word = entry.value(QStringLiteral("Word")).toString(), pair = entryPair(entry);
    // Importing/re-favoriting is an explicit user intent. It may restore an older cached
    // source after a deletion, while a background merge of the same source may not.
    qint64 prior = timeTicks(scalar(c.db, QStringLiteral("SELECT updated_at FROM favorite_events WHERE normalized=? AND pair=?"), {normalizeWord(word), pair}).toString());
    qint64 intent = std::max(utcTicks(), std::max(deletedAt(c.db, word, pair), prior) + 1);
    qint64 since = intent - EpochTicks;
    QString at = QDateTime::fromSecsSinceEpoch(since / TicksPerSecond, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"))
        + QLatin1Char('.') + QString::number(since % TicksPerSecond).rightJustified(7, QLatin1Char('0')) + QLatin1Char('Z');
    bool changed = storeCore(c.db, entry, intent);
    changed |= favoriteEvent(c.db, word, pair, true, at);
    if (changed) dirty(c.db);
    tx.commit();
}
void Database::recordImportError(QString word, QString error) {
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    query(c.db, QStringLiteral("INSERT INTO import_errors(word,error,at) VALUES(?,?,?)"), {nonNull(word), nonNull(error), isoNow()});
    tx.commit();
}
bool Database::remove(qint64 id) {
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    auto row = query(c.db, QStringLiteral("SELECT normalized,pair,favorite,last_ticks,favorite_at FROM words WHERE id=?"), {id});
    if (!row.next() || row.value(2).toBool()) { row.finish(); tx.commit(); return false; }
    QString word = row.value(0).toString(), pair = row.value(1).toString();
    qint64 latest = std::max(row.value(3).toLongLong(), timeTicks(row.value(4).toString())); row.finish();
    qint64 ticks = std::max(utcTicks(), std::max(latest, deletedAt(c.db, word, pair))) + 1;
    deleteCore(c.db, word, pair, ticks); dirty(c.db); tx.commit(); return true;
}
BookPage Database::page(QString filter, bool favoritesOnly, int offset, int limit) const {
    limit = std::max(1, std::min(100, limit)); offset = std::max(0, offset);
    Connection c(filePath, true); verify(c.db, user);
    auto parameters = filterValues(filter, favoritesOnly); BookPage result;
    result.total = scalar(c.db, QStringLiteral("SELECT COUNT(*) FROM words w") + likeWhere(), parameters).toInt();
    parameters << limit << offset;
    auto rows = query(c.db, QStringLiteral("SELECT w.id,w.word,w.pair,w.summary,w.favorite,EXISTS(SELECT 1 FROM sources s WHERE s.word_id=w.id AND s.is_ai=1),EXISTS(SELECT 1 FROM sources s WHERE s.word_id=w.id AND s.is_ai=0) FROM words w") + likeWhere() + QStringLiteral(" ORDER BY w.last_ticks DESC,w.id DESC LIMIT ? OFFSET ?"), parameters);
    while (rows.next()) {
        BookRow row; row.id = rows.value(0).toLongLong(); row.word = rows.value(1).toString(); row.pair = rows.value(2).toString(); row.summary = rows.value(3).toString();
        row.favorite = rows.value(4).toBool(); row.hasAi = rows.value(5).toBool(); row.hasOnline = rows.value(6).toBool(); result.rows.append(row);
    }
    return result;
}
QJsonObject Database::navigate(int direction) {
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    qint64 cursor = meta(c.db, QStringLiteral("cursor")).toLongLong();
    qint64 last = scalar(c.db, QStringLiteral("SELECT COALESCE(MAX(position),-1) FROM history")).toLongLong();
    qint64 next = cursor + (direction > 0 ? 1 : direction < 0 ? -1 : 0);
    if (next >= 0 && next <= last) { cursor = next; setMeta(c.db, QStringLiteral("cursor"), QString::number(cursor)); }
    auto selected = query(c.db, QStringLiteral("SELECT w.id FROM history h JOIN sources s ON s.source_key=h.source_key JOIN words w ON w.id=s.word_id WHERE h.position=?"), {cursor});
    qint64 id = selected.next() ? selected.value(0).toLongLong() : 0; selected.finish();
    QJsonObject entry = id > 0 ? selectEntry(c.db, QStringLiteral("w.id=?"), {id}) : QJsonObject(); tx.commit(); return entry;
}
bool Database::canNavigate(int direction) const {
    Connection c(filePath, true); verify(c.db, user);
    qint64 cursor = meta(c.db, QStringLiteral("cursor")).toLongLong();
    qint64 last = scalar(c.db, QStringLiteral("SELECT COALESCE(MAX(position),-1) FROM history")).toLongLong();
    return direction < 0 ? cursor > 0 : direction > 0 ? cursor < last : cursor >= 0 && cursor <= last;
}
void Database::exportCsv(QString path, QString filter, bool favoritesOnly) const {
    Connection c(filePath, true); verify(c.db, user);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) fail(QStringLiteral("无法创建 CSV 文件。"));
    auto write = [&](const QByteArray &bytes) { if (output.write(bytes) != bytes.size()) fail(QStringLiteral("写入 CSV 失败。")); };
    write(QByteArray::fromHex("efbbbf")); write(QStringLiteral("单词,翻译,方向,收藏时间,词典详解\r\n").toUtf8());
    auto rows = query(c.db, QStringLiteral("SELECT w.id,w.favorite_at FROM words w") + likeWhere() + QStringLiteral(" ORDER BY w.last_ticks DESC,w.id DESC"), filterValues(filter, favoritesOnly));
    while (rows.next()) {
        auto entry = selectEntry(c.db, QStringLiteral("w.id=?"), {rows.value(0)});
        QStringList values{entry.value(QStringLiteral("Word")).toString(), shortTranslation(entry), entryPair(entry), rows.value(1).toString(), entry.value(QStringLiteral("Details")).toString()};
        for (auto &value : values) value = csvValue(value);
        write((values.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8());
    }
    if (!output.commit()) fail(QStringLiteral("保存 CSV 失败。"));
}
void Database::importEntries(QString jsonPath) {
    QFile input(jsonPath);
    if (!input.open(QIODevice::ReadOnly)) fail(QStringLiteral("无法读取词库迁移文件。"));
    auto readLine = [&]() {
        QByteArray line = input.readLine(32 * 1024 * 1024 + 1);
        if (line.size() > 32 * 1024 * 1024) fail(QStringLiteral("迁移文件单行超过 32 MiB。"));
        QJsonParseError error; auto doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) fail(QStringLiteral("迁移文件包含无效 JSON 行。"));
        return doc.object();
    };
    auto header = readLine();
    if (header.value(QStringLiteral("format")).toString() != QStringLiteral("mydict-portable-v1")) fail(QStringLiteral("迁移文件格式不是 mydict-portable-v1。"));
    if (header.value(QStringLiteral("username")).toString() != user) fail(QStringLiteral("迁移文件 username 不匹配。请先在 settings.json 中指定原词库 username。"));
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    bool changed = false, historyStarted = false;
    int requestedCursor = -1; qint64 maxHistory = -1;
    while (!input.atEnd()) {
        auto item = readLine(); QString type = item.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("entry")) {
            auto entry = item.value(QStringLiteral("entry")).toObject();
            QString at = item.value(QStringLiteral("favoriteAt")).toString();
            bool favorite = item.value(QStringLiteral("favorite")).toBool();
            changed |= storeCore(c.db, entry, favorite ? timeTicks(at) : 0);
            if (!at.isEmpty()) changed |= favoriteEvent(c.db, entry.value(QStringLiteral("Word")).toString(), entryPair(entry), favorite, at);
        } else if (type == QStringLiteral("favorite")) {
            changed |= favoriteEvent(c.db, item.value(QStringLiteral("word")).toString(), item.value(QStringLiteral("pair")).toString(), item.value(QStringLiteral("favorite")).toBool(), item.value(QStringLiteral("updatedAt")).toString());
        } else if (type == QStringLiteral("deleted")) {
            auto value = item.value(QStringLiteral("ticks"));
            if (!value.isString()) fail(QStringLiteral("迁移 ticks 必须为字符串，避免丢失 64 位精度。"));
            bool ok = false; qint64 ticks = value.toString().toLongLong(&ok);
            if (!ok || ticks < 0) fail(QStringLiteral("迁移 ticks 无效。"));
            changed |= deleteCore(c.db, item.value(QStringLiteral("word")).toString(), item.value(QStringLiteral("pair")).toString(), ticks);
        } else if (type == QStringLiteral("history")) {
            if (!historyStarted) { query(c.db, QStringLiteral("DELETE FROM history")); historyStarted = true; }
            auto key = item.value(QStringLiteral("sourceKey")).toString();
            int position = item.value(QStringLiteral("position")).toInt(-1);
            if (position < 0) fail(QStringLiteral("迁移历史位置无效。"));
            if (scalar(c.db, QStringLiteral("SELECT COUNT(*) FROM sources WHERE source_key=?"), {key}).toInt() > 0) {
                query(c.db, QStringLiteral("INSERT OR REPLACE INTO history(position,source_key) VALUES(?,?)"), {position, key});
                maxHistory = std::max(maxHistory, qint64(position));
            }
        } else if (type == QStringLiteral("cursor")) requestedCursor = item.value(QStringLiteral("position")).toInt(-1);
        else if (type == QStringLiteral("importError")) {
            QString word = nonNull(item.value(QStringLiteral("word")).toString());
            QString error = nonNull(item.value(QStringLiteral("error")).toString());
            QString at = nonNull(item.value(QStringLiteral("at")).toString());
            query(c.db, QStringLiteral("INSERT INTO import_errors(word,error,at) SELECT ?,?,? WHERE NOT EXISTS(SELECT 1 FROM import_errors WHERE word=? AND error=? AND at=?)"), {word, error, at, word, error, at});
        }
        else fail(QStringLiteral("迁移文件包含未知记录类型。"));
    }
    if (historyStarted) {
        qint64 old = std::max(Q_INT64_C(-1), std::min(qint64(requestedCursor), maxHistory));
        qint64 surviving = scalar(c.db, QStringLiteral("SELECT COUNT(*) FROM history WHERE position<=?"), {old}).toLongLong() - 1;
        query(c.db, QStringLiteral("CREATE TEMP TABLE import_history(position INTEGER PRIMARY KEY AUTOINCREMENT,source_key TEXT NOT NULL)"));
        query(c.db, QStringLiteral("INSERT INTO import_history(source_key) SELECT source_key FROM history ORDER BY position"));
        query(c.db, QStringLiteral("DELETE FROM history"));
        query(c.db, QStringLiteral("INSERT INTO history(position,source_key) SELECT position-1,source_key FROM import_history ORDER BY position"));
        query(c.db, QStringLiteral("DROP TABLE import_history"));
        setMeta(c.db, QStringLiteral("cursor"), QString::number(surviving));
    }
    if (changed) dirty(c.db);
    tx.commit();
}
void Database::mergeFrom(QString sqlitePath) {
    QString sourcePath = QFileInfo(sqlitePath).absoluteFilePath();
    if (sourcePath == filePath) return;
    Connection remote(sourcePath, true); verify(remote.db, user);
    query(remote.db, QStringLiteral("BEGIN")); // Pin one coherent snapshot through the paged read.
    QString fingerprint = QString::number(QFileInfo(sourcePath).size()) + QLatin1Char(':') + QString::number(QFileInfo(sourcePath).lastModified().toMSecsSinceEpoch()) + QLatin1Char(':') + meta(remote.db, QStringLiteral("revision"));
    QString key = QStringLiteral("remote:") + sourcePath;
    { Connection local(filePath, true); verify(local.db, user); if (meta(local.db, key) == fingerprint) return; }
    struct Change { QString word, pair, at; qint64 ticks = 0; bool favorite = false; };
    auto mergeChanges = [&](const QString &sql, bool deletion) {
        auto rows = query(remote.db, sql); QList<Change> batch;
        auto flush = [&]() {
            if (batch.isEmpty()) return;
            Connection local(filePath); verify(local.db, user); Transaction tx(local.db); bool changed = false;
            for (const auto &item : batch) changed |= deletion ? deleteCore(local.db, item.word, item.pair, item.ticks) : favoriteEvent(local.db, item.word, item.pair, item.favorite, item.at);
            if (changed) dirty(local.db); tx.commit(); batch.clear();
        };
        while (rows.next()) {
            Change item; item.word = rows.value(0).toString(); item.pair = rows.value(1).toString();
            if (deletion) item.ticks = rows.value(2).toLongLong(); else { item.favorite = rows.value(2).toBool(); item.at = rows.value(3).toString(); }
            batch.append(item); if (batch.size() == 50) flush();
        }
        flush();
    };
    mergeChanges(QStringLiteral("SELECT normalized,pair,deleted_ticks FROM deleted_words"), true);
    mergeChanges(QStringLiteral("SELECT normalized,pair,favorite,updated_at FROM favorite_events"), false);
    {
        auto rows = query(remote.db, QStringLiteral("SELECT s.payload,w.favorite,w.favorite_at FROM sources s JOIN words w ON w.id=s.word_id"));
        struct Item { QJsonObject entry; qint64 intent; }; QList<Item> batch;
        auto flush = [&]() {
            if (batch.isEmpty()) return;
            Connection local(filePath); verify(local.db, user); Transaction tx(local.db); bool changed = false;
            for (const auto &item : batch) changed |= storeCore(local.db, item.entry, item.intent);
            if (changed) dirty(local.db); tx.commit(); batch.clear();
        };
        while (rows.next()) {
            batch.append({decode(rows.value(0).toByteArray()), rows.value(1).toBool() ? timeTicks(rows.value(2).toString()) : 0});
            if (batch.size() == 50) flush();
        }
        flush();
    }
    mergeChanges(QStringLiteral("SELECT word,pair,favorite,favorite_at FROM words WHERE favorite_at<>''"), false);
    Connection local(filePath); verify(local.db, user); Transaction tx(local.db); setMeta(local.db, key, fingerprint); tx.commit();
}
void Database::backupTo(QString destination) const {
    QString absolute = QFileInfo(destination).absoluteFilePath();
    if (absolute == filePath) fail(QStringLiteral("备份目标不能为当前词库文件。"));
    if (!QDir().mkpath(QFileInfo(absolute).absolutePath())) fail(QStringLiteral("无法创建备份目录。"));
    // VACUUM INTO is a transactionally consistent SQLite snapshot, excluding WAL sidecars.
    // Use a sibling temporary file, then atomically replace the caller's destination.
    QString temporary = absolute + QLatin1Char('.') + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".tmp");
    try {
        { Connection c(filePath); verify(c.db, user); query(c.db, QStringLiteral("VACUUM INTO ?"), {temporary}); }
        QFile input(temporary); if (!input.open(QIODevice::ReadOnly)) fail(QStringLiteral("读取数据库快照失败。"));
        QSaveFile output(absolute); if (!output.open(QIODevice::WriteOnly)) fail(QStringLiteral("无法写入数据库快照。"));
        while (!input.atEnd()) { auto bytes = input.read(1024 * 1024); if (bytes.isEmpty() && input.error() != QFile::NoError) fail(QStringLiteral("读取数据库快照失败。")); if (output.write(bytes) != bytes.size()) fail(QStringLiteral("写入数据库快照失败。")); }
        if (!output.commit()) fail(QStringLiteral("保存数据库快照失败。"));
        input.close(); QFile::remove(temporary);
    } catch (...) { QFile::remove(temporary); throw; }
}
qint64 Database::revision() const { Connection c(filePath, true); verify(c.db, user); return meta(c.db, QStringLiteral("revision")).toLongLong(); }
qint64 Database::syncedRevision() const { Connection c(filePath, true); verify(c.db, user); return meta(c.db, QStringLiteral("synced")).toLongLong(); }
void Database::markSynced(qint64 revision) {
    Connection c(filePath); verify(c.db, user); Transaction tx(c.db);
    auto current = meta(c.db, QStringLiteral("revision")).toLongLong(); auto previous = meta(c.db, QStringLiteral("synced")).toLongLong();
    setMeta(c.db, QStringLiteral("synced"), QString::number(std::max(previous, std::min(current, revision)))); tx.commit();
}
} // namespace MyDict
