#include "mainwindow.h"
#include "entryrenderer.h"
#include "core.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QMessageBox>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QFontDatabase>
using namespace MyDict;

class UiTest : public QObject {
    Q_OBJECT
private:
    QJsonObject entry() {
        QJsonObject example{{"Text","They run every day."},{"Chinese","他们每天跑步。"},{"GrammarNote","SHOULD_NOT_RENDER"}};
        QJsonObject sense{{"Definition","To move quickly on foot."},{"Chinese","用脚快速移动；奔跑"},{"Examples",QJsonArray{example}},{"Collocations",QJsonArray{"run a business（经营企业）"}}};
        return {{"Word","run"},{"Pair","en|zh-CN"},{"Summary","跑；经营"},{"Translation","奔跑"},{"LookupSource","DeepSeek"},{"LookupModel","fixture"},{"QueryEndpoint","https://example.invalid/chat/completions"},{"SuccessfulQueryAtUtc","2026-11-03T13:00:00.0000000Z"},
            {"Record",QJsonObject{{"Word","run"},{"Phonetic","/rʌn/"},{"Derived","runner 跑步者"},{"Forms",QJsonObject{{"ThirdPerson","runs"},{"PresentParticiple","running"},{"Past","ran"},{"PastParticiple","run"}}},{"Meanings",QJsonArray{QJsonObject{{"PartOfSpeech","verb"},{"Senses",QJsonArray{sense}}}}}}}};
    }
private slots:
    void initTestCase(){
        Q_INIT_RESOURCE(resources);
#ifdef Q_OS_WIN
        // The offscreen plugin has no Windows font engine. Load the installed
        // fonts explicitly for visual test artifacts without redistributing them.
        QString fonts=qEnvironmentVariable("SystemRoot")+"/Fonts/";
        for(const auto &name:QStringList{"msyh.ttc","msyhbd.ttc","georgia.ttf","georgiab.ttf","georgiai.ttf","segoeui.ttf"})QFontDatabase::addApplicationFont(fonts+name);
#endif
    }
    void renderRichEntry(){
        auto html=EntryRenderer::render(entry(),defaultSettings());
        QTextDocument document;document.setHtml(html);auto plain=document.toPlainText();
        QVERIFY(plain.contains("run a business"));QVERIFY(html.contains(QStringLiteral("经营企业")));
        QVERIFY(html.contains(QStringLiteral("过去式")));QVERIFY(html.contains("ran"));QVERIFY(html.contains("runner"));
        QVERIFY(html.contains("word:move"));QVERIFY(!html.contains("SHOULD_NOT_RENDER"));
        QVERIFY(!html.contains("n. 名词"));QVERIFY(!html.contains("来源：DeepSeek"));
        auto unsafe=entry();unsafe.insert("Summary","<script>alert(1)</script>");auto escaped=EntryRenderer::render(unsafe,defaultSettings());
        QVERIFY(!escaped.contains("<script>"));QVERIFY(escaped.contains("&lt;script&gt;"));
        auto options=defaultSettings();options.insert("ShowExamples",false);QVERIFY(!EntryRenderer::render(entry(),options).contains("They"));
    }
    void cacheFavoriteRemoveAndPaidConfirmation(){
        QTemporaryDir dir;QVERIFY(dir.isValid());Settings config(dir.path());config.load();config.data.insert("username","ui-test");auto ai=config.data.value("DeepSeek").toObject();ai.insert("Enabled",true);ai.insert("ApiKey","");config.data.insert("DeepSeek",ai);auto sync=config.data.value("CacheSync").toObject();sync.insert("Enabled",false);config.data.insert("CacheSync",sync);config.save();
        Database db(config.databasePath(),config.username());db.initialize();db.store(entry());
        MainWindow window(dir.path());window.show();
        auto *input=window.findChild<QLineEdit*>("queryInput");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");auto *star=window.findChild<QPushButton*>("favoriteButton");auto *bookTab=window.findChild<QPushButton*>("bookTab");
        QVERIFY(input&&browser&&star&&bookTab);QTRY_VERIFY(input->isEnabled());input->setText("run");QTRY_VERIFY(browser->toPlainText().contains("To move quickly"));QTRY_VERIFY(star->isEnabled());
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-preview.png");
        bool confirmed=false,defaultNo=false;
        QTimer::singleShot(60,[&]{for(QWidget *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget)){confirmed=true;defaultNo=box->defaultButton()==box->button(QMessageBox::No);box->done(QMessageBox::No);}});
        QTest::keyClick(input,Qt::Key_Return);QTRY_VERIFY(confirmed);QVERIFY(defaultNo);QTRY_VERIFY(browser->toPlainText().contains("To move quickly"));QTRY_VERIFY(star->isEnabled());
        // Hold a write lock while changing a favorite. The GUI timer must keep
        // running while the database worker waits for SQLite's busy timeout.
        {
            auto blocker=QSqlDatabase::addDatabase("QSQLITE","ui-lock");blocker.setDatabaseName(config.databasePath());QVERIFY(blocker.open());QSqlQuery q(blocker);QVERIFY(q.exec("BEGIN IMMEDIATE"));
            bool heartbeat=false;QTimer::singleShot(70,[&]{heartbeat=true;q.exec("ROLLBACK");});QTest::mouseClick(star,Qt::LeftButton);QTRY_VERIFY(heartbeat);QTRY_VERIFY(db.isFavorite("run","en|zh-CN"));blocker.close();
        }
        QSqlDatabase::removeDatabase("ui-lock");QTest::mouseClick(bookTab,Qt::LeftButton);
        auto *list=window.findChild<QListWidget*>("wordbook");QTRY_COMPARE(list->count(),1);
        auto *remove=window.findChild<QPushButton*>("rowRemove");auto *saved=window.findChild<QPushButton*>("rowFavorite");QVERIFY(remove&&saved);QVERIFY(!remove->isEnabled());
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-book-preview.png");
        QTest::mouseClick(saved,Qt::LeftButton);QTRY_VERIFY(!db.isFavorite("run","en|zh-CN"));
        QTRY_VERIFY(window.findChild<QPushButton*>("rowRemove")&&window.findChild<QPushButton*>("rowRemove")->isEnabled());QTest::mouseClick(window.findChild<QPushButton*>("rowRemove"),Qt::LeftButton);
        QTRY_VERIFY(db.find("run","en|zh-CN").isEmpty());QTRY_COMPARE(list->count(),0);
        // A deleted word in DeepSeek mode only offers a manual query. Typing
        // cannot spend API credit or restore the word.
        input->setText("run again");input->setText("run");QTest::qWait(900);QVERIFY(db.find("run","en|zh-CN").isEmpty());
        // Identity hot reload switches to a separately locked profile and restores
        // its own history without copying the previous user's entries.
        Settings edited(dir.path());edited.load();edited.data.insert("username","another-user");
        Database another(edited.databasePath(),edited.username());another.initialize();auto nextEntry=entry();nextEntry.insert("Word","walk");nextEntry.insert("Summary",QStringLiteral("走路"));another.store(nextEntry);edited.save();
        QTRY_VERIFY(input->isEnabled()&&input->text()=="walk");QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("走路")));
        QVERIFY(db.find("walk","en|zh-CN").isEmpty());QVERIFY(!another.find("walk","en|zh-CN").isEmpty());
        window.grab().save(dir.path()+"/ui-test.png");window.close();QTRY_VERIFY(!window.isVisible());
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
