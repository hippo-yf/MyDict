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
#include <QDir>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#include <QThread>
#include <thread>
#endif
#include <functional>
#include <stdexcept>
#include <cmath>

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
QJsonObject exampleEntry(QString word, const QStringList &sentences, QJsonObject forms = {}) {
    auto result = entry(word, word + " meaning", "2026-11-04T00:00:00Z", true);
    QJsonArray examples;
    for (const auto &sentence : sentences) examples.append(QJsonObject{{"Text", sentence}});
    const QJsonObject sense{{"Examples", examples}};
    const QJsonObject meaning{{"Senses", QJsonArray{sense}}};
    result.insert("Record", QJsonObject{{"Forms", forms}, {"Meanings", QJsonArray{meaning}}});
    return result;
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
void makeOldQtSnapshot(QString path) {
    sql(path, [](QSqlDatabase &connection) {
        QSqlQuery q(connection);QVERIFY(q.exec("UPDATE metadata SET value='qt-json-v1' WHERE key='payload_format'"));
        QVERIFY(q.exec("SELECT source_key,payload FROM sources"));QList<QPair<QString,QByteArray>> rows;
        while(q.next())rows.append({q.value(0).toString(),q.value(1).toByteArray()});q.finish();
        for(const auto &row:rows){QVERIFY(row.second.startsWith("MYDICT-JSON1\n"));
            QVERIFY(q.prepare("UPDATE sources SET payload=? WHERE source_key=?"));
            q.addBindValue(QByteArray("MYDICT-QT-JSON1\n")+row.second.mid(13));q.addBindValue(row.first);QVERIFY(q.exec());}
        QVERIFY(q.exec("PRAGMA wal_checkpoint(TRUNCATE)"));
    });
}
}

class StorageTest : public QObject {
    Q_OBJECT
private slots:
    void wordbookOrdersByReviewWeightAcrossPages() {
        QTemporaryDir temp;
        Database db(temp.filePath("words.sqlite"), "tester");
        db.initialize();
        for (const auto &word : QStringList{"apple", "pear", "book", "run"})
            db.store(entry(word, word + " meaning", "2026-11-04T00:00:00Z"));
        db.setFavorite("apple", "en|zh-CN", true);
        auto question = db.reviewQuestion();
        QVERIFY(db.answerReview(question, -1));
        QCOMPARE(db.page("", false, 0, 1).rows.first().word, QString("apple"));
        QVERIFY(db.page("", false, 1, 1).rows.first().word != "apple");
        QCOMPARE(db.page("app", false, 0).rows.first().word, QString("apple"));
        QCOMPARE(db.page("", true, 0).rows.first().word, QString("apple"));
        question = db.reviewQuestion();
        QVERIFY(db.answerReview(question, question.correctIndex));
        QCOMPARE(db.page("", false, 3, 1).rows.first().word, QString("apple"));
        QCOMPARE(db.page("", false, 0).total, 4);
        sql(db.path(), [&](QSqlDatabase &connection) {
            QSqlQuery q(connection);
            QVERIFY(q.exec("DROP TRIGGER remove_review_weight"));
            QVERIFY(q.exec("DROP TABLE review_weights"));
        });
        QCOMPARE(db.page("", false, 0).rows.size(), 4);
    }

    void clozeUsesWholeWordsFormsAndLegacyExamples() {
        struct Case { QString word; QStringList examples; QJsonObject forms; QString sentence, answer; };
        const QList<Case> cases{
            {"run", {"The runner is here.", "He ran yesterday.", "They run and she ran."},
                {{"Past", "ran"}}, "He ____ yesterday.", "ran"},
            {"run", {"Run, run!"}, {}, "____, ____!", "Run"},
            {"take off", {"Planes take off at noon."}, {}, "Planes ____ at noon.", "take off"}
        };
        for (const auto &test : cases) {
            QTemporaryDir temp;
            Database db(temp.filePath("words.sqlite"), "tester");
            db.initialize();
            db.saveFavorite(exampleEntry(test.word, test.examples, test.forms));
            db.saveFavorite(exampleEntry("missing", {"This sentence has no target."}));
            for (const auto &other : QStringList{"eat", "read", "sleep"})
                db.store(entry(other, other, "2026-11-04T00:00:00Z"));
            const auto question = db.reviewQuestion(true);
            QCOMPARE(question.word, test.word);
            QCOMPARE(question.sentence, test.sentence);
            QCOMPARE(question.options.at(question.correctIndex), test.answer);
            QCOMPARE(question.options.size(), 4);
            QVERIFY(db.answerReview(question, -1));
        }
        QTemporaryDir temp;
        Database db(temp.filePath("words.sqlite"), "tester");
        db.initialize();
        db.saveFavorite(exampleEntry("run", {"No usable example."}));
        auto online = entry("run", "run meaning", "2026-11-05T00:00:00Z");
        online.insert("Record", QJsonObject{{"Meanings", QJsonArray{QJsonObject{{"Senses", QJsonArray{
            QJsonObject{{"Example", "They run daily."}}}}}}}});
        db.store(online);
        for (const auto &other : QStringList{"eat", "read", "sleep"})
            db.store(entry(other, other, "2026-11-04T00:00:00Z"));
        QCOMPARE(db.reviewQuestion(true).sentence, QString("They ____ daily."));
        db.setFavorite("run", "en|zh-CN", false);
        QVERIFY_EXCEPTION_THROWN(db.reviewQuestion(true), std::runtime_error);
    }

    void earlierClozeExamplesAreMoreLikely() {
        QTemporaryDir temp;
        Database db(temp.filePath("words.sqlite"), "tester");
        db.initialize();
        db.saveFavorite(exampleEntry("run", {"They run every day.", "I run home."}));
        for (const auto &other : QStringList{"eat", "read", "sleep"})
            db.store(entry(other, other, "2026-11-04T00:00:00Z"));
        int first = 0;
        for (int i = 0; i < 2400; ++i) {
            const auto question = db.reviewQuestion(true);
            QCOMPARE(question.options.at(question.correctIndex), QString("run"));
            if (question.sentence.startsWith("They")) ++first;
        }
        // Expected 1600 with 1 : 1/2 weighting; wide bounds avoid flaky tests.
        QVERIFY2(first > 1400 && first < 1800, qPrintable(QString::number(first)));
    }

    void reviewWeightsPersistAndSampleFavorites() {
        QTemporaryDir temp;
        Database db(temp.filePath("words.sqlite"), "tester");
        db.initialize();
        const QString at = "2026-11-04T00:00:00Z";
        db.saveFavorite(entry("apple", QStringLiteral("苹果"), at));
        db.store(entry("pear", QStringLiteral("梨"), at));
        db.store(entry("book", QStringLiteral("书"), at));
        db.store(entry("run", QStringLiteral("跑步"), at));
        db.store(entry("duplicate", QStringLiteral(" 苹果 "), at));
        auto question = db.reviewQuestion();
        QCOMPARE(question.word, QString("apple"));
        QCOMPARE(question.options.size(), 4);
        QCOMPARE(question.options.at(question.correctIndex), QStringLiteral("苹果"));
        QCOMPARE(QSet<QString>(question.options.begin(), question.options.end()).size(), 4);
        auto weight = [&] {
            double logarithm = 0;
            sql(db.path(), [&](QSqlDatabase &connection) {
                QSqlQuery q(connection);
                QVERIFY(q.exec("SELECT log_weight FROM review_weights"));
                if (q.next()) logarithm = q.value(0).toDouble();
            });
            return std::exp(logarithm);
        };
        QCOMPARE(weight(), 1.0);
        const auto revision = db.revision();
        QVERIFY(db.answerReview(question, (question.correctIndex + 1) % 4));
        QVERIFY(std::abs(weight() - 1.3) < 1e-12);
        QVERIFY(db.answerReview(question, (question.correctIndex + 1) % 4));
        QVERIFY(std::abs(weight() - 1.3) < 1e-12);
        question = db.reviewQuestion();
        QVERIFY(db.answerReview(question, question.correctIndex));
        QVERIFY(std::abs(weight() - .91) < 1e-12);
        QCOMPARE(db.revision(), revision);
        Database reopened(db.path(), "tester");
        reopened.initialize();
        QVERIFY(std::abs(weight() - .91) < 1e-12);
        question = db.reviewQuestion();
        QVERIFY(db.answerReview(question, -1));
        QVERIFY(std::abs(weight() - 1.183) < 1e-12);
        QVERIFY(db.answerReview(question, -1));
        QVERIFY(std::abs(weight() - 1.183) < 1e-12);
        db.setFavorite("pear", "en|zh-CN", true);
        sql(db.path(), [&](QSqlDatabase &connection) {
            QSqlQuery q(connection);
            QVERIFY(q.exec("UPDATE review_weights SET log_weight=1000"));
        });
        for (int i = 0; i < 8; ++i) QCOMPARE(db.reviewQuestion().word, QString("apple"));
        db.setFavorite("apple", "en|zh-CN", false);
        QVERIFY(!db.answerReview(question, question.correctIndex));
        QCOMPARE(db.reviewQuestion().word, QString("pear"));
        QVERIFY(!db.remove(question.wordId));
        sql(db.path(), [&](QSqlDatabase &connection) {
            QSqlQuery q(connection);
            QVERIFY(q.exec("SELECT COUNT(*) FROM review_weights"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
        });
    }

    void reviewHandlesOldSchemaAndInsufficientChoices() {
        QTemporaryDir temp;
        Database db(temp.filePath("words.sqlite"), "tester");
        db.initialize();
        sql(db.path(), [&](QSqlDatabase &connection) {
            QSqlQuery q(connection);
            QVERIFY(q.exec("DROP TRIGGER remove_review_weight"));
            QVERIFY(q.exec("DROP TABLE review_weights"));
        });
        db.initialize();
        QVERIFY_EXCEPTION_THROWN(db.reviewQuestion(), std::runtime_error);
        const QString at = "2026-11-04T00:00:00Z";
        db.saveFavorite(entry("apple", QStringLiteral("苹果"), at));
        db.store(entry("pear", QStringLiteral("梨"), at));
        db.store(entry("book", QStringLiteral("书"), at));
        db.store(entry("duplicate", QStringLiteral(" 梨 "), at));
        QVERIFY_EXCEPTION_THROWN(db.reviewQuestion(), std::runtime_error);
        db.store(entry("run", QStringLiteral("跑步"), at));
        QCOMPARE(db.reviewQuestion().options.size(), 4);
    }

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
        QVERIFY(s.databasePath().endsWith("/words-shared.sqlite"));
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
    void settingsWindowsShortSharingLocksAreRetried() {
#ifdef Q_OS_WIN
        // Cover both atomic replacements: the live file and the backup. These
        // real Win32 readers allow reading/writing but deliberately deny delete.
        for (const auto &suffix : QStringList{QString(), QStringLiteral(".bak")}) {
            QTemporaryDir temp; QVERIFY(temp.isValid()); Settings settings(temp.path()); settings.load();
            settings.data["_lock_revision"] = "before"; settings.save();
            const auto original = bytes(settings.path);
            const auto user = settings.username(), device = settings.data["CacheSync"].toObject()["DeviceId"].toString();
            const auto lockedPath = QDir::toNativeSeparators(settings.path + suffix);
            HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(lockedPath.utf16()), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            QVERIFY2(locked != INVALID_HANDLE_VALUE, qPrintable(QString("CreateFileW failed: %1").arg(GetLastError())));
            std::thread release([locked] { QThread::msleep(20); CloseHandle(locked); });
            settings.data["_lock_revision"] = "after";
            QString error;
            try { settings.save(); } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
            release.join();
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(bytes(settings.path + ".bak"), original);
            Settings restored(temp.path()); restored.load();
            QCOMPARE(restored.data["_lock_revision"].toString(), QString("after"));
            QCOMPARE(restored.username(), user); QCOMPARE(restored.data["CacheSync"].toObject()["DeviceId"].toString(), device);
            QCOMPARE(QJsonDocument::fromJson(bytes(settings.path + ".bak")).object()["username"].toString(), user);
            QVERIFY(!settings.reload());
        }
#else
        QSKIP("FILE_SHARE_DELETE behavior requires Windows.");
#endif
    }
    void settingsWindowsPermanentSharingLockPreservesOriginal() {
#ifdef Q_OS_WIN
        QTemporaryDir temp; QVERIFY(temp.isValid()); Settings settings(temp.path()); settings.load();
        settings.data["_lock_revision"] = "before"; settings.save();
        const auto original = bytes(settings.path);
        const auto user = settings.username();
        const auto lockedPath = QDir::toNativeSeparators(settings.path);
        HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(lockedPath.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY2(locked != INVALID_HANDLE_VALUE, qPrintable(QString("CreateFileW failed: %1").arg(GetLastError())));
        settings.data["_lock_revision"] = "not saved";
        QString error;
        try { settings.save(); } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
        CloseHandle(locked);
        const auto prefix = QStringLiteral("保存 settings.json 失败，原文件已保留。");
        QVERIFY2(error.startsWith(prefix), qPrintable(error)); QVERIFY(error.size() > prefix.size() + 1);
        QCOMPARE(bytes(settings.path), original); QCOMPARE(bytes(settings.path + ".bak"), original);
        QVERIFY(!settings.reload());
        Settings restored(temp.path()); restored.load();
        QCOMPARE(restored.username(), user); QCOMPARE(restored.data["_lock_revision"].toString(), QString("before"));
#else
        QSKIP("FILE_SHARE_DELETE behavior requires Windows.");
#endif
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
    void selectedSourceGroupsAreStrictAndKeepAiBookSummary() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        QVERIFY(db.findForSource("civil", "en|zh-CN", false).isEmpty());
        QVERIFY(db.findForSource("civil", "en|zh-CN", true).isEmpty());
        db.store(entry("civil", "AI original", "2026-01-01T00:00:00.0000001Z", true), false);
        db.store(entry("civil", "MyMemory", "2026-01-01T00:00:00.0000002Z"), false);
        auto ai = entry("civil", "AI new model", "2026-01-01T00:00:00.0000003Z", true);
        ai["LookupModel"] = "new-model"; db.store(ai, false);
        auto online = entry("civil", "LibreTranslate newest", "2026-01-01T00:00:00.0000004Z");
        online["TranslationProvider"] = "LibreTranslate"; online["TranslationUrl"] = "https://example.invalid/translate"; db.store(online, false);
        QCOMPARE(db.findForSource(" CIVIL ", "en|zh-CN", true)["Summary"].toString(), QString("AI new model"));
        QCOMPARE(db.findForSource("civil", "en|zh-CN", false)["Summary"].toString(), QString("LibreTranslate newest"));
        QCOMPARE(db.find("civil", "en|zh-CN")["Summary"].toString(), QString("LibreTranslate newest"));
        QVERIFY(db.findForSource("civil", "zh-CN|en", false).isEmpty());
        const auto page = db.page("civil", false, 0);
        QCOMPARE(page.total, 1); QCOMPARE(page.rows.first().summary, QString("AI new model"));
        QVERIFY(page.rows.first().hasAi && page.rows.first().hasOnline);
        db.store(entry("ai-only", "AI only", "2026-01-01T00:00:00Z", true), false);
        db.store(entry("online-only", "online only", "2026-01-01T00:00:00Z"), false);
        QVERIFY(db.findForSource("ai-only", "en|zh-CN", false).isEmpty());
        QVERIFY(db.findForSource("online-only", "en|zh-CN", true).isEmpty());
        // Looking at Online must not deserialize even a broken AI payload.
        sql(db.path(), [](QSqlDatabase &connection) { QSqlQuery q(connection); QVERIFY(q.exec("UPDATE sources SET payload=x'00' WHERE is_ai=1")); });
        QCOMPARE(db.findForSource("civil", "en|zh-CN", false)["Summary"].toString(), QString("LibreTranslate newest"));
        QVERIFY_EXCEPTION_THROWN(db.findForSource("civil", "en|zh-CN", true), std::runtime_error);
    }
    void selectedSourceHistoryRetainsWordsAndRestoresCursor() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        auto selected = db.navigateForSource(0, true);
        QVERIFY(selected.word.isEmpty()); QVERIFY(selected.pair.isEmpty()); QVERIFY(selected.entry.isEmpty());
        db.store(entry("civil", "AI civil", "2026-01-01T00:00:00.0000001Z", true));
        db.store(entry("civil", "online civil", "2026-01-01T00:00:00.0000002Z"));
        db.store(entry("ai-only", "AI only", "2026-01-01T00:00:00Z", true));
        db.store(entry("online-only", "online only", "2026-01-01T00:00:00Z"));
        selected = db.navigateForSource(0, true);
        QCOMPARE(selected.word, QString("online-only")); QCOMPARE(selected.pair, QString("en|zh-CN")); QVERIFY(selected.entry.isEmpty());
        QVERIFY(db.canNavigate(-1)); QVERIFY(!db.canNavigate(1));
        Database restored(db.path(), "tester");
        selected = restored.navigateForSource(0, false);
        QCOMPARE(selected.word, QString("online-only")); QCOMPARE(selected.entry["Summary"].toString(), QString("online only"));
        selected = restored.navigateForSource(-1, false);
        QCOMPARE(selected.word, QString("ai-only")); QCOMPARE(selected.pair, QString("en|zh-CN")); QVERIFY(selected.entry.isEmpty());
        selected = db.navigateForSource(0, true);
        QCOMPARE(selected.word, QString("ai-only")); QCOMPARE(selected.entry["Summary"].toString(), QString("AI only"));
        // Both civil history positions honor the selected group, independently
        // of the source originally recorded at that particular position.
        selected = db.navigateForSource(-1, true);
        QCOMPARE(selected.word, QString("civil")); QCOMPARE(selected.entry["Summary"].toString(), QString("AI civil"));
        selected = db.navigateForSource(-1, false);
        QCOMPARE(selected.word, QString("civil")); QCOMPARE(selected.entry["Summary"].toString(), QString("online civil"));
        QVERIFY(!db.canNavigate(-1)); QVERIFY(db.canNavigate(1));
        QCOMPARE(db.navigateForSource(-1, false).entry["Summary"].toString(), QString("online civil"));
        QCOMPARE(db.navigateForSource(1, true).entry["Summary"].toString(), QString("AI civil"));
        // Original navigation still returns its established latest-body result.
        QCOMPARE(db.navigate(0)["Summary"].toString(), QString("online civil"));
        sql(db.path(), [](QSqlDatabase &connection) { QSqlQuery q(connection); QVERIFY(q.exec("SELECT COUNT(*) FROM history")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 4); });
        // The history's original AI source is only an identity join: a corrupt
        // unselected payload must not prevent displaying the Online source.
        db.navigateForSource(-1, false);
        sql(db.path(), [](QSqlDatabase &connection) { QSqlQuery q(connection); QVERIFY(q.exec("UPDATE sources SET payload=x'00' WHERE is_ai=1")); });
        selected = restored.navigateForSource(0, false);
        QCOMPARE(selected.word, QString("civil")); QCOMPARE(selected.entry["Summary"].toString(), QString("online civil"));
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
    void unfavoritePreservesCacheAndStableIds() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        auto e = entry("one", "first", "2026-01-01T00:00:00Z"); db.store(e);
        qint64 id = db.page("", false, 0).rows.first().id;
        db.setFavorite("one", "en|zh-CN", true); QVERIFY(!db.remove(id));
        db.setFavorite("one", "en|zh-CN", false); QVERIFY(!db.remove(id));
        QVERIFY(!db.find("one", "en|zh-CN").isEmpty());
        db.store(e); QVERIFY(!db.find("one", "en|zh-CN").isEmpty());
        db.store(entry("two", "second", "2026-01-01T00:00:00Z"));
        QCOMPARE(db.page("", false, 0).total, 2);
        QVERIFY(!db.remove(id)); // Old rendered-row IDs cannot delete a newly inserted word.
        db.saveFavorite(e); QVERIFY(db.isFavorite("one", "en|zh-CN"));
        QVERIFY(!db.find("one", "en|zh-CN").isEmpty());
    }
    void navigationSurvivesRejectedDeletion() {
        QTemporaryDir temp; Database db(temp.filePath("words.sqlite"), "tester"); db.initialize();
        db.store(entry("one", "1", "2026-01-01T00:00:00Z"));
        db.store(entry("two", "2", "2026-01-01T00:00:00Z"));
        db.store(entry("three", "3", "2026-01-01T00:00:00Z"));
        QCOMPARE(db.navigate(-1)["Word"].toString(), QString("two")); QVERIFY(db.canNavigate(-1)); QVERIFY(db.canNavigate(1));
        auto rows = db.page("two", false, 0); QVERIFY(!db.remove(rows.rows.first().id));
        QCOMPARE(db.navigate(0)["Word"].toString(), QString("two"));
        QCOMPARE(db.navigate(1)["Word"].toString(), QString("three"));
        QVERIFY(!db.canNavigate(1));
        db.navigate(-1); db.store(entry("four", "4", "2026-01-01T00:00:00Z"));
        QVERIFY(!db.canNavigate(1)); QCOMPARE(db.navigate(-1)["Word"].toString(), QString("two"));
    }
    void mergeIgnoresDeletionAndKeepsFavoriteEvents() {
        QTemporaryDir temp;
        Database a(temp.filePath("a.sqlite"), "tester"), b(temp.filePath("b.sqlite"), "tester"); a.initialize(); b.initialize();
        auto e = entry("merge", "original", "2026-01-01T00:00:00Z"); a.store(e); b.mergeFrom(a.path());
        QVERIFY(!b.find("merge", "en|zh-CN").isEmpty());
        a.setFavorite("merge", "en|zh-CN", true); b.mergeFrom(a.path()); QVERIFY(b.isFavorite("merge", "en|zh-CN"));
        a.setFavorite("merge", "en|zh-CN", false); auto row = a.page("", false, 0).rows.first(); QVERIFY(!a.remove(row.id));
        sql(a.path(),[](QSqlDatabase &connection){QSqlQuery q(connection);QVERIFY(q.exec("INSERT INTO deleted_words VALUES('merge','en|zh-CN',999999999999999999)"));});
        b.mergeFrom(a.path()); QVERIFY(!b.find("merge", "en|zh-CN").isEmpty());
        // Legacy deletion markers must not remove existing cached definitions.
        b.store(e); QVERIFY(!b.find("merge", "en|zh-CN").isEmpty());
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
        QVERIFY(!Database::isJsonSnapshot(path));
        QVERIFY_EXCEPTION_THROWN(db.initialize(), std::runtime_error);
        QCOMPARE(bytes(path), original);
        Database mismatch(temp.filePath("other.sqlite"), "other"); mismatch.initialize();
        Database target(temp.filePath("qt.sqlite"), "tester"); target.initialize();
        QVERIFY_EXCEPTION_THROWN(target.mergeFrom(mismatch.path()), std::runtime_error);
    }
    void sharedPayloadAndReadOnlyQtUpgradePreserveLocalState() {
        QTemporaryDir temp;const auto oldPath=temp.filePath("words-qt.sqlite");Database old(oldPath,"tester");old.initialize();
        auto keep=entry("keep","AI summary","2026-01-01T00:00:00.1234567Z",true);
        old.store(keep);old.setFavorite("keep","en|zh-CN",true);
        old.store(entry("other","other","2026-01-01T00:00:00.1234568Z"));
        const auto deleted=entry("deleted","old","2026-01-01T00:00:00Z");old.store(deleted);
        sql(old.path(),[](QSqlDatabase &connection){QSqlQuery q(connection);QVERIFY(q.exec("INSERT INTO deleted_words VALUES('deleted','en|zh-CN',999999999999999999)"));QVERIFY(q.exec("DELETE FROM history WHERE source_key IN (SELECT source_key FROM sources WHERE word_id=(SELECT id FROM words WHERE word='deleted'))"));QVERIFY(q.exec("DELETE FROM sources WHERE word_id=(SELECT id FROM words WHERE word='deleted')"));QVERIFY(q.exec("DELETE FROM words WHERE word='deleted'"));});old.recordImportError("fixture","old failure");
        makeOldQtSnapshot(oldPath);const auto original=bytes(oldPath);
        QVERIFY(Database::isJsonSnapshot(oldPath));QCOMPARE(old.find("keep","en|zh-CN")["Summary"].toString(),QString("AI summary"));
        QVERIFY_EXCEPTION_THROWN(old.initialize(),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(old.store(keep),std::runtime_error);
        QCOMPARE(bytes(oldPath),original);
        Database shared(temp.filePath("words-shared.sqlite"),"tester");shared.initialize();
        QCOMPARE(shared.page({},false,0).total,2);QVERIFY(shared.isFavorite("keep","en|zh-CN"));
        QCOMPARE(shared.navigate(0)["Word"].toString(),QString("other"));QCOMPARE(shared.navigate(-1)["Word"].toString(),QString("keep"));
        shared.store(deleted,false);QVERIFY(!shared.find("deleted","en|zh-CN").isEmpty());
        sql(shared.path(),[](QSqlDatabase &connection){QSqlQuery q(connection);QVERIFY(q.exec("SELECT value FROM metadata WHERE key='payload_format'"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("mydict-json-v1"));
            QVERIFY(q.exec("SELECT payload,ticks FROM sources ORDER BY ticks DESC"));QVERIFY(q.next());QVERIFY(q.value(0).toByteArray().startsWith("MYDICT-JSON1\n"));
            QCOMPARE(q.value(1).toLongLong()%10000000,qint64(1234568));QVERIFY(q.exec("SELECT COUNT(*) FROM import_errors"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);});
        shared.setFavorite("keep","en|zh-CN",false);const auto revision=shared.revision();shared.initialize();
        QCOMPARE(shared.revision(),revision);QVERIFY(!shared.isFavorite("keep","en|zh-CN"));
        QCOMPARE(shared.navigate(0)["Word"].toString(),QString("keep"));QCOMPARE(bytes(oldPath),original);
    }
};
QTEST_GUILESS_MAIN(StorageTest)
#include "storage_test.moc"
