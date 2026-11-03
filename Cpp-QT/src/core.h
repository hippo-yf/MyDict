#pragma once
#include <QJsonObject>
#include <QString>
#include <QList>
#include <QByteArray>

namespace MyDict {
QString detectPair(QString text);
QString normalizeWord(QString text);
bool isDeepSeek(const QJsonObject &entry);
QString sourceKey(QJsonObject entry);
QString shortTranslation(QJsonObject entry);
qint64 utcTicks();
QJsonObject defaultSettings();

class Settings {
public:
    explicit Settings(QString directory);
    QString directory, path;
    QJsonObject data;
    void load();
    void save();
    bool reload(); // Invalid edits throw and leave the last valid settings intact.
    QString username() const;
    QString databasePath() const;
private:
    QByteArray loadedBytes;
};

struct BookRow {
    qint64 id = 0;
    QString word, pair, summary;
    bool favorite = false, hasAi = false, hasOnline = false;
};
struct BookPage { QList<BookRow> rows; int total = 0; };

// Each operation opens an independent SQLite connection in its caller's thread.
// This value type may safely be captured by background jobs.
class Database {
public:
    explicit Database(QString path, QString username);
    QString path() const;
    void initialize();
    QJsonObject find(QString word, QString pair) const;
    QJsonObject findDeepSeek(QString word, QString pair) const;
    QJsonObject get(qint64 id) const;
    bool hasDeepSeek(QString word, QString pair) const;
    bool isFavorite(QString word, QString pair) const;
    void store(QJsonObject entry, bool addHistory = true);
    void setFavorite(QString word, QString pair, bool favorite);
    void saveFavorite(QJsonObject entry);
    void recordImportError(QString word, QString error);
    bool remove(qint64 id);
    BookPage page(QString filter, bool favoritesOnly, int offset, int limit = 50) const;
    QJsonObject navigate(int direction);
    bool canNavigate(int direction) const;
    void exportCsv(QString path, QString filter, bool favoritesOnly) const;
    // Streamed NDJSON mydict-portable-v1, emitted by Export-Legacy.cs.
    void importEntries(QString jsonPath);
    // Only qt-json-v1 databases are accepted. C# binary XML is never decoded here.
    void mergeFrom(QString sqlitePath);
    void backupTo(QString path) const;
    qint64 revision() const;
    qint64 syncedRevision() const;
    void markSynced(qint64 revision);
private:
    QString filePath, user;
};
} // namespace MyDict
