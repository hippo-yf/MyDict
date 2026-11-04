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
struct HistorySelection { QString word, pair; QJsonObject entry; };

// Each operation opens an independent SQLite connection in its caller's thread.
// This value type may safely be captured by background jobs.
class Database {
public:
    explicit Database(QString path, QString username);
    QString path() const;
    void initialize();
    QJsonObject find(QString word, QString pair) const;
    // A missing selected source never falls back to the other source group.
    QJsonObject findForSource(QString word, QString pair, bool deepSeek) const;
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
    // Keep the history word even when its selected source has not been cached.
    HistorySelection navigateForSource(int direction, bool deepSeek);
    bool canNavigate(int direction) const;
    void exportCsv(QString path, QString filter, bool favoritesOnly) const;
    // Streamed NDJSON mydict-portable-v1, emitted by Export-Legacy.cs.
    void importEntries(QString jsonPath);
    // Shared mydict-json-v1 and old qt-json-v1 snapshots are read-only inputs.
    // C# binary XML must first be converted by an updated C# client.
    void mergeFrom(QString sqlitePath);
    static bool isJsonSnapshot(QString sqlitePath);
    void backupTo(QString path) const;
    qint64 revision() const;
    qint64 syncedRevision() const;
    void markSynced(qint64 revision);
private:
    QString filePath, user;
    void migrateLocalQt();
};
} // namespace MyDict
