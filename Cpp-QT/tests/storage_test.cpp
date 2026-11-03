#include "core.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QtConcurrent>
#include <functional>
#include <stdexcept>

using namespace MyDict;

namespace {
QJsonObject entry(QString word, QString text, QString at, bool ai = false) {
    return {{"Word", word}, {"Pair", detectPair(word)}, {"Translation", text}, {"Summary", text},
            {"TranslationProvider", ai ? "DeepSeek" : "MyMemory"}, {"LookupSource", ai ? "DeepSeek" : "Online"},
            {"LookupModel", ai ? "test-model" : ""}, {"QueryEndpoint", ai ? "https://example.com/chat/completions" : ""},
            {"SuccessfulQueryAtUtc", at}, {"QueriedAtUtc", at},
            {"Record", QJsonObject{{"Word", word}, {"Meanings", QJsonArray()}}}};
}
QString ndjson(QTemporaryDir &temp, const QList<QJsonObject> &items) {
    QString path = temp.filePath(QUuid::createUuid().toString(QUuid::Id128) + ".ndjson");
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {};
    for (const auto &item : items) { file.write(QJsonDocument(item).toJson(QJsonDocument::Compact)); file.write("\n"); }
    return path;
}
QJsonObject header(QString user = "tester") { return {{"format", "mydict-portable-v1"}, {"username", user}}; }
void sql(QString path, const std::function<void(QSqlDatabase &)> &action) {
    QString name = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(path);
        if (db.open()) action(db);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}
QByteArray bytes(QString path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
}

class StorageTest : public QObject {
    Q_OBJECT
private slots:
    void normalizationAndSourceIdentity() {
        QCOMPARE(detectPair("civil"), QString("en|zh-CN"));
        QCOMPARE(detectPair(QString::fromUtf8("晚上")), QString("zh-CN|en"));
        QCOMPARE(normalizeWord(" E\u0301COLE "), QString::fromUtf8("école"));
        auto a = entry(" Test ", "test", "2026-01-01T00:00:00.0000000Z");
        QCOMPARE(sourceKey(a), QString("MyMemory||en|zh-CN|test"));
        auto ai = entry("test", "t", "2026-01-01T00:00:00Z", true);
        QVERIFY(sourceKey(ai).startsWith("DeepSeek|https://example.com/"));
        a["Summary"] = QString(90, 'x'); QCOMPARE(shortTranslation(a).size(), 80);
    }
    void settingsRoundTripAndInvalidReload() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QTemporaryDir templateDirectory;
        Settings firstRun(templateDirectory.path());
        QFile templateFile(firstRun.path); QVERIFY(templateFile.open(QIODevice::WriteOnly));
        templateFile.write(QJsonDocument(defaultSettings()).toJson()); templateFile.close();
        firstRun.load(); // Windows: close the reader before atomic identity persistence.
        QVERIFY(!firstRun.username().isEmpty());
        QVERIFY(!firstRun.data["CacheSync"].toObject()["DeviceId"].toString().isEmpty());
        Settings s(temp.path()); s.load();
        QVERIFY(s.username().startsWith("device-"));
        QVERIFY(s.databasePath().endsWith("/words-qt.sqlite"));
        QString user = s.username();
        s.data["_personal_note"] = "preserved";
        s.data["Window"] = QJsonObject{{"Width", 750}, {"Height", 650}};
        auto deepseek = s.data["DeepSeek"].toObject(); deepseek["ApiKey"] = "fake-fixture-key"; s.data["DeepSeek"] = deepseek;
        s.save(); QVERIFY(!s.reload());
        Settings again(temp.path()); again.load();
        QCOMPARE(again.username(), user);
        QCOMPARE(again.data["_personal_note"].toString(), QString("preserved"));
        QCOMPARE(again.data["DeepSeek"].toObject()["ApiKey"].toString(), QString("fake-fixture-key"));
        QFile file(s.path); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("{invalid"); file.close();
        QVERIFY_EXCEPTION_THROWN(s.reload(), std::runtime_error);
        QCOMPARE(s.username(), user); QCOMPARE(s.data["_personal_note"].toString(), QString("preserved"));
        QVERIFY(QFileInfo::exists(s.path + ".bak"));
    }
    void latestBodyAiSummaryAndMetadataPagination() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        db.store(entry("civil", "AI summary", "2026-01-01T00:00:00.0000001Z", true));
        db.store(entry("civil", "online newer", "2026-01-01T00:00:00.0000002Z"));
        QCOMPARE(db.find("CIVIL", "en|zh-CN")["Summary"].toString(), QString("online newer"));
        QCOMPARE(db.findDeepSeek("civil", "en|zh-CN")["Summary"].toString(), QString("AI summary"));
        QVERIFY(db.hasDeepSeek("civil", "en|zh-CN"));
        auto first = db.page("", false, 0);
        QCOMPARE(first.total, 1); QCOMPARE(first.rows.size(), 1);
        QCOMPARE(first.rows.first().summary, QString("AI summary"));
        QVERIFY(first.rows.first().hasAi && first.rows.first().hasOnline);
        // Page must not deserialize BLOBs. A corrupt unselected payload does not prevent browsing.
        sql(db.path(), [](QSqlDatabase &connection) { QSqlQuery q(connection); QVERIFY(q.exec("UPDATE sources SET payload=x'00'")); });
        QCOMPARE(db.page("", false, 0).total, 1);
        QVERIFY_EXCEPTION_THROWN(db.get(first.rows.first().id), std::runtime_error);
    }
    void pageFilterLimitAndExport() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        for (int i = 0; i < 58; ++i) db.store(entry(QString("word%1").arg(i), "fixture", "2026-01-01T00:00:00Z"), false);
        QCOMPARE(db.page("", false, 0).rows.size(), 50);
        QCOMPARE(db.page("", false, 50).rows.size(), 8);
        db.setFavorite("word1", "en|zh-CN", true);
        QCOMPARE(db.page("", true, 0).total, 1);
        QCOMPARE(db.page("word1", false, 0).total, 11);
        QString csv = temp.filePath("export.csv"); db.exportCsv(csv, "", true);
        QVERIFY(bytes(csv).startsWith(QByteArray::fromHex("efbbbf")));
        QVERIFY(bytes(csv).contains("word1")); QVERIFY(!bytes(csv).contains("word2"));
    }
    void favoriteDeleteTombstoneAndStableIds() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        auto e = entry("one", "first", "2026-01-01T00:00:00Z"); db.store(e);
        qint64 id = db.page("", false, 0).rows.first().id;
        db.setFavorite("one", "en|zh-CN", true); QVERIFY(!db.remove(id));
        db.setFavorite("one", "en|zh-CN", false); QVERIFY(db.remove(id));
        QVERIFY(db.find("one", "en|zh-CN").isEmpty());
        db.store(e); QVERIFY(db.find("one", "en|zh-CN").isEmpty());
        db.store(entry("two", "second", "2026-01-01T00:00:00Z"));
        QVERIFY(db.page("", false, 0).rows.first().id > id);
        QVERIFY(!db.remove(id)); // Old rendered-row IDs cannot delete a newly inserted word.
        db.saveFavorite(e); QVERIFY(db.isFavorite("one", "en|zh-CN"));
        QVERIFY(!db.find("one", "en|zh-CN").isEmpty());
    }
    void navigationAndDeletionCompaction() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        db.store(entry("one", "1", "2026-01-01T00:00:00Z"));
        db.store(entry("two", "2", "2026-01-01T00:00:00Z"));
        db.store(entry("three", "3", "2026-01-01T00:00:00Z"));
        QCOMPARE(db.navigate(-1)["Word"].toString(), QString("two")); QVERIFY(db.canNavigate(-1)); QVERIFY(db.canNavigate(1));
        auto rows = db.page("two", false, 0); QVERIFY(db.remove(rows.rows.first().id));
        QCOMPARE(db.navigate(0)["Word"].toString(), QString("one"));
        QCOMPARE(db.navigate(1)["Word"].toString(), QString("three"));
        QVERIFY(!db.canNavigate(1));
        db.navigate(-1); db.store(entry("four", "4", "2026-01-01T00:00:00Z"));
        QVERIFY(!db.canNavigate(1)); QCOMPARE(db.navigate(-1)["Word"].toString(), QString("one"));
    }
    void mergeDeletionAndFavoriteEvents() {
        QTemporaryDir temp;
        Database a(temp.filePath("a.sqlite"), "tester"), b(temp.filePath("b.sqlite"), "tester"); a.initialize(); b.initialize();
        auto e = entry("merge", "original", "2026-01-01T00:00:00Z"); a.store(e); b.mergeFrom(a.path());
        QVERIFY(!b.find("merge", "en|zh-CN").isEmpty());
        a.setFavorite("merge", "en|zh-CN", true); b.mergeFrom(a.path()); QVERIFY(b.isFavorite("merge", "en|zh-CN"));
        a.setFavorite("merge", "en|zh-CN", false); auto row = a.page("", false, 0).rows.first(); QVERIFY(a.remove(row.id));
        b.mergeFrom(a.path()); QVERIFY(b.find("merge", "en|zh-CN").isEmpty());
        // Stale snapshots/entries cannot resurrect a deleted word.
        b.store(e); QVERIFY(b.find("merge", "en|zh-CN").isEmpty());
        auto revision = b.revision(); b.mergeFrom(a.path()); QCOMPARE(b.revision(), revision);
        b.saveFavorite(e); a.mergeFrom(b.path()); QVERIFY(a.isFavorite("merge", "en|zh-CN"));
        QString snapshot = temp.filePath("snapshot.sqlite"); a.backupTo(snapshot);
        Database snap(snapshot, "tester"); snap.initialize(); QCOMPARE(snap.revision(), a.revision());
        QVERIFY(snap.isFavorite("merge", "en|zh-CN"));
        a.markSynced(a.revision()); QCOMPARE(a.syncedRevision(), a.revision());
        a.markSynced(0); QCOMPARE(a.syncedRevision(), a.revision());
    }
    void portableImportLedgerAndAtomicFailure() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        auto e = entry("word", "imported", "2026-01-01T00:00:00.1234567Z", true);
        QString at = "2026-01-01T00:00:01.0000001Z";
        auto p = ndjson(temp, {header(), {{"type", "entry"}, {"entry", e}, {"favorite", true}, {"favoriteAt", at}},
            {{"type", "favorite"}, {"word", "word"}, {"pair", "en|zh-CN"}, {"favorite", false}, {"updatedAt", at}},
            {{"type", "history"}, {"sourceKey", sourceKey(e)}, {"position", 0}}, {{"type", "cursor"}, {"position", 0}},
            {{"type", "importError"}, {"word", "fixture"}, {"error", "mock error"}, {"at", at}}});
        db.importEntries(p); QVERIFY(!db.isFavorite("word", "en|zh-CN"));
        QCOMPARE(db.navigate(0)["Summary"].toString(), QString("imported"));
        qint64 revision = db.revision(); db.importEntries(p); QCOMPARE(db.revision(), revision);
        auto failed = ndjson(temp, {header(), {{"type", "entry"}, {"entry", entry("rollback", "bad", "2026-01-01T00:00:00Z")}}, {{"type", "unknown"}}});
        QVERIFY_EXCEPTION_THROWN(db.importEntries(failed), std::runtime_error);
        QVERIFY(db.find("rollback", "en|zh-CN").isEmpty());
        auto badUser = ndjson(temp, {header("other")}); QVERIFY_EXCEPTION_THROWN(db.importEntries(badUser), std::runtime_error);
        // An orphan favorite event should apply when its entry is encountered later.
        auto orphan = ndjson(temp, {header(), {{"type", "favorite"}, {"word", "later"}, {"pair", "en|zh-CN"}, {"favorite", true}, {"updatedAt", at}}});
        db.importEntries(orphan); db.store(entry("later", "arrived", "2026-01-01T00:00:00Z")); QVERIFY(db.isFavorite("later", "en|zh-CN"));
    }
    void concurrentShortConnections() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        QList<QFuture<void>> jobs;
        for (int i = 0; i < 12; ++i) jobs.append(QtConcurrent::run([db, i]() mutable {
            db.store(entry(QString("parallel%1").arg(i), "safe", "2026-01-01T00:00:00Z"), false);
            auto page = db.page("", false, 0); if (page.total < 1) throw std::runtime_error("empty page");
        }));
        for (auto &job : jobs) job.waitForFinished();
        QCOMPARE(db.page("", false, 0).total, 12);
    }
    void refusesBinaryLegacyWithoutChangingOriginal() {
        QTemporaryDir temp; QString path = temp.filePath("legacy.sqlite");
        sql(path, [](QSqlDatabase &connection) {
            QSqlQuery q(connection); QVERIFY(q.exec("CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"));
            QVERIFY(q.exec("INSERT INTO metadata VALUES('username','tester'),('schema','1')"));
            QVERIFY(q.exec("CREATE TABLE sources(payload BLOB)")); QVERIFY(q.exec("INSERT INTO sources VALUES(x'1f8b0800')"));
        });
        auto original = bytes(path); Database db(path, "tester");
        QVERIFY_EXCEPTION_THROWN(db.initialize(), std::runtime_error);
        QCOMPARE(bytes(path), original);
        Database mismatch(temp.filePath("other.sqlite"), "other"); mismatch.initialize();
        Database target(temp.filePath("qt.sqlite"), "tester"); target.initialize();
        QVERIFY_EXCEPTION_THROWN(target.mergeFrom(mismatch.path()), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(StorageTest)
#include "storage_test.moc"
