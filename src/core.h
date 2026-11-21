#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <QByteArray>
#include <QStringList>

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
struct ReviewQuestion {
    qint64 wordId = 0;
    QString word, pair, token, sentence, sentenceChinese;
    QStringList options, meanings;
    QJsonArray sentenceSegments;
    int correctIndex = -1;
};

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
    bool remove(qint64 id); // Compatibility API: always false; cached words cannot be deleted.
    BookPage page(QString filter, bool favoritesOnly, int offset, int limit = 50) const;
    ReviewQuestion reviewQuestion(bool sentenceMode = false) const;
    // selected == -1 records "not familiar" as an incorrect answer.
    bool answerReview(const ReviewQuestion &question, int selected);
    QJsonObject dailyArticle(QString date = {}) const;
    QJsonArray dailyHistory(int offset = 0, int limit = 20) const;
    QStringList recentDailyWords(int days = 90) const;
    bool beginDailyRequest(QString date, int limit, bool existingAllowed = false);
    void endDailyRequest(QString date);
    void saveDailyArticle(QJsonObject article);
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
