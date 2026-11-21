#include <QtTest>
#include "dailyword.h"
#include "dailypage.h"
#include "mainwindow.h"
#include <QLineEdit>
#include <QStackedWidget>
#include "entryrenderer.h"
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QDateTime>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QScrollBar>
using namespace MyDict;
class DailyTest:public QObject{
    Q_OBJECT
    QJsonObject offlineSettings(){auto settings=MyDict::defaultSettings();auto daily=settings.value("DailyWord").toObject();daily.insert("ShowImage",false);settings.insert("DailyWord",daily);return settings;}
    QJsonArray sources(){return {QJsonObject{{"Title","Scientists study resilience in forests"},{"Summary","Researchers examine recovery after drought."},{"Url","https://example.org/science"},{"Source","Fixture News"},{"Topic","科学"},{"PublishedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}};}
    QJsonObject response(){
        QJsonArray examples{QJsonObject{{"text","Forests show resilience."},{"chinese","森林表现出恢复能力。"},{"segments",QJsonArray{QJsonObject{{"text","Forests "},{"role","subject"}},QJsonObject{{"text","show "},{"role","predicate"}},QJsonObject{{"text","resilience."},{"role","object"}}}}},QJsonObject{{"text","We value resilience."},{"chinese","我们重视韧性。"}}};
        QJsonObject dict{{"word","resilience"},{"summary","韧性；恢复能力"},{"translation","韧性；恢复能力"},{"senses",QJsonArray{QJsonObject{{"partOfSpeech","noun"},{"definition","The ability to recover."},{"chinese","恢复能力"},{"collocations",QJsonArray{QJsonObject{{"text","build resilience"},{"chinese","增强韧性"}},QJsonObject{{"text","ecological resilience"},{"chinese","生态恢复力"}}}},{"examples",examples}}}}};
        return {{"articleIndex",0},{"word","resilience"},{"why","研究关注森林恢复能力。"},{"contextMeaning","这里指生态系统恢复的能力。"},{"quoteChinese","科学家研究森林的恢复能力"},{"confusion","recovery 强调恢复过程。"},{"dictionary",dict}};
    }
private slots:
    void initTestCase(){Q_INIT_RESOURCE(resources);}
    void preferredNewsAndLocalSelection(){
        const auto feeds=DailyWord::newsSources(offlineSettings());QVERIFY(feeds.size()>=6);
        QCOMPARE(feeds[0].toObject().value("Name").toString(),QString("Xinhua"));
        QCOMPARE(feeds[1].toObject().value("Name").toString(),QString("China Daily"));
        QCOMPARE(feeds[2].toObject().value("Name").toString(),QString("CGTN"));
        const auto now=QDateTime::currentDateTimeUtc();const auto path=now.date().toString("yyyyMMdd");
        auto links=DailyWord::parseNewsLinks("<a href='/"+path+"/abc/c.html'>Scientists study resilience in forests</a><a href='https://evil.test/"+path+"/a'>Scientists study resilience</a><a href='/20190101/a'>Scientists study resilience</a>",feeds[0].toObject(),now);
        QCOMPARE(links.size(),1);
        auto parsed=DailyWord::parseNewsArticle("<meta property='og:title' content='Scientists study resilience'><span id='detailContent'><p>Researchers examine resilience after drought.</p><p>Forests recover through ecological restoration.</p></span><p>Unrelated navigation outside the article.</p>",links[0].toObject());
        QVERIFY(!parsed.isEmpty());QVERIFY(!parsed.value("Summary").toString().contains("Unrelated"));QVERIFY(parsed.value("Summary").toString().endsWith("restoration."));
        auto selected=DailyWord::selectWord(QJsonArray{parsed},{});QCOMPARE(selected.value("word").toString(),QString("resilience"));
        QVERIFY(DailyWord::selectWord(QJsonArray{parsed},{"resilience","drought","ecological","restoration"}).isEmpty());
        QVERIFY(DailyWord::parseNewsArticle("<p>Only navigation without article container.</p>",{}).isEmpty());
        auto china=DailyWord::parseNewsLinks("<a href='/a/"+now.date().toString("yyyyMM/dd")+"/WSabc.html'>Marine biodiversity conservation advances</a>",feeds[1].toObject(),now);QCOMPARE(china.size(),1);
    }
    void downloadedNewsFixtures(){
        const auto dir=qEnvironmentVariable("MYDICT_NEWS_FIXTURES");if(dir.isEmpty())QSKIP("Optional real-page parser check");
        const auto feeds=DailyWord::newsSources(offlineSettings());
        QFile rss(dir+"/news-cgtn.xml");QVERIFY(rss.open(QIODevice::ReadOnly));const auto cgtn=DailyWord::parseFeed(rss.readAll(),feeds[2].toObject(),QDateTime::currentDateTimeUtc());QVERIFY(!cgtn.isEmpty());QVERIFY(!DailyWord::selectWord(cgtn,{}).isEmpty());
        for(int i=0;i<2;++i){QFile home(dir+"/news-probe-"+QString::number(i)+".html");QVERIFY(home.open(QIODevice::ReadOnly));auto links=DailyWord::parseNewsLinks(QString::fromUtf8(home.readAll()),feeds[i].toObject(),QDateTime::currentDateTimeUtc());QVERIFY(!links.isEmpty());QVERIFY(!DailyWord::selectWord(links,{}).isEmpty());
            QFile detail(dir+"/news-probe-"+QString::number(i+3)+".html");QVERIFY(detail.open(QIODevice::ReadOnly));auto parsed=DailyWord::parseNewsArticle(QString::fromUtf8(detail.readAll()),links[0].toObject());QVERIFY(!parsed.isEmpty());}
    }
    void feedAndValidation(){
        auto now=QDateTime::currentDateTimeUtc();auto date=now.toString(Qt::RFC2822Date);date.replace("+0000","GMT");
        QByteArray xml=("<rss><channel><item><title>Scientists study resilience</title><description>Forests recover.</description><link>https://example.org/news</link><pubDate>"+date+"</pubDate></item><item><title>Old</title><link>https://example.org/old</link><pubDate>Tue, 01 Jan 2019 12:00:00 GMT</pubDate></item></channel></rss>").toUtf8();
        auto parsed=DailyWord::parseFeed(xml,{{"Name","Fixture"},{"Topic","科学"}},now);QCOMPARE(parsed.size(),1);
        auto settings=offlineSettings();auto article=DailyWord::validate(response(),sources(),{},settings);QCOMPARE(article.value("Word").toString(),QString("resilience"));
        auto invalid=response();invalid.insert("word","fabrication");QVERIFY_THROWS_EXCEPTION(std::runtime_error,DailyWord::validate(invalid,sources(),{},settings));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,DailyWord::validate(response(),sources(),{"resilience"},settings));
        invalid=response();invalid.insert("articleIndex",99);QVERIFY_THROWS_EXCEPTION(std::runtime_error,DailyWord::validate(invalid,sources(),{},settings));
        const auto html=EntryRenderer::daily(article,settings);QVERIFY(html.contains("https://example.org/science"));QVERIFY(html.contains("#2563a6",Qt::CaseInsensitive));QVERIFY(html.contains(QStringLiteral("增强韧性")));QVERIFY(html.indexOf("Fixture News")<html.indexOf("resilience"));QTextDocument rendered;rendered.setHtml(html);QVERIFY(rendered.toPlainText().contains("Researchers examine recovery"));QVERIFY(rendered.toPlainText().contains("The ability to recover"));QVERIFY(!html.contains(QStringLiteral("新闻语境 · 标题与摘要")));
    }
    void sentenceSummaryCoverageAndMerge(){
        QJsonArray rows{QJsonObject{{"Text","Birds fly."},{"Chinese","鸟会飞。"},{"Segments",QJsonArray{QJsonObject{{"Text","Birds "},{"Role","subject"}},QJsonObject{{"Text","fly."},{"Role","predicate"}}}}},QJsonObject{{"Text","They migrate."},{"Chinese","它们迁徙。"},{"Segments",QJsonArray{QJsonObject{{"Text","They "},{"Role","subject"}},QJsonObject{{"Text","migrate."},{"Role","predicate"}}}}}};
        QCOMPARE(DailyWord::validateSummary(rows,"Birds fly. They migrate.").size(),2);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,DailyWord::validateSummary(rows,"Birds fly. They migrate. A missing sentence."));
        auto bad=rows;auto first=bad[0].toObject();first.insert("Segments",QJsonArray());bad[0]=first;QVERIFY_THROWS_EXCEPTION(std::runtime_error,DailyWord::validateSummary(bad,"Birds fly. They migrate."));
        QTemporaryDir temp;Database a(temp.filePath("a.sqlite"),"daily"),b(temp.filePath("b.sqlite"),"daily");a.initialize();b.initialize();auto old=DailyWord::validate(response(),sources(),{},offlineSettings());a.saveDailyArticle(old);b.saveDailyArticle(old);
        auto enriched=old;enriched.insert("SummarySentences",rows);enriched.insert("SummaryVersion",1);enriched.insert("SummaryUpdatedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));a.saveDailyArticle(enriched);a.saveDailyArticle(old);QCOMPARE(a.dailyArticle(),enriched);b.mergeFrom(a.path());QCOMPARE(b.dailyArticle(),enriched);
        auto html=EntryRenderer::daily(enriched,offlineSettings());QTextDocument doc;doc.setHtml(html);QVERIFY(doc.toPlainText().contains(QStringLiteral("鸟会飞。")));QVERIFY(doc.toPlainText().contains("They migrate."));QVERIFY(!doc.toPlainText().contains(QStringLiteral("原文摘要")));
    }
    void standfirstIsNotNumberedSentence(){
        const auto now=QDateTime::currentDateTimeUtc();
        const auto xml=("<rss><channel><item><title>Test</title><description><![CDATA[<p>A short news teaser</p><p>Birds fly. They migrate.</p><p>Continue reading...</p>]]></description><link>https://example.org/news</link><pubDate>"+now.toString(Qt::RFC2822Date)+"</pubDate></item></channel></rss>").toUtf8();
        auto feed=DailyWord::parseFeed(xml,{},now);QCOMPARE(feed.size(),1);QCOMPARE(feed[0].toObject().value("SummaryBody").toString(),QString("Birds fly. They migrate."));QCOMPARE(feed[0].toObject().value("SummaryLead").toString(),QString("A short news teaser"));
        QTemporaryDir temp;Database db(temp.filePath("a.sqlite"),"daily");db.initialize();auto article=DailyWord::validate(response(),sources(),{},offlineSettings());article.insert("SummaryVersion",1);article.insert("SummarySentences",QJsonArray{QJsonObject{{"Text","A short news teaser"},{"Chinese","简短导语"}},QJsonObject{{"Text","Birds fly."},{"Chinese","鸟会飞。"}}});db.saveDailyArticle(article);
        auto upgraded=DailyWord::enrichSummary(db,article,offlineSettings(),std::make_shared<std::atomic_bool>(false));QCOMPARE(upgraded.value("SummaryVersion").toInt(),2);QCOMPARE(upgraded.value("SummarySentences").toArray().size(),1);QCOMPARE(upgraded.value("Source").toObject().value("SummaryLead").toString(),QString("A short news teaser"));
        QTextDocument rendered;rendered.setHtml(EntryRenderer::daily(upgraded,offlineSettings()));QVERIFY(rendered.toPlainText().simplified().contains("1 │ Birds fly."));QVERIFY(!rendered.toPlainText().contains("A short news teaser"));
    }
    void cacheLeaseAndSync(){
        QTemporaryDir temp;Database a(temp.filePath("a.sqlite"),"daily"),b(temp.filePath("b.sqlite"),"daily");a.initialize();b.initialize();auto article=DailyWord::validate(response(),sources(),{},offlineSettings());auto day=article.value("Date").toString();
        QVERIFY(a.beginDailyRequest(day,2));QVERIFY_THROWS_EXCEPTION(std::runtime_error,a.beginDailyRequest(day,2));a.endDailyRequest(day);QVERIFY(a.beginDailyRequest(day,2));a.endDailyRequest(day);QVERIFY_THROWS_EXCEPTION(std::runtime_error,a.beginDailyRequest(day,2));
        a.saveDailyArticle(article);QCOMPARE(a.dailyArticle(day),article);QVERIFY(!a.beginDailyRequest(day,2));QCOMPARE(a.dailyHistory().size(),1);QVERIFY(a.recentDailyWords().contains("resilience"));
        b.mergeFrom(a.path());QCOMPARE(b.dailyArticle(day),article);auto newer=article;newer.insert("CreatedAt",QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODateWithMs));newer.insert("Why","duplicate");b.saveDailyArticle(newer);QCOMPARE(b.dailyArticle(day),article);
        auto entry=article.value("Entry").toObject();auto complete=entry;complete.insert("LookupSource","DeepSeek");complete.insert("TranslationProvider","DeepSeek");complete.insert("Summary","完整释义");a.store(complete,false);a.saveFavorite(entry);QCOMPARE(a.findDeepSeek("resilience","en|zh-CN").value("Summary").toString(),QStringLiteral("完整释义"));QVERIFY(a.isFavorite("resilience","en|zh-CN"));a.setFavorite("resilience","en|zh-CN",false);QVERIFY(!a.find("resilience","en|zh-CN").isEmpty());QCOMPARE(a.dailyArticle(day),article);
        for(int i=1;i<=22;++i){auto old=article;old.insert("Date",QDate::fromString(day,Qt::ISODate).addDays(-i).toString(Qt::ISODate));a.saveDailyArticle(old);}QCOMPARE(a.dailyHistory(0).size(),20);QCOMPARE(a.dailyHistory(20).size(),3);
    }
    void mainWindowNavigation(){
        QTemporaryDir temp;Settings config(temp.path());config.data=offlineSettings();config.data.insert("username","daily-ui");auto sync=config.data.value("CacheSync").toObject();sync.insert("Enabled",false);config.data.insert("CacheSync",sync);auto ai=config.data.value("DeepSeek").toObject();ai.insert("Enabled",true);config.data.insert("DeepSeek",ai);config.save();
        Database db(config.databasePath(),config.username());db.initialize();auto article=DailyWord::validate(response(),sources(),{},config.data);db.saveDailyArticle(article);db.store(article.value("Entry").toObject(),false);
        MainWindow window(temp.path());window.show();QTRY_VERIFY(window.findChild<QLineEdit*>("queryInput")->isEnabled());window.resize(480,680);
        auto *tab=window.findChild<QPushButton*>("dailyTab");QVERIFY(tab);QTest::mouseClick(tab,Qt::LeftButton);
        auto *pages=window.findChild<QStackedWidget*>("pages");QCOMPARE(pages->currentIndex(),3);auto *daily=window.findChild<DailyPage*>();auto *browser=daily->findChild<EntryBrowser*>("dailyBrowser");QTRY_VERIFY(browser->toPlainText().contains("resilience"));
        QTRY_VERIFY(!daily->findChild<QLabel*>("dailyStatus")->isVisible());QTest::qWait(100);QTRY_VERIFY(daily->findChild<QPushButton*>("dailyRetry")->isEnabled());browser->verticalScrollBar()->setValue(100);int scroll=browser->verticalScrollBar()->value();QSignalSpy requests(&window,&MainWindow::onlineLookup);
        emit daily->lookupRequested("resilience");QCOMPARE(pages->currentIndex(),0);QTRY_VERIFY(window.findChild<EntryBrowser*>("entryBrowser")->toPlainText().contains("The ability to recover"));QCOMPARE(requests.count(),0);
        QTest::mouseClick(tab,Qt::LeftButton);QTRY_VERIFY(daily->findChild<QPushButton*>("dailyRetry")->isEnabled());QCOMPARE(browser->verticalScrollBar()->value(),scroll);
        window.grab().save(QCoreApplication::applicationDirPath()+"/../daily-main-window.png");
    }
    void liveCachedPreview(){
        const auto directory=qEnvironmentVariable("MYDICT_DAILY_PREVIEW_DIR");if(directory.isEmpty())QSKIP("Optional live preview");
        Settings config(directory);config.load();Database db(config.databasePath(),config.username());QVERIFY(!db.dailyArticle().isEmpty());
        DailyPage page;page.configure(db,config.data);page.resize(640,820);page.show();page.activate();auto *favorite=page.findChild<QPushButton*>("dailyRetry");QTRY_VERIFY_WITH_TIMEOUT(favorite->isEnabled(),30000);
        QTest::qWait(100);QTRY_VERIFY_WITH_TIMEOUT(favorite->isEnabled(),45000);page.grab().save(QCoreApplication::applicationDirPath()+"/../daily-live-page.png");
        auto *browser=page.findChild<EntryBrowser*>("dailyBrowser");browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());QTest::qWait(50);page.grab().save(QCoreApplication::applicationDirPath()+"/../daily-live-page-bottom.png");
    }
    void cachedPageAndFavorite(){
        QTemporaryDir temp;Database db(temp.filePath("a.sqlite"),"daily");db.initialize();auto settings=offlineSettings();auto article=DailyWord::validate(response(),sources(),{},settings);db.saveDailyArticle(article);
        DailyPage page;page.configure(db,settings);page.resize(480,680);page.show();page.activate();auto *browser=page.findChild<EntryBrowser*>("dailyBrowser");auto *favorite=page.findChild<QPushButton*>("dailyRetry");QTRY_VERIFY(browser->toPlainText().contains("resilience"));QTRY_VERIFY(favorite->isEnabled());
        QTRY_VERIFY(db.isFavorite("resilience","en|zh-CN"));
        QVERIFY(!page.findChild<QPushButton*>("dailyFavorite"));
        QVERIFY(!page.findChild<QLabel*>("dailyStatus")->isVisible());
        QVERIFY(!browser->toPlainText().contains(QStringLiteral("这里具体是什么意思")));
        QVERIFY(browser->toPlainText().contains(QStringLiteral("词形与派生")));
        page.grab().save(QCoreApplication::applicationDirPath()+"/../daily-page.png");
        QTRY_VERIFY(favorite->isEnabled());browser->verticalScrollBar()->setValue(60);int position=browser->verticalScrollBar()->value();page.activate();QTRY_VERIFY(favorite->isEnabled());QCOMPARE(browser->verticalScrollBar()->value(),position);
    }
};
QTEST_MAIN(DailyTest)
#include "daily_test.moc"
