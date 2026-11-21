#include "mainwindow.h"
#include "entryrenderer.h"
#include "appfonts.h"
#include "core.h"
#include <QtTest>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPainter>
#include <QRawFont>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextLayout>
#include <QWindow>
#include <QtMath>
#include <exception>
#include <windows.h>

using namespace MyDict;

namespace {
QString requestedPlatform;
const QString Nerd = QStringLiteral("CaskaydiaCove NFP");
const QString Noto = QStringLiteral("Noto Sans SC");

QString fontDirectory() {
#ifdef FONT_SOURCE_DIR
    return QString::fromUtf8(FONT_SOURCE_DIR);
#else
    return QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("../fonts");
#endif
}
QString sourceNameHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    const auto head=file.read(12);if(head.size()!=12)return {};
    const auto u16=[](const QByteArray &b,int at){return (quint32(quint8(b[at]))<<8)|quint8(b[at+1]);};
    const auto u32=[](const QByteArray &b,int at){return (quint32(quint8(b[at]))<<24)|(quint32(quint8(b[at+1]))<<16)|(quint32(quint8(b[at+2]))<<8)|quint8(b[at+3]);};
    const auto count=u16(head,4);if(count>256)return {};
    for(quint32 i=0;i<count;++i){if(!file.seek(12+i*16))return {};const auto table=file.read(16);if(table.size()!=16)return {};
        if(table.left(4)!="name")continue;const auto start=u32(table,8),length=u32(table,12);
        if(length>4*1024*1024||qint64(start)+length>file.size()||!file.seek(start))return {};
        const auto data=file.read(length);if(data.size()!=qint64(length))return {};
        return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
    }
    return {};
}
void putFont(Settings &settings,const QString &key,const QString &family) {
    auto styles=settings.data.value("Styles").toObject();bool found=false;
    if(styles.contains(key)){styles.insert(key,family);found=true;}
    else for(auto i=styles.begin();i!=styles.end();++i) {
        if(!i.value().isObject())continue;
        auto group=i.value().toObject();
        if(group.contains(key)){group.insert(key,family);styles.insert(i.key(),group);found=true;break;}
    }
    if(!found){auto group=styles.value("99_NativeFontQA").toObject();group.insert(key,family);styles.insert("99_NativeFontQA",group);}
    settings.data.insert("Styles",styles);
}
QJsonObject fixture() {
    const QString definition=QStringLiteral("To move quickly on foot.");
    const QString example=QStringLiteral("They run every day.");
    QJsonObject sense{{"Definition",definition},{"Chinese",QStringLiteral("用脚快速移动；奔跑。")},
        {"DefinitionSegments",QJsonArray{QJsonObject{{"Text","To move"},{"Role","predicate"}},QJsonObject{{"Text"," quickly"},{"Role","adverbial"}},QJsonObject{{"Text"," on foot."},{"Role","adverbial"}}}},
        {"Examples",QJsonArray{QJsonObject{{"Text",example},{"Chinese",QStringLiteral("他们每天跑步。")},
            {"Segments",QJsonArray{QJsonObject{{"Text","They"},{"Role","subject"}},QJsonObject{{"Text"," run"},{"Role","predicate"}},QJsonObject{{"Text"," every day."},{"Role","adverbial"}}}}}}},
        {"Collocations",QJsonArray{QStringLiteral("run a business（经营企业）")}}};
    return {{"Word","run"},{"Pair","en|zh-CN"},{"Summary",QStringLiteral("跑；经营")},{"Translation",QStringLiteral("奔跑")},
        {"LookupSource","DeepSeek"},{"LookupModel","native-font-fixture"},{"QueryEndpoint","https://example.invalid/chat/completions"},
        {"SuccessfulQueryAtUtc","2026-11-04T00:00:00.0000000Z"},
        {"Record",QJsonObject{{"Word","run"},{"Phonetic",QStringLiteral("/rʌn/　/ˈwɜːd/　/əˈbɪləti/")},
            {"Forms",QJsonObject{{"ThirdPerson","runs"},{"PresentParticiple","running"},{"Past","ran"},{"PastParticiple","run"}}},
            {"Derived",QStringLiteral("runner 跑步者；running 跑步")},
            {"Meanings",QJsonArray{QJsonObject{{"PartOfSpeech","verb"},{"Senses",QJsonArray{sense}}}}}}}};
}
QStringList registeredNerd() {
    QStringList result;
    for(const auto &file:AppFonts::loadedFiles())if(QFileInfo(file).fileName().startsWith("CaskaydiaCoveNerdFontPropo-",Qt::CaseInsensitive))result<<QFileInfo(file).canonicalFilePath();
    result.sort(Qt::CaseInsensitive);return result;
}
bool registeredNoto() {
    for(const auto &file:AppFonts::loadedFiles())if(QFileInfo(file).fileName()=="NotoSansSC-Variable.ttf")return true;
    return false;
}
QJsonObject shape(QString text,const QFont &font) {
    QTextLayout layout(text,font);layout.beginLayout();auto line=layout.createLine();if(line.isValid())line.setLineWidth(10000);layout.endLayout();
    QJsonArray runs;int missing=0,count=0;
    for(const auto &run:layout.glyphRuns()) {
        const auto raw=run.rawFont();
        for(auto glyph:run.glyphIndexes()){++count;if(!glyph)++missing;}
        runs.append(QJsonObject{{"family",raw.familyName()},{"style",raw.styleName()},{"weight",int(raw.weight())},
            {"pixelSize",raw.pixelSize()},{"glyphs",run.glyphIndexes().size()},{"variable",!raw.fontTable("fvar").isEmpty()},
            {"nameHash",QString::fromLatin1(QCryptographicHash::hash(raw.fontTable("name"),QCryptographicHash::Sha256).toHex())}});
    }
    return {{"text",text},{"requestedFamily",font.family()},{"glyphs",count},{"missingGlyphs",missing},
        {"valid",count>0&&missing==0},{"advance",line.isValid()?line.naturalTextWidth():0.},{"height",line.isValid()?line.height():0.},{"runs",runs}};
}
struct Raster {QImage image;double ink=0;QJsonObject details;};
Raster raster(QString text,QFont font,qreal dpr) {
    Raster result;result.details=shape(text,font);
    QTextLayout layout(text,font);layout.beginLayout();auto line=layout.createLine();if(line.isValid())line.setLineWidth(10000);layout.endLayout();
    const QSize logical(qMax(64,qCeil(line.naturalTextWidth())+24),qMax(52,qCeil(line.height())+16));
    result.image=QImage(QSize(qCeil(logical.width()*dpr),qCeil(logical.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
    result.image.setDevicePixelRatio(dpr);result.image.fill(Qt::white);
    {QPainter painter(&result.image);painter.setRenderHint(QPainter::TextAntialiasing);painter.setPen(Qt::black);layout.draw(&painter,QPointF(12,8));}
    for(int y=0;y<result.image.height();++y){const auto *pixels=reinterpret_cast<const QRgb*>(result.image.constScanLine(y));for(int x=0;x<result.image.width();++x)result.ink+=(765-qRed(pixels[x])-qGreen(pixels[x])-qBlue(pixels[x]))/765.;}
    result.details.insert("ink",result.ink);result.details.insert("rasterWidth",result.image.width());result.details.insert("rasterHeight",result.image.height());
    result.details.insert("rasterSha256",QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char*>(result.image.constBits()),int(result.image.sizeInBytes())),QCryptographicHash::Sha256).toHex()));
    return result;
}
}

class NativeFontsTest : public QObject {
    Q_OBJECT
    QString output;
    QJsonObject report;
    bool completed=false;
    QList<QPair<QString,Raster>> sheets;

    bool screenshot(MainWindow &window,const QString &name) {
        const auto image=window.grab();
        const qreal ratio=image.devicePixelRatio();
        if(qAbs(image.width()-window.width()*ratio)>1||qAbs(image.height()-window.height()*ratio)>1)return false;
        report.insert(name,QJsonObject{{"logicalWidth",window.width()},{"logicalHeight",window.height()},
            {"pixelWidth",image.width()},{"pixelHeight",image.height()},{"devicePixelRatio",ratio}});
        return image.save(QDir(output).filePath(name+".png"));
    }
    QFont documentFont(EntryBrowser *browser,const QString &text) {
        return browser->document()->find(text).charFormat().font();
    }
    bool visibleGlyphs(MainWindow &window,EntryBrowser *browser,const QString &name) {
        QJsonObject phase;
        phase.insert("ui",shape(QStringLiteral("拾词查询收藏释义 Wordbook 123"),window.font()));
        for(auto sample:QList<QPair<QString,QString>>{{"definition",QStringLiteral("To move quickly on foot.")},{"chinese",QStringLiteral("用脚快速移动；奔跑。")},{"phonetic",QStringLiteral("/rʌn/")},{"example",QStringLiteral("They run every day.")}}) {
            auto cursor=browser->document()->find(sample.second);if(cursor.isNull())return false;
            phase.insert(sample.first,shape(sample.second,cursor.charFormat().font()));
        }
        bool good=true;for(auto i=phase.begin();i!=phase.end();++i)good=good&&i.value().toObject().value("valid").toBool();
        report.insert(name+"-glyphs",phase);return good;
    }
    bool saveSheet() {
        qreal dpr=report.value("devicePixelRatio").toDouble(1.);
        int width=1000,height=20;
        for(const auto &item:sheets){width=qMax(width,qCeil(item.second.image.width()/dpr)+32);height+=qCeil(item.second.image.height()/dpr)+40;}
        QImage image(QSize(qCeil(width*dpr),qCeil(height*dpr)),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(dpr);image.fill(Qt::white);
        QPainter painter(&image);QFont label("Segoe UI");label.setPixelSize(14);painter.setFont(label);int y=16;
        for(const auto &item:sheets){painter.setPen(QColor("#493564"));painter.drawText(QPointF(16,y+14),item.first);y+=24;painter.drawImage(QPointF(8,y),item.second.image);y+=qCeil(item.second.image.height()/dpr)+16;}
        painter.end();return image.save(QDir(output).filePath("glyph-sheet.png"));
    }
private slots:
    void windowEdgesAndDeferredStartup() {try {
        QTemporaryDir temp;Settings config(temp.path());config.load();config.data.insert("username","window-edge-fixture");
        auto sync=config.data.value("CacheSync").toObject();sync.insert("Enabled",false);config.data.insert("CacheSync",sync);config.save();
        MainWindow window(temp.path());
        auto *input=window.findChild<QLineEdit*>("queryInput");QVERIFY(input);
        QVERIFY(!input->isEnabled());
        window.show();QVERIFY(window.isVisible());
        QVERIFY(!input->isEnabled());
        QTRY_VERIFY(input->isEnabled());
        const auto hwnd=reinterpret_cast<HWND>(window.winId());
        RECT r;QVERIFY(GetWindowRect(hwnd,&r));
        const int midX=(r.left+r.right)/2,midY=(r.top+r.bottom)/2;
        auto hit=[hwnd](int x,int y){return SendMessage(hwnd,WM_NCHITTEST,0,MAKELPARAM(x,y));};
        QCOMPARE(hit(r.left+1,midY),LRESULT(HTLEFT));
        QCOMPARE(hit(r.right-2,midY),LRESULT(HTRIGHT));
        QCOMPARE(hit(midX,r.top+1),LRESULT(HTTOP));
        QCOMPARE(hit(midX,r.bottom-2),LRESULT(HTBOTTOM));
        QCOMPARE(hit(r.left+1,r.top+1),LRESULT(HTTOPLEFT));
        QCOMPARE(hit(r.right-2,r.top+1),LRESULT(HTTOPRIGHT));
        QCOMPARE(hit(r.left+1,r.bottom-2),LRESULT(HTBOTTOMLEFT));
        QCOMPARE(hit(r.right-2,r.bottom-2),LRESULT(HTBOTTOMRIGHT));
        window.close();
    }catch(const std::exception &e){QFAIL(e.what());}}
    void initTestCase() {
        Q_INIT_RESOURCE(resources);
        QVERIFY2(QGuiApplication::platformName()=="windows","Run this QA with the real windows platform plugin, not offscreen/minimal.");
        output=qEnvironmentVariable("MYDICT_NATIVE_FONT_QA_OUTPUT_DIR");
        if(output.isEmpty()){auto tag=qEnvironmentVariable("QT_SCALE_FACTOR","native");tag.replace('.','_');output=QDir(QCoreApplication::applicationDirPath()).filePath("../native-fonts-"+tag);}
        output=QDir(output).absolutePath();QVERIFY(QDir().mkpath(output));
        report.insert("requestedPlatform",requestedPlatform);report.insert("platform",QGuiApplication::platformName());
        report.insert("scaleFactorEnvironment",qEnvironmentVariable("QT_SCALE_FACTOR"));report.insert("screenScaleFactorsEnvironment",qEnvironmentVariable("QT_SCREEN_SCALE_FACTORS"));
        report.insert("qtVersion",qVersion());report.insert("fontSource",fontDirectory());
    }
    void realWindowAndFaces() {try {
        QTemporaryDir temp;QVERIFY(temp.isValid());const QString directory=QDir(temp.path()).filePath("fonts");QVERIFY(QDir().mkpath(directory));
        const QDir source(fontDirectory());auto names=source.entryList({"CaskaydiaCoveNerdFontPropo-*.ttf"},QDir::Files,QDir::Name);QCOMPARE(names.size(),4);
        names<<"NotoSansSC-Variable.ttf";QStringList expected;
        for(const auto &name:names){const QString target=QDir(directory).filePath(name);QVERIFY2(QFile::copy(source.filePath(name),target),qPrintable(name));if(name.startsWith("Caskaydia"))expected<<QFileInfo(target).canonicalFilePath();}expected.sort(Qt::CaseInsensitive);
        Settings settings(temp.path());settings.load();settings.data.insert("username","native-font-fixture");
        auto ai=settings.data.value("DeepSeek").toObject();ai.insert("Enabled",true);ai.insert("ApiKey","");settings.data.insert("DeepSeek",ai);
        auto sync=settings.data.value("CacheSync").toObject();sync.insert("Enabled",false);settings.data.insert("CacheSync",sync);
        const QStringList keys{"font.mainwindow.window.1.fontfamily","font.doc.body","font.doc.serif","font.doc.phonetic"};
        const QStringList standards{"Segoe UI","Microsoft YaHei UI","Georgia","Segoe UI"};
        for(int i=0;i<keys.size();++i)putFont(settings,keys[i],standards[i]);settings.save();
        Database db(settings.databasePath(),settings.username());db.initialize();db.store(fixture(),false);
        MainWindow window(temp.path());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *input=window.findChild<QLineEdit*>("queryInput");auto *browser=window.findChild<EntryBrowser*>("entryBrowser");QVERIFY(input&&browser);QTRY_VERIFY(input->isEnabled());
        input->setText("run");QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("用脚快速移动")));
        const qreal dpr=window.devicePixelRatioF();QVERIFY(dpr>0);report.insert("devicePixelRatio",dpr);
        report.insert("logicalDpi",window.logicalDpiX());report.insert("screen",window.screen()->name());
        report.insert("standardDefinitionPixelSize",documentFont(browser,"To move quickly on foot.").pixelSize());
        QVERIFY(visibleGlyphs(window,browser,"standard"));QVERIFY(screenshot(window,"standard-window"));QVERIFY(registeredNerd().isEmpty());
        for(auto sample:QList<QPair<QString,QString>>{{"Standard UI + Chinese",QStringLiteral("拾词查询收藏释义 Wordbook 123")},{"Standard IPA",QStringLiteral("/rʌn/ /ˈwɜːd/ /əˈbɪləti/ ð θ ŋ ʃ ʒ æ ɑ ɔ ɜ ɪ ʊ ʌ ː")}}){QFont font=sample.first.contains("IPA")?QFont("Segoe UI"):window.font();font.setPixelSize(30);auto rendered=raster(sample.second,font,dpr);QVERIFY(rendered.details.value("valid").toBool());QVERIFY(rendered.ink>0);sheets.append({sample.first,rendered});}

        Settings edited(temp.path());edited.load();for(const auto &key:keys)putFont(edited,key,Nerd);edited.save();
        QTRY_COMPARE(registeredNerd(),expected);QTRY_VERIFY(window.font().family().contains("Caskaydia",Qt::CaseInsensitive));QTRY_VERIFY(browser->document()->toHtml().contains("Caskaydia"));
        QVERIFY(visibleGlyphs(window,browser,"nerd"));QVERIFY(screenshot(window,"nerd-window"));
        QJsonObject styleCatalog;
        for(const auto &family:QStringList{AppFonts::resolve(Nerd)}){
            QJsonArray styles;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            for(const auto &style:QFontDatabase::styles(family))styles.append(QJsonObject{{"name",style},{"weight",QFontDatabase::weight(family,style)},{"italic",QFontDatabase::italic(family,style)}});
#else
            QFontDatabase fonts;for(const auto &style:fonts.styles(family))styles.append(QJsonObject{{"name",style},{"weight",fonts.weight(family,style)},{"italic",fonts.italic(family,style)}});
#endif
            styleCatalog.insert(family,styles);
        }
        report.insert("nerdStyleCatalog",styleCatalog);
        QSet<QByteArray> faceTables,rasters;QJsonArray nerdReports;QStringList expectedFaceHashes;
        for(const auto &style:QStringList{"Regular","Bold","Italic","BoldItalic"}){auto hash=sourceNameHash(QDir(directory).filePath("CaskaydiaCoveNerdFontPropo-"+style+".ttf"));QVERIFY(!hash.isEmpty());expectedFaceHashes<<hash;}
        report.insert("expectedNerdFaceHashes",QJsonArray::fromStringList(expectedFaceHashes));
        for(int variant=0;variant<4;++variant){
            QFont font(AppFonts::resolve(Nerd));font.setPixelSize(32);font.setBold(variant&1);font.setItalic(variant&2);
            font=AppFonts::styledFont(font);
            auto raw=QRawFont::fromFont(font);QVERIFY(raw.isValid());QVERIFY(raw.familyName().contains("Caskaydia",Qt::CaseInsensitive));
            QVERIFY(!raw.fontTable("name").isEmpty());const auto nameHash=QCryptographicHash::hash(raw.fontTable("name"),QCryptographicHash::Sha256);faceTables.insert(nameHash);
            const QString name=variant==0?"Nerd Regular":variant==1?"Nerd Bold":variant==2?"Nerd Italic":"Nerd Bold Italic";
            auto rendered=raster(QStringLiteral("Aa Bb 0123456789 ffi office Quick fox /rʌn/"),font,dpr);
            QVERIFY(rendered.details.value("valid").toBool());QVERIFY(rendered.ink>0);rasters.insert(rendered.details.value("rasterSha256").toString().toLatin1());
            rendered.details.insert("requestedBold",font.bold());rendered.details.insert("requestedItalic",font.italic());rendered.details.insert("requestedStyleName",font.styleName());rendered.details.insert("rawFamily",raw.familyName());rendered.details.insert("rawStyle",raw.styleName());rendered.details.insert("rawWeight",int(raw.weight()));rendered.details.insert("nameHash",QString::fromLatin1(nameHash.toHex()));rendered.details.insert("rawItalic",raw.style()!=QFont::StyleNormal);
            nerdReports.append(rendered.details);sheets.append({name,rendered});
            if(variant==3){QJsonArray probes;for(const auto &style:QStringList{"Bold Italic","BoldItalic"}){QFont named=font;named.setStyleName(style);auto candidate=raster(QStringLiteral("Aa Bb 0123456789 ffi office Quick fox /rʌn/"),named,dpr);candidate.details.insert("requestedStyleName",style);probes.append(candidate.details);sheets.append({"Diagnostic styleName="+style,candidate});}report.insert("nerdBoldItalicStyleProbes",probes);}
        }
        report.insert("nerdFaces",nerdReports);
        // Independently exercise the product's HTML formatting path, rather
        // than letting a test-only QFont hint conceal a renderer regression.
        EntryBrowser rich;rich.resize(1000,400);QString html;QStringList richSamples;
        for(int variant=0;variant<4;++variant){QString sample=QString("Variant%1 Aa Bb 0123456789 ffi office").arg(variant);richSamples<<sample;html+=QString("<p style=\"font-family:'%1';font-size:32px;font-weight:%2;font-style:%3\"><a href=\"word:font\" style=\"color:#0874c4;text-decoration:none\">%4</a></p>").arg(AppFonts::resolve(Nerd)).arg(variant&1?700:400).arg(variant&2?"italic":"normal").arg(sample);}
        rich.setHtml(html);rich.show();QVERIFY(QTest::qWaitForWindowExposed(&rich));QJsonArray richReports;
        for(int variant=0;variant<4;++variant){auto cursor=rich.document()->find(richSamples[variant]);QVERIFY(!cursor.isNull());
            const auto format=cursor.charFormat();QVERIFY(format.isAnchor());QCOMPARE(format.anchorHref(),QString("word:font"));QCOMPARE(format.foreground().color(),QColor("#0874c4"));
            auto result=raster(richSamples[variant],format.font(),dpr);QVERIFY(result.details.value("valid").toBool());QVERIFY(result.ink>0);richReports.append(result.details);
        }
        report.insert("richBrowserFaces",richReports);QVERIFY(rich.grab().save(QDir(output).filePath("rich-browser-variants.png")));rich.close();
        QVERIFY(saveSheet());
        QCOMPARE(faceTables.size(),4);QCOMPARE(rasters.size(),4);
        for(int variant=0;variant<4;++variant){QCOMPARE(nerdReports[variant].toObject().value("nameHash").toString(),expectedFaceHashes[variant]);
            const auto runs=richReports[variant].toObject().value("runs").toArray();QVERIFY(!runs.isEmpty());for(auto run:runs)QCOMPARE(run.toObject().value("nameHash").toString(),expectedFaceHashes[variant]);}

        // Keep the static QFont weight identical for each request. Changing
        // only wght proves real variation interpolation rather than fake-bold
        // synthesis. The Thin control also detects a Normal request stuck Thin.
        putFont(edited,"font.mainwindow.window.1.fontfamily",Noto);putFont(edited,"font.doc.body",Noto);edited.save();
        QTRY_VERIFY(registeredNoto());QTRY_VERIFY(window.font().family().contains("Noto",Qt::CaseInsensitive));
        QVERIFY(visibleGlyphs(window,browser,"noto"));QVERIFY(screenshot(window,"noto-window"));
        QList<Raster> weights;QJsonArray notoReports;
        for(int weight:QList<int>{100,400,700}){
            QFont font(AppFonts::resolve(Noto));font.setPixelSize(36);font.setWeight(QFont::Normal);
#if QT_VERSION >= QT_VERSION_CHECK(6,7,0)
            font.setVariableAxis(QFont::Tag("wght"),float(weight));
#else
            QSKIP("Native variable-axis QA requires Qt 6.8 or later.");
#endif
            auto raw=QRawFont::fromFont(font);
            QVERIFY(raw.isValid());QVERIFY(raw.familyName().contains("Noto",Qt::CaseInsensitive));QVERIFY(!raw.fontTable("fvar").isEmpty());
            auto rendered=raster(QStringLiteral("拾词查询收藏释义 中文解释与例句 Aa 0123"),font,dpr);QVERIFY(rendered.details.value("valid").toBool());QVERIFY(rendered.ink>0);
            rendered.details.insert("wght",weight);rendered.details.insert("staticQFontWeight",int(font.weight()));rendered.details.insert("rawWeight",int(raw.weight()));rendered.details.insert("rawStyle",raw.styleName());
            weights.append(rendered);notoReports.append(rendered.details);sheets.append({QString("Noto variable wght=%1 (static weight Normal)").arg(weight),rendered});
        }
        report.insert("notoWeights",notoReports);QVERIFY(saveSheet());
        QVERIFY2(weights[1].ink>weights[0].ink*1.05,"Noto Normal raster is still Thin: the native backend did not apply the variable weight.");
        QVERIFY2(weights[2].ink>weights[1].ink*1.05,"Noto Bold raster is not heavier than Normal: the native backend did not apply the variable weight.");
        QVERIFY(weights[0].details.value("rasterSha256")!=weights[1].details.value("rasterSha256"));QVERIFY(weights[1].details.value("rasterSha256")!=weights[2].details.value("rasterSha256"));

        for(int i=0;i<keys.size();++i)putFont(edited,keys[i],standards[i]);edited.save();
        QTRY_VERIFY(registeredNerd().isEmpty());QTRY_VERIFY(!window.font().family().contains("Noto",Qt::CaseInsensitive));
        QVERIFY(visibleGlyphs(window,browser,"restored"));QVERIFY(screenshot(window,"restored-window"));
        QCOMPARE(documentFont(browser,"To move quickly on foot.").pixelSize(),report.value("standardDefinitionPixelSize").toInt());
        QVERIFY(window.close());QTRY_VERIFY(!window.isVisible());completed=true;
    } catch(const std::exception &error) {
        QFAIL(error.what());
    }}
    void cleanupTestCase() {
        report.insert("completed",completed);report.insert("registeredFilesAtEnd",QJsonArray::fromStringList(AppFonts::loadedFiles()));
        QFile file(QDir(output).filePath("report.json"));QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(QJsonDocument(report).toJson(QJsonDocument::Indented))>0);
        AppFonts::apply({},{});
    }
};

int main(int argc,char **argv) {
    requestedPlatform=qEnvironmentVariable("QT_QPA_PLATFORM");
    for(int i=1;i+1<argc;++i)if(QByteArray(argv[i])=="-platform")requestedPlatform=QString::fromLocal8Bit(argv[i+1]);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc,argv);app.setStyle("Fusion");NativeFontsTest test;return QTest::qExec(&test,argc,argv);
}
#include "native_fonts_test.moc"
