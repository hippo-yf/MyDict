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
#include <QTextBlock>
#include <QTextFragment>
#include <QStackedWidget>
#include <QComboBox>
#include <QSignalSpy>
#include <QImage>
#include <QUuid>
#include <exception>
#include <QScopeGuard>
#include <cmath>
#include <limits>
using namespace MyDict;

class UiTest : public QObject {
    Q_OBJECT
private:
    static void offlineSettings(Settings &settings,const QString &username,bool deepSeek) {
        settings.load();settings.data.insert("username",username);settings.data.insert("DelayMs",300);
        auto ai=settings.data.value("DeepSeek").toObject();ai.insert("Enabled",deepSeek);ai.insert("ApiKey","");settings.data.insert("DeepSeek",ai);
        auto sync=settings.data.value("CacheSync").toObject();sync.insert("Enabled",false);settings.data.insert("CacheSync",sync);settings.save();
    }
    QJsonObject sourceEntry(const QString &word,bool ai,const QString &definition,const QString &time) {
        auto result=entry();result.insert("Word",word);result.insert("Summary",ai?"AI brief":"Online brief");result.insert("Translation",ai?"AI translation":"Online translation");
        result.insert("LookupSource",ai?"DeepSeek":"MyMemory");result.insert("TranslationProvider",ai?"DeepSeek":"MyMemory");result.insert("LookupModel",ai?"fixture":"");result.insert("QueryEndpoint",ai?"https://example.invalid/chat/completions":"");result.insert("SuccessfulQueryAtUtc",time);
        auto record=result.value("Record").toObject();record.insert("Word",word);auto meanings=record.value("Meanings").toArray();auto meaning=meanings.first().toObject();auto senses=meaning.value("Senses").toArray();auto sense=senses.first().toObject();sense.insert("Definition",definition);sense.remove("DefinitionSegments");senses[0]=sense;meaning.insert("Senses",senses);meanings[0]=meaning;record.insert("Meanings",meanings);result.insert("Record",record);return result;
    }
    static QString definitionOf(const QJsonObject &entry) {
        auto meanings=entry.value("Record").toObject().value("Meanings").toArray();if(meanings.isEmpty())return {};
        auto senses=meanings.first().toObject().value("Senses").toArray();return senses.isEmpty()?QString():senses.first().toObject().value("Definition").toString();
    }
    static int historyCount(const QString &path) {
        const QString name="ui-history-"+QUuid::createUuid().toString(QUuid::WithoutBraces);int count=-1;
        {auto connection=QSqlDatabase::addDatabase("QSQLITE",name);connection.setDatabaseName(path);connection.setConnectOptions("QSQLITE_OPEN_READONLY");if(connection.open()){QSqlQuery query(connection);if(query.exec("SELECT COUNT(*) FROM history")&&query.next())count=query.value(0).toInt();}connection.close();}
        QSqlDatabase::removeDatabase(name);return count;
    }
    static QListWidgetItem *bookItem(QListWidget *list,qint64 id) {
        for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toLongLong()==id)return list->item(i);return nullptr;
    }
    static double reviewWeight(const QString &path) {
        const auto name = QUuid::createUuid().toString();
        double result = std::numeric_limits<double>::quiet_NaN();
        {
            auto connection = QSqlDatabase::addDatabase("QSQLITE", name);
            connection.setDatabaseName(path);
            if (connection.open()) {
                QSqlQuery query(connection);
                if (query.exec("SELECT COALESCE((SELECT log_weight FROM review_weights LIMIT 1),0)") && query.next())
                    result = std::exp(query.value(0).toDouble());
            }
            connection.close();
        }
        QSqlDatabase::removeDatabase(name);
        return result;
    }
    static bool hasIconColor(const QImage &image,bool blue) {
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto color=image.pixelColor(x,y);if(color.alpha()<200)continue;
            if(blue?(color.blue()>color.red()+30&&color.blue()>color.green()+30):(color.red()>225&&color.green()>225&&color.blue()>225))return true;}
        return false;
    }
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
    void favoriteReviewAnswersNextAndExit() {
        QTemporaryDir dir;
        Settings config(dir.path());
        offlineSettings(config, "review-ui", true);
        Database db(config.databasePath(), config.username());
        db.initialize();
        auto favoriteEntry = entry();
        auto record = favoriteEntry.value("Record").toObject();
        auto meanings = record.value("Meanings").toArray();
        auto meaning = meanings[0].toObject();
        auto senses = meaning.value("Senses").toArray();
        auto sense = senses[0].toObject();
        auto examples = sense.value("Examples").toArray();
        auto example = examples[0].toObject();
        example.insert("Segments", QJsonArray{
            QJsonObject{{"Text","They "},{"Role","subject"}},
            QJsonObject{{"Text","r"},{"Role","predicate"}},
            QJsonObject{{"Text","un"},{"Role","predicate"}},
            QJsonObject{{"Text"," every day."},{"Role","adverbial"}}});
        examples[0] = example; sense.insert("Examples", examples); senses[0] = sense;
        meaning.insert("Senses", senses); meanings[0] = meaning; record.insert("Meanings", meanings);
        favoriteEntry.insert("Record", record);
        db.saveFavorite(favoriteEntry);
        for (const auto &word : QStringList{"apple", "pear", "book"}) {
            auto other = entry();
            other.insert("Word", word);
            other.insert("Summary", word + QStringLiteral(" 的中文释义，包含较长的说明，用来验证窄窗口下多行文字完整显示，不能裁切最后一行。"));
            db.store(other);
        }
        MainWindow window(dir.path());
        window.show();
        auto *input = window.findChild<QLineEdit *>("queryInput");
        QTRY_VERIFY(input->isEnabled());
        window.resize(480,600);
        QTest::mouseClick(window.findChild<QPushButton *>("bookTab"), Qt::LeftButton);
        QTest::mouseClick(window.findChild<QPushButton *>("reviewStart"), Qt::LeftButton);
        auto *pages = window.findChild<QStackedWidget *>("pages");
        auto *unfamiliar = window.findChild<QPushButton *>("reviewUnfamiliar");
        QVERIFY(unfamiliar);
        QVERIFY(!window.findChild<QPushButton *>("reviewNext"));
        QVERIFY(!window.findChild<QLabel *>("reviewFeedback"));
        auto *word = window.findChild<QLabel *>("reviewWord");
        auto *first = window.findChild<QPushButton *>("reviewChoice0");
        QTRY_VERIFY(first->isVisible() && first->isEnabled());
        QCOMPARE(pages->currentIndex(), 2);
        QCOMPARE(word->text(), QString("run"));
        QCOMPARE(first->findChild<QLabel *>()->alignment(), Qt::Alignment(Qt::AlignCenter));
        QVERIFY(unfamiliar->isEnabled());
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-review-question.png");
        QPushButton *correct = nullptr, *wrong = nullptr;
        for (int i = 0; i < 4; ++i) {
            auto *choice = window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i));
            if (choice->findChild<QLabel *>()->text().contains(QStringLiteral("跑；经营"))) correct = choice;
            else wrong = choice;
        }
        QVERIFY(correct && wrong);
        QTest::mouseClick(wrong, Qt::LeftButton);
        QTRY_VERIFY(std::abs(reviewWeight(db.path()) - 1.3) < 1e-12);
        QVERIFY(correct->styleSheet().contains("#3F8D59"));
        QVERIFY(wrong->styleSheet().contains("#B94D64"));
        QVERIFY(!correct->isEnabled());
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-review-answer.png");
        // Even clicking a disabled answer advances after an incorrect answer.
        QTest::mouseClick(first, Qt::LeftButton);
        QTRY_VERIFY(first->isVisible() && first->isEnabled());
        QVERIFY(first->styleSheet().isEmpty());
        QVERIFY(std::abs(reviewWeight(db.path()) - 1.3) < 1e-12);
        for (int i = 0; i < 4; ++i) {
            auto *choice = window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i));
            if (choice->findChild<QLabel *>()->text().contains(QStringLiteral("跑；经营"))) correct = choice;
        }
        QTest::mouseClick(correct, Qt::LeftButton);
        QTRY_VERIFY(std::abs(reviewWeight(db.path()) - .91) < 1e-12);
        QTRY_VERIFY(first->isVisible() && first->isEnabled());

        // A page click during the pending write is honored after commit.
        {
            auto blocker = QSqlDatabase::addDatabase("QSQLITE", "review-write-lock");
            blocker.setDatabaseName(db.path());
            QVERIFY(blocker.open());
            QSqlQuery lock(blocker);
            QVERIFY(lock.exec("BEGIN IMMEDIATE"));
            QTest::mouseClick(unfamiliar, Qt::LeftButton);
            QVERIFY(!first->isEnabled());
            QTest::mouseClick(word, Qt::LeftButton);
            bool heartbeat = false;
            QTimer::singleShot(80, [&] { heartbeat = true; lock.exec("ROLLBACK"); });
            QTRY_VERIFY(heartbeat);
            QTRY_VERIFY(first->isVisible() && first->isEnabled());
            QTRY_VERIFY(std::abs(reviewWeight(db.path()) - 1.183) < 1e-12);
            blocker.close();
        }
        QSqlDatabase::removeDatabase("review-write-lock");
        QTest::mouseClick(unfamiliar, Qt::LeftButton);
        QTRY_VERIFY(std::abs(reviewWeight(db.path()) - 1.5379) < 1e-12);
        QVERIFY(!first->isEnabled());
        for (int i = 0; i < 4; ++i) {
            auto *choice = window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i));
            QVERIFY(!choice->styleSheet().contains("#B94D64"));
            if (choice->findChild<QLabel *>()->text().contains(QStringLiteral("跑；经营")))
                QVERIFY(choice->styleSheet().contains("#3F8D59"));
        }
        auto *mode = window.findChild<QComboBox *>("reviewMode");
        QVERIFY(mode);
        QTRY_VERIFY(mode->isEnabled());
        mode->setCurrentIndex(1);
        QTRY_VERIFY(first->isVisible() && first->isEnabled() && word->text().contains("____"));
        QTextDocument sentenceDocument;
        sentenceDocument.setHtml(word->text());
        QCOMPARE(sentenceDocument.toPlainText(), QString("They ____ every day."));
        QVERIFY(word->text().contains("#2563a6", Qt::CaseInsensitive));
        QVERIFY(word->text().contains("font-style:italic"));
        QCOMPARE(mode->itemText(1), QStringLiteral("选词填空"));
        QVERIFY(!word->text().contains("run", Qt::CaseInsensitive));
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-review-cloze.png");
        for (int i = 0; i < 4; ++i) {
            auto *choice = window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i));
            if (choice->findChild<QLabel *>()->text().endsWith("run")) correct = choice;
        }
        QTest::qWait(50);
        QList<QRect> optionRects;
        for (int i = 0; i < 4; ++i) optionRects.append(window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i))->geometry());
        QTest::mouseClick(correct, Qt::LeftButton);
        QTRY_VERIFY(std::abs(reviewWeight(db.path()) - 1.07653) < 1e-12);
        QVERIFY(!first->isEnabled());
        auto *translation = window.findChild<QLabel *>("reviewPrompt");
        QVERIFY(translation->isVisible());
        for (int i = 0; i < 4; ++i) {
            auto *option = window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i));
            for (auto *label : option->findChildren<QLabel *>()) {
                QVERIFY(label->height() >= label->heightForWidth(label->width()));
                QVERIFY(option->rect().contains(label->geometry()));
            }
        }
        QTest::qWait(50);
        for (int i = 0; i < 4; ++i) QCOMPARE(window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i))->geometry(), optionRects.at(i));
        QCOMPARE(translation->text(), QStringLiteral("他们每天跑步。"));
        for (int i = 0; i < 4; ++i) {
            const auto text = window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i))->findChild<QLabel *>()->text();
            QVERIFY(window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i))->findChild<QLabel *>("reviewChoiceMeaning")->isVisible());
            QVERIFY(!text.contains(QStringLiteral("正确")));
        }
        QTest::mouseClick(word, Qt::LeftButton);
        QTRY_VERIFY(first->isVisible() && first->isEnabled());
        QVERIFY(!translation->isVisible());
        QTest::mouseClick(unfamiliar, Qt::LeftButton);
        QTRY_VERIFY(std::abs(reviewWeight(db.path()) - 1.399489) < 1e-12);
        QVERIFY(!first->isEnabled());
        QStringList feedback;
        for (int i = 0; i < 4; ++i) {
            auto *choice = window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i));
            const auto text = choice->findChild<QLabel *>()->text();
            QVERIFY(window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i))->findChild<QLabel *>("reviewChoiceMeaning")->isVisible());
            const auto meaningText = choice->findChild<QLabel *>("reviewChoiceMeaning")->text();
            QVERIFY(meaningText.contains(QStringLiteral("跑；经营")) || meaningText.contains(QStringLiteral("的中文释义")));
            QVERIFY(!text.contains(QStringLiteral("正确")));
            feedback.append(text);
        }
        const auto savedSentence = word->text();
        QTest::mouseClick(window.findChild<QPushButton *>("lookupTab"), Qt::LeftButton);
        QCOMPARE(pages->currentIndex(), 0);
        input->setText("apple");
        QTest::qWait(800);
        QTest::mouseClick(window.findChild<QPushButton *>("bookTab"), Qt::LeftButton);
        QCOMPARE(pages->currentIndex(), 2);
        QCOMPARE(word->text(), savedSentence);
        QVERIFY(!first->isEnabled());
        QCOMPARE(mode->currentIndex(), 1);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(window.findChild<QPushButton *>(QString("reviewChoice%1").arg(i))->findChild<QLabel *>()->text(), feedback.at(i));
        QVERIFY(std::abs(reviewWeight(db.path()) - 1.399489) < 1e-12);
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-review-cloze-feedback.png");
        QTest::mouseClick(word, Qt::LeftButton);
        QTRY_VERIFY(first->isVisible() && first->isEnabled());
        QTest::mouseClick(window.findChild<QPushButton *>("reviewExit"), Qt::LeftButton);
        QCOMPARE(pages->currentIndex(), 1);
        QVERIFY(window.isVisible());
        QCOMPARE(db.page("", false, 0).rows.first().word, QString("run"));
    }

    void favoriteBookPagingAndNumbers() {
        QTemporaryDir dir; Settings config(dir.path()); offlineSettings(config,"book-pages",true);
        Database db(config.databasePath(),config.username());db.initialize();
        for(int i=0;i<53;++i){auto e=entry();e.insert("Word",QString("word%1").arg(i));db.saveFavorite(e);}
        auto cached=entry();cached.insert("Word","cache-only");db.store(cached);
        MainWindow window(dir.path());window.show();QTRY_VERIFY(window.findChild<QLineEdit*>("queryInput")->isEnabled());
        QTest::mouseClick(window.findChild<QPushButton*>("bookTab"),Qt::LeftButton);
        auto *list=window.findChild<QListWidget*>("wordbook");QTRY_COMPARE(list->count(),50);
        auto *jump=window.findChild<QSpinBox*>("bookPageJump");QVERIFY(jump);QCOMPARE(jump->maximum(),2);
        QCOMPARE(list->itemWidget(list->item(0))->findChild<QLabel*>("bookRowNumber")->text(),QString("1"));
        QCOMPARE(list->itemWidget(list->item(49))->findChild<QLabel*>("bookRowNumber")->text(),QString("50"));
        jump->setFocus();QTest::keyClick(jump,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(jump,"2");QTest::keyClick(jump,Qt::Key_Return);
        QTRY_COMPARE(list->count(),3);QCOMPARE(list->itemWidget(list->item(0))->findChild<QLabel*>("bookRowNumber")->text(),QString("1"));
        auto *save=list->itemWidget(list->item(0))->findChild<QPushButton*>("rowFavorite");QTest::mouseClick(save,Qt::LeftButton);
        QTRY_COMPARE(list->count(),2);QCOMPARE(db.page({},false,0).total,54);QCOMPARE(db.page({},true,0).total,52);
    }

    void emptyReviewCanExitWhileLoading() {
        QTemporaryDir dir;
        Settings config(dir.path());
        offlineSettings(config, "empty-review", false);
        MainWindow window(dir.path());
        window.show();
        QTRY_VERIFY(window.findChild<QLineEdit *>("queryInput")->isEnabled());
        QTest::mouseClick(window.findChild<QPushButton *>("bookTab"), Qt::LeftButton);
        auto *start = window.findChild<QPushButton *>("reviewStart");
        auto *exit = window.findChild<QPushButton *>("reviewExit");
        auto *pages = window.findChild<QStackedWidget *>("pages");
        QTest::mouseClick(start, Qt::LeftButton);
        QTest::mouseClick(exit, Qt::LeftButton);
        QTest::qWait(100);
        QCOMPARE(pages->currentIndex(), 1);
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QLabel *>("reviewWord")->text().contains(QStringLiteral("暂时无法出题")));
        QVERIFY(window.findChild<QLabel *>("notice")->text().contains(QStringLiteral("收藏")));
        QVERIFY(!window.findChild<QPushButton *>("reviewChoice0")->isVisible());
        QTest::mouseClick(exit, Qt::LeftButton);
        QCOMPARE(pages->currentIndex(), 1);
    }

    void roundedPartLabelsRemainClickable(){
        EntryBrowser browser;browser.resize(650,400);browser.setHtml(EntryRenderer::render(entry(),defaultSettings()));browser.show();
        QTextCursor label(browser.document());bool found=false;
        for(auto block=browser.document()->begin();block.isValid()&&!found;block=block.next())for(auto it=block.begin();!it.atEnd();++it){
            const auto fragment=it.fragment();if(fragment.isValid()&&fragment.charFormat().objectType()==QTextFormat::UserObject+1&&fragment.charFormat().anchorHref()=="pos:verb"){
                QCOMPARE(fragment.charFormat().fontWeight(),int(QFont::Black));
                label.setPosition(fragment.position());found=true;break;
            }
        }
        QVERIFY(found);QTest::qWait(20);QSignalSpy clicks(&browser,&QTextBrowser::anchorClicked);
        auto point=browser.cursorRect(label).center()+QPoint(10,0);QCOMPARE(browser.anchorAt(point),QString("pos:verb"));
        QTest::mouseClick(browser.viewport(),Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(clicks.count(),1);QCOMPARE(clicks.first().first().toUrl().toString(),QString("pos:verb"));
    }
    void compactWindowKeepsControlsVisible(){
        QTemporaryDir dir;QVERIFY(dir.isValid());Settings config(dir.path());offlineSettings(config,"compact-ui",true);
        Database db(config.databasePath(),config.username());db.initialize();db.store(entry());
        MainWindow window(dir.path());window.show();
        auto *input=window.findChild<QLineEdit*>("queryInput");QVERIFY(input);QTRY_VERIFY(input->isEnabled());
        QCOMPARE(window.minimumWidth(),480);window.resize(480,600);QTRY_COMPARE(window.width(),480);
        input->setText("run");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");QVERIFY(browser);
        QTRY_VERIFY(browser->toPlainText().contains("To move quickly"));
        const QStringList lookupControls{"lookupTab","bookTab","dailyTab","syncButton","about","minimize","maximize","close","backButton","forwardButton","queryInput","deepSeekButton","queryButton","favoriteButton","copyButton","entryBrowser"};
        for(const auto &name:lookupControls){
            auto *widget=window.findChild<QWidget*>(name);QVERIFY(widget);QVERIFY(widget->isVisible());
            QVERIFY2(window.rect().contains(QRect(widget->mapTo(&window,QPoint()),widget->size())),qPrintable(name));
        }
        QVERIFY(input->width()>=120);window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-compact-preview.png");
        QTest::mouseClick(window.findChild<QPushButton*>("bookTab"),Qt::LeftButton);
        const QStringList bookControls{"bookFilter","bookPageJump","exportButton","bookMore","wordbook"};
        for(const auto &name:bookControls){
            auto *widget=window.findChild<QWidget*>(name);QVERIFY(widget);QVERIFY(widget->isVisible());
            QVERIFY2(window.rect().contains(QRect(widget->mapTo(&window,QPoint()),widget->size())),qPrintable(name));
        }
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
        auto *saved=window.findChild<QPushButton*>("rowFavorite");QVERIFY(saved);QVERIFY(!window.findChild<QPushButton*>("rowRemove"));QVERIFY(!window.findChild<QWidget*>("favoritesOnly"));
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-book-preview.png");
        QTest::mouseClick(saved,Qt::LeftButton);QTRY_VERIFY(!db.isFavorite("run","en|zh-CN"));
        QTRY_COMPARE(list->count(),0);
        QVERIFY(!db.find("run","en|zh-CN").isEmpty());
        input->setText("run again");input->setText("run");QTest::qWait(900);QVERIFY(!db.find("run","en|zh-CN").isEmpty());
        // Identity hot reload switches to a separately locked profile and restores
        // its own history without copying the previous user's entries.
        Settings edited(dir.path());edited.load();edited.data.insert("username","another-user");
        Database another(edited.databasePath(),edited.username());another.initialize();auto nextEntry=entry();nextEntry.insert("Word","walk");nextEntry.insert("Summary",QStringLiteral("走路"));another.store(nextEntry);edited.save();
        QTRY_VERIFY(input->isEnabled()&&input->text()=="walk");QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("走路")));
        QVERIFY(db.find("walk","en|zh-CN").isEmpty());QVERIFY(!another.find("walk","en|zh-CN").isEmpty());
        window.grab().save(dir.path()+"/ui-test.png");window.close();QTRY_VERIFY(!window.isVisible());
    }
    void fontsFollowVisibleContentAndSettings() {
        try {
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
        }catch(const std::exception &error){QFAIL(error.what());}
    }
    void sourceCycleUsesOnlySelectedCacheWithoutRequests() {
        try {
        QTemporaryDir dir;QVERIFY(dir.isValid());Settings config(dir.path());offlineSettings(config,"ui-source-cycle",true);
        Database db(config.databasePath(),config.username());db.initialize();
        const QString aiText="DeepSeek cached definition for run.",onlineText="Online cached definition for run.",onlyAiText="DeepSeek exclusive cached meaning.";
        db.store(sourceEntry("run",false,onlineText,"2026-11-01T00:00:00.0000000Z"),false);
        db.store(sourceEntry("run",true,aiText,"2026-11-03T00:00:00.0000000Z"),false);
        db.store(sourceEntry("exclusive",true,onlyAiText,"2026-11-03T00:00:00.0000000Z"),false);
        MainWindow window(dir.path());QVERIFY(QObject::disconnect(&window,&MainWindow::onlineLookup,nullptr,nullptr));QSignalSpy requests(&window,&MainWindow::onlineLookup);QVERIFY(requests.isValid());window.show();
        auto *input=window.findChild<QLineEdit*>("queryInput");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");auto *source=window.findChild<QPushButton*>("deepSeekButton");auto *star=window.findChild<QPushButton*>("favoriteButton");auto *copy=window.findChild<QPushButton*>("copyButton");auto *notice=window.findChild<QLabel*>("notice");QVERIFY(input&&browser&&source&&star&&copy&&notice);
        QTRY_VERIFY(input->isEnabled());QVERIFY(!source->isCheckable());QCOMPARE(source->property("lookupSource").toString(),QString("DeepSeek"));QVERIFY(source->accessibleName().contains("DeepSeek"));
        input->setText("run");QTRY_VERIFY(browser->toPlainText().contains(aiText));QTRY_VERIFY(star->isEnabled());
        const int firstHistory=historyCount(config.databasePath());QVERIFY(firstHistory>=1);const auto whale=source->icon().pixmap(24,24).toImage();QVERIFY(hasIconColor(whale,true));
        QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(onlineText));QVERIFY(!browser->toPlainText().contains(aiText));
        QCOMPARE(source->property("lookupSource").toString(),QString("Online"));QVERIFY(source->accessibleName().contains(QStringLiteral("在线")));QVERIFY(!notice->isVisible());QCOMPARE(requests.count(),0);QCOMPARE(historyCount(config.databasePath()),firstHistory);
        Settings switched(dir.path());switched.load();QCOMPARE(switched.data.value("Provider").toString(),config.data.value("Provider").toString());
        const auto globe=source->icon().pixmap(24,24).toImage();QVERIFY(hasIconColor(globe,false));QVERIFY(globe.convertToFormat(QImage::Format_Alpha8)!=whale.convertToFormat(QImage::Format_Alpha8));
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-source-online.png");
        QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(aiText));QCOMPARE(source->property("lookupSource").toString(),QString("DeepSeek"));QCOMPARE(historyCount(config.databasePath()),firstHistory);QCOMPARE(requests.count(),0);QVERIFY(!notice->isVisible());
        input->setText("exclusive");QTRY_VERIFY(browser->toPlainText().contains(onlyAiText));const int onlyAiHistory=historyCount(config.databasePath());
        QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("暂无在线词典释义")));QCOMPARE(input->text(),QString("exclusive"));QVERIFY(!browser->toPlainText().contains(onlyAiText));QVERIFY(!star->isEnabled());QVERIFY(!copy->isEnabled());QVERIFY(!notice->isVisible());
        QTest::qWait(450);QCOMPARE(requests.count(),0);QCOMPARE(historyCount(config.databasePath()),onlyAiHistory);
        QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(onlyAiText));QCOMPARE(requests.count(),0);QVERIFY(!notice->isVisible());
        Settings saved(dir.path());saved.load();QCOMPARE(saved.data.value("Provider").toString(),config.data.value("Provider").toString());QVERIFY(saved.data.value("DeepSeek").toObject().value("Enabled").toBool());
        window.grab().save(QCoreApplication::applicationDirPath()+"/../ui-source-cycle.png");QVERIFY(window.close());QTRY_VERIFY(!window.isVisible());
        }catch(const std::exception &error){QFAIL(error.what());}
    }
    void historyAndWordbookRespectSelectedSource() {
        try {
        QTemporaryDir dir;QVERIFY(dir.isValid());Settings config(dir.path());offlineSettings(config,"ui-source-history",false);
        Database db(config.databasePath(),config.username());db.initialize();const QString onlineText="Online historical definition.",aiText="DeepSeek newer historical definition.",exclusiveText="DeepSeek exclusive historical meaning.";
        db.store(sourceEntry("run",false,onlineText,"2026-11-01T00:00:00.0000000Z"));db.store(sourceEntry("run",true,aiText,"2026-11-03T00:00:00.0000000Z"));db.store(sourceEntry("exclusive",true,exclusiveText,"2026-11-03T00:00:00.0000000Z"));db.setFavorite("run","en|zh-CN",true);db.setFavorite("exclusive","en|zh-CN",true);
        qint64 runId=0,exclusiveId=0;for(const auto &row:db.page({},false,0).rows){if(row.word=="run")runId=row.id;if(row.word=="exclusive")exclusiveId=row.id;}QVERIFY(runId&&exclusiveId);
        MainWindow window(dir.path());QVERIFY(QObject::disconnect(&window,&MainWindow::onlineLookup,nullptr,nullptr));QSignalSpy requests(&window,&MainWindow::onlineLookup);window.show();
        auto *input=window.findChild<QLineEdit*>("queryInput");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");auto *source=window.findChild<QPushButton*>("deepSeekButton");auto *back=window.findChild<QPushButton*>("backButton");auto *forward=window.findChild<QPushButton*>("forwardButton");auto *bookTab=window.findChild<QPushButton*>("bookTab");auto *list=window.findChild<QListWidget*>("wordbook");auto *star=window.findChild<QPushButton*>("favoriteButton");auto *copy=window.findChild<QPushButton*>("copyButton");QVERIFY(input&&browser&&source&&back&&forward&&bookTab&&list&&star&&copy);
        QTRY_VERIFY(input->isEnabled());QTRY_COMPARE(input->text(),QString("exclusive"));QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("暂无在线词典释义")));QVERIFY(!browser->toPlainText().contains(exclusiveText));QCOMPARE(source->property("lookupSource").toString(),QString("Online"));
        QTRY_VERIFY(back->isEnabled());QTest::mouseClick(back,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(onlineText));QCOMPARE(input->text(),QString("run"));QVERIFY(!browser->toPlainText().contains(aiText));
        QTRY_VERIFY(forward->isEnabled());QTest::mouseClick(forward,Qt::LeftButton);QTRY_COMPARE(input->text(),QString("exclusive"));QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("暂无在线词典释义")));QVERIFY(!star->isEnabled());QVERIFY(!copy->isEnabled());
        QTest::mouseClick(bookTab,Qt::LeftButton);QTRY_COMPARE(list->count(),2);auto *run=bookItem(list,runId);QVERIFY(run);QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(run).center());QTRY_VERIFY(browser->toPlainText().contains(onlineText));QCOMPARE(input->text(),QString("run"));QVERIFY(!browser->toPlainText().contains(aiText));
        QTest::mouseClick(bookTab,Qt::LeftButton);QTRY_COMPARE(list->count(),2);auto *exclusive=bookItem(list,exclusiveId);QVERIFY(exclusive);QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(exclusive).center());QTRY_COMPARE(input->text(),QString("exclusive"));QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("暂无在线词典释义")));QVERIFY(!browser->toPlainText().contains(exclusiveText));QVERIFY(!star->isEnabled());QVERIFY(!copy->isEnabled());
        QTest::qWait(450);QCOMPARE(requests.count(),0);QCOMPARE(source->property("lookupSource").toString(),QString("Online"));QVERIFY(window.close());QTRY_VERIFY(!window.isVisible());
        }catch(const std::exception &error){QFAIL(error.what());}
    }
    void explicitQueriesUseSelectedSourceAndIgnoreCanceledReplies() {
        try {
        QTemporaryDir dir;QVERIFY(dir.isValid());Settings config(dir.path());offlineSettings(config,"ui-source-requests",false);
        Database db(config.databasePath(),config.username());db.initialize();const QString originalOnline="Online original selected meaning.",newOnline="Online refreshed meaning remains selected.",aiText="DeepSeek globally newer cached meaning.",staleText="Canceled reply must never appear.";
        db.store(sourceEntry("run",false,originalOnline,"2026-11-01T00:00:00.0000000Z"),false);db.store(sourceEntry("run",true,aiText,"2026-11-03T00:00:00.0000000Z"),false);
        MainWindow window(dir.path());QVERIFY(QObject::disconnect(&window,&MainWindow::onlineLookup,nullptr,nullptr));QSignalSpy requests(&window,&MainWindow::onlineLookup);QVERIFY(requests.isValid());window.show();
        auto *input=window.findChild<QLineEdit*>("queryInput");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");auto *source=window.findChild<QPushButton*>("deepSeekButton");auto *query=window.findChild<QPushButton*>("queryButton");QVERIFY(input&&browser&&source&&query);QTRY_VERIFY(input->isEnabled());input->setText("run");QTRY_VERIFY(browser->toPlainText().contains(originalOnline));QCOMPARE(requests.count(),0);
        QTest::mouseClick(query,Qt::LeftButton);QTRY_COMPARE(requests.count(),1);QVERIFY(!requests.at(0).at(3).toBool());QCOMPARE(requests.at(0).at(1).toString(),QString("run"));
        const quint64 onlineRevision=requests.at(0).at(0).toULongLong();auto online=sourceEntry("run",false,newOnline,"2026-11-02T00:00:00.0000000Z");
        QVERIFY(QMetaObject::invokeMethod(&window,"receiveEntry",Qt::DirectConnection,Q_ARG(quint64,onlineRevision),Q_ARG(QJsonObject,online)));QTRY_VERIFY(browser->toPlainText().contains(newOnline));QVERIFY(!browser->toPlainText().contains(aiText));QVERIFY(isDeepSeek(db.find("run","en|zh-CN")));
        QTest::keyClick(input,Qt::Key_Return);QTRY_COMPARE(requests.count(),2);QVERIFY(!requests.at(1).at(3).toBool());const quint64 canceledOnline=requests.at(1).at(0).toULongLong();
        QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(aiText));QCOMPARE(requests.count(),2);
        auto stale=sourceEntry("run",false,staleText,"2026-11-05T00:00:00.0000000Z");QVERIFY(QMetaObject::invokeMethod(&window,"receiveEntry",Qt::DirectConnection,Q_ARG(quint64,canceledOnline),Q_ARG(QJsonObject,stale)));QTest::qWait(150);QVERIFY(browser->toPlainText().contains(aiText));QVERIFY(!browser->toPlainText().contains(staleText));QCOMPARE(definitionOf(db.findForSource("run","en|zh-CN",false)),newOnline);
        // New AI words avoid a refresh confirmation. Real worker connections
        // remain disconnected: we inspect dispatch and inject fixture replies.
        input->setText("fresh-ai");QTest::keyClick(input,Qt::Key_Return);QTRY_COMPARE(requests.count(),3);QVERIFY(requests.at(2).at(3).toBool());QCOMPARE(requests.at(2).at(1).toString(),QString("fresh-ai"));
        const quint64 aiRevision=requests.at(2).at(0).toULongLong();auto ai=sourceEntry("fresh-ai",true,"DeepSeek fresh selected meaning.","2026-11-04T00:00:00.0000000Z");QVERIFY(QMetaObject::invokeMethod(&window,"receiveEntry",Qt::DirectConnection,Q_ARG(quint64,aiRevision),Q_ARG(QJsonObject,ai)));QTRY_VERIFY(browser->toPlainText().contains("DeepSeek fresh selected meaning."));
        input->setText("another-ai");QTest::mouseClick(query,Qt::LeftButton);QTRY_COMPARE(requests.count(),4);QVERIFY(requests.at(3).at(3).toBool());QCOMPARE(requests.at(3).at(1).toString(),QString("another-ai"));const quint64 canceledAi=requests.at(3).at(0).toULongLong();
        QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("暂无在线词典释义")));QCOMPARE(input->text(),QString("another-ai"));QCOMPARE(requests.count(),4);
        auto lateAi=sourceEntry("another-ai",true,staleText,"2026-11-05T00:00:00.0000000Z");QVERIFY(QMetaObject::invokeMethod(&window,"receiveEntry",Qt::DirectConnection,Q_ARG(quint64,canceledAi),Q_ARG(QJsonObject,lateAi)));QTest::qWait(450);QVERIFY(!db.hasDeepSeek("another-ai","en|zh-CN"));QVERIFY(!browser->toPlainText().contains(staleText));QCOMPARE(requests.count(),4);
        QVERIFY(window.close());QTRY_VERIFY(!window.isVisible());
        }catch(const std::exception &error){QFAIL(error.what());}
    }
    void sourceSwitchKeepsFavoriteWritePendingUntilCommit() {
        try {
        QTemporaryDir dir;QVERIFY(dir.isValid());Settings config(dir.path());offlineSettings(config,"ui-source-favorite-race",true);
        Database db(config.databasePath(),config.username());db.initialize();const QString aiText="DeepSeek before the pending favorite.",onlineText="Online while the favorite write is pending.";
        db.store(sourceEntry("run",true,aiText,"2026-11-03T00:00:00.0000000Z"),false);db.store(sourceEntry("run",false,onlineText,"2026-11-01T00:00:00.0000000Z"),false);
        MainWindow window(dir.path());QVERIFY(QObject::disconnect(&window,&MainWindow::onlineLookup,nullptr,nullptr));QSignalSpy requests(&window,&MainWindow::onlineLookup);QVERIFY(requests.isValid());window.show();
        auto *input=window.findChild<QLineEdit*>("queryInput");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");auto *source=window.findChild<QPushButton*>("deepSeekButton");auto *star=window.findChild<QPushButton*>("favoriteButton");QVERIFY(input&&browser&&source&&star);
        QTRY_VERIFY(input->isEnabled());input->setText("run");QTRY_VERIFY(browser->toPlainText().contains(aiText));QTRY_VERIFY(star->isEnabled());QVERIFY(!db.isFavorite("run","en|zh-CN"));const int initialHistory=historyCount(config.databasePath());QVERIFY(initialHistory>=1);

        const QString lockName="ui-favorite-lock-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto blocker=QSqlDatabase::addDatabase("QSQLITE",lockName);bool locked=false;
        // Declared after the window, so every failure path releases the writer
        // lock before destroying the UI and its pending background write.
        const auto releaseLock=qScopeGuard([&]{if(locked){QSqlQuery rollback(blocker);rollback.exec("ROLLBACK");}blocker.close();blocker=QSqlDatabase();QSqlDatabase::removeDatabase(lockName);});
        blocker.setDatabaseName(config.databasePath());QVERIFY(blocker.open());
        {QSqlQuery begin(blocker);QVERIFY(begin.exec("BEGIN IMMEDIATE"));locked=true;}
        QTest::mouseClick(star,Qt::LeftButton);QVERIFY(!star->isEnabled());
        QTest::mouseClick(source,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().contains(onlineText));QCOMPARE(source->property("lookupSource").toString(),QString("Online"));
        bool responsive=false;QTimer::singleShot(20,[&]{responsive=true;});QTRY_VERIFY(responsive);
        // The read is legal under WAL but still returns the pre-commit value.
        // That old read must not re-enable or reset this word's pending star.
        QVERIFY(!db.isFavorite("run","en|zh-CN"));QVERIFY(!star->isEnabled());QCOMPARE(requests.count(),0);QCOMPARE(historyCount(config.databasePath()),initialHistory);
        {QSqlQuery rollback(blocker);QVERIFY(rollback.exec("ROLLBACK"));locked=false;}
        QTRY_VERIFY(db.isFavorite("run","en|zh-CN"));QTRY_VERIFY(star->isEnabled());QVERIFY(browser->toPlainText().contains(onlineText));
        // If the source switch installed its old false value after the commit,
        // this click would save true again instead of removing the favorite.
        QTest::mouseClick(star,Qt::LeftButton);QTRY_VERIFY(!db.isFavorite("run","en|zh-CN"));QTRY_VERIFY(star->isEnabled());
        QCOMPARE(requests.count(),0);QCOMPARE(historyCount(config.databasePath()),initialHistory);QCOMPARE(source->property("lookupSource").toString(),QString("Online"));QVERIFY(browser->toPlainText().contains(onlineText));
        QVERIFY(window.close());QTRY_VERIFY(!window.isVisible());
        }catch(const std::exception &error){QFAIL(error.what());}
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
