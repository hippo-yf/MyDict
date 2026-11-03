#include "mainwindow.h"
#include "entryrenderer.h"
#include "core.h"
#include "appfonts.h"
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
#include <QDir>
#include <QFileInfo>
#include <QTextLayout>
#include <QGlyphRun>
#include <QTextCursor>
using namespace MyDict;

class UiTest : public QObject {
    Q_OBJECT
private:
    static QStringList nerdFiles() {
        QStringList files;
        for(const auto &file:AppFonts::loadedFiles())
            if(QFileInfo(file).fileName().startsWith("CaskaydiaCoveNerdFontPropo-",Qt::CaseInsensitive))
                files.append(QFileInfo(file).canonicalFilePath());
        files.sort(Qt::CaseInsensitive);
        return files;
    }
    static bool shapesChinese(const QFont &font) {
        QTextLayout layout(QStringLiteral("拾词查询收藏释义"),font);layout.beginLayout();
        auto line=layout.createLine();if(line.isValid())line.setLineWidth(10000);layout.endLayout();
        bool found=false;
        for(const auto &run:layout.glyphRuns())for(auto glyph:run.glyphIndexes()){found=true;if(!glyph)return false;}
        return found;
    }
    static void fontStyle(Settings &settings,const QString &key,const QString &family) {
        auto styles=settings.data.value("Styles").toObject();bool found=false;
        if(styles.contains(key)){styles.insert(key,family);found=true;}
        else for(auto i=styles.begin();i!=styles.end();++i){
            if(!i.value().isObject())continue;auto group=i.value().toObject();
            if(group.contains(key)){group.insert(key,family);styles.insert(i.key(),group);found=true;break;}
        }
        if(!found){auto group=styles.value("99_FontLoadingTest").toObject();group.insert(key,family);styles.insert("99_FontLoadingTest",group);}
        settings.data.insert("Styles",styles);
    }
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
    void cleanup(){AppFonts::apply({},{});}
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
    void fontsFollowVisibleContentAndSettings() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
#ifdef FONT_SOURCE_DIR
        const QDir source(QString::fromUtf8(FONT_SOURCE_DIR));
#else
        const QDir source(QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("../fonts"));
#endif
        const QString fonts=QDir(dir.path()).filePath("fonts");QVERIFY(QDir().mkpath(fonts));
        const auto names=source.entryList({"CaskaydiaCoveNerdFontPropo-*.ttf"},QDir::Files,QDir::Name);QCOMPARE(names.size(),4);
        QStringList expected;
        for(const auto &name:names){const QString target=QDir(fonts).filePath(name);QVERIFY2(QFile::copy(source.filePath(name),target),qPrintable(name));expected.append(QFileInfo(target).canonicalFilePath());}
        expected.sort(Qt::CaseInsensitive);
        const QString noto=QDir(fonts).filePath("NotoSansSC-Variable.ttf");QVERIFY(QFile::copy(source.filePath("NotoSansSC-Variable.ttf"),noto));
        const auto notoLoaded=[&]{for(const auto &file:AppFonts::loadedFiles())if(QFileInfo(file).canonicalFilePath()==QFileInfo(noto).canonicalFilePath())return true;return false;};

        Settings config(dir.path());config.load();config.data.insert("username","ui-fonts-test");
        auto ai=config.data.value("DeepSeek").toObject();ai.insert("Enabled",true);ai.insert("ApiKey","");config.data.insert("DeepSeek",ai);
        auto sync=config.data.value("CacheSync").toObject();sync.insert("Enabled",false);config.data.insert("CacheSync",sync);
        const QString nerd="CaskaydiaCove NFP";
        // Compatibility fields and unused WPF keys must not register a local
        // family merely because it is mentioned anywhere in settings.json.
        config.data.insert("FontName",nerd);config.data.insert("DefinitionFontName",nerd);
        for(const auto &key:QStringList{"font.settingswindow.window.1.fontfamily","font.about.body","font.doc.tabs","font.doc.phonetic"})fontStyle(config,key,nerd);
        const QString body=styleValue(config.data,"font.doc.body","Microsoft YaHei UI");QVERIFY(body!=nerd);config.save();
        const QString ui=styleValue(config.data,"font.mainwindow.window.1.fontfamily","Microsoft YaHei UI");
        const bool needsFallback=!shapesChinese(QFont(ui))||!shapesChinese(QFont(body));
        Database db(config.databasePath(),config.username());db.initialize();db.store(entry(),false);
        auto noPhonetic=entry();noPhonetic.insert("Word","walk");auto record=noPhonetic.value("Record").toObject();record.insert("Word","walk");record.remove("Phonetic");noPhonetic.insert("Record",record);db.store(noPhonetic,false);
        // No history entry: startup must display a message, not immediately
        // restore the cached record and its phonetic font.
        MainWindow window(dir.path());window.show();
        auto *input=window.findChild<QLineEdit*>("queryInput");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");QVERIFY(input&&browser);
        QTRY_VERIFY(input->isEnabled());QVERIFY(input->text().isEmpty());
        QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("输入中文或英文")));
        QVERIFY(nerdFiles().isEmpty());
        QCOMPARE(notoLoaded(),needsFallback);
        QVERIFY(shapesChinese(window.font()));
        auto bodyCursor=browser->document()->find(QStringLiteral("输入中文或英文"));QVERIFY(!bodyCursor.isNull());QVERIFY(shapesChinese(bodyCursor.charFormat().font()));

        // Even a real entry with POS tabs does not use the WPF-only tab font;
        // an absent IPA field must not activate the configured phonetic family.
        input->setText("walk");QTRY_VERIFY(browser->toPlainText().contains("To move quickly"));
        QVERIFY(!browser->toPlainText().contains("/rʌn/"));QVERIFY(nerdFiles().isEmpty());

        // A normal cache hit exposes the IPA field and needs all four local
        // faces. No query button/Enter is used, so no paid request can occur.
        input->setText("run");QTRY_VERIFY(browser->toPlainText().contains("/rʌn/"));
        QTRY_COMPARE(nerdFiles(),expected);
        QVERIFY(browser->document()->toHtml().contains("CaskaydiaCove"));
        input->clear();QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("输入中文或英文")));
        QTRY_VERIFY(nerdFiles().isEmpty());

        // Atomic external saves exercise the live window and its file watcher,
        // including rearming the watcher for a second edit to the same file.
        Settings edited(dir.path());edited.load();fontStyle(edited,"font.doc.body",nerd);edited.save();
        QTRY_COMPARE(nerdFiles(),expected);
        QTRY_VERIFY(browser->document()->toHtml().contains("CaskaydiaCove"));
        bodyCursor=browser->document()->find(QStringLiteral("输入中文或英文"));QVERIFY(!bodyCursor.isNull());QVERIFY(shapesChinese(bodyCursor.charFormat().font()));
        fontStyle(edited,"font.doc.body",body);edited.save();
        QTRY_VERIFY(nerdFiles().isEmpty());
        QTRY_VERIFY(!browser->document()->toHtml().contains("CaskaydiaCove"));
        QTRY_COMPARE(notoLoaded(),needsFallback);
        // A real CJK fallback may remain registered when the system has no
        // Chinese fonts; it must not cause unrelated Nerd faces to remain.
        QVERIFY(window.close());QTRY_VERIFY(!window.isVisible());
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
