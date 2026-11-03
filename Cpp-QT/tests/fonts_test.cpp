#include "appfonts.h"
#include <QtTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>

using MyDict::AppFonts;

namespace {
QString sourceDirectory(){
#ifdef FONT_SOURCE_DIR
    return QString::fromUtf8(FONT_SOURCE_DIR);
#else
    return QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("../fonts");
#endif
}
QStringList copyCaskaydia(const QString &directory){
    const QDir source(sourceDirectory());QStringList copied;
    const auto names=source.entryList({"CaskaydiaCoveNerdFontPropo-*.ttf"},QDir::Files,QDir::Name);
    for(const auto &name:names){const auto path=QDir(directory).filePath(name);if(QFile::copy(source.filePath(name),path))copied.append(QFileInfo(path).canonicalFilePath());}
    copied.sort(Qt::CaseInsensitive);return copied;
}
void put16(QByteArray &bytes,int at,quint16 value){if(bytes.size()<at+2)bytes.resize(at+2);bytes[at]=char(value>>8);bytes[at+1]=char(value);}
void put32(QByteArray &bytes,int at,quint32 value){put16(bytes,at,quint16(value>>16));put16(bytes,at+2,quint16(value));}
QByteArray unicode(QString text){QByteArray bytes;for(auto c:text){bytes.append(char(c.unicode()>>8));bytes.append(char(c.unicode()));}return bytes;}
struct Record {quint16 platform,encoding,language,id;QByteArray bytes;};
QByteArray nameTable(const QList<Record> &records,int format=0){
    QByteArray table(6+records.size()*12+(format==1?2:0),char(0)),strings;put16(table,0,quint16(format));put16(table,2,quint16(records.size()));put16(table,4,quint16(table.size()));
    for(int i=0;i<records.size();++i){const auto &r=records.at(i);const int at=6+i*12;
        put16(table,at,r.platform);put16(table,at+2,r.encoding);put16(table,at+4,r.language);put16(table,at+6,r.id);
        put16(table,at+8,quint16(r.bytes.size()));put16(table,at+10,quint16(strings.size()));strings+=r.bytes;}
    return table+strings;
}
QByteArray sfnt(QByteArray names,QByteArray signature=QByteArray::fromHex("00010000")){
    QByteArray bytes(32,char(0));bytes.replace(0,4,signature);put16(bytes,4,1);bytes.replace(12,4,"name");put32(bytes,20,32);put32(bytes,24,quint32(names.size()));return bytes+names;
}
void write(QString path,QByteArray bytes){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));}
QByteArray collection(QList<QList<Record>> alternatives={}){
    QByteArray bytes(2048,char(0));bytes.replace(0,4,"ttcf");put32(bytes,4,0x00020000);put32(bytes,8,2);put32(bytes,12,32);put32(bytes,16,64);
    const QList<QPair<int,int>> faces={{32,256},{64,1024}};int index=0;
    for(const auto &face:faces){auto names=nameTable(alternatives.isEmpty()?QList<Record>{{3,1,0x0409,16,unicode(index==0?"Collection Alpha":"Collection Beta")},
        {3,1,0x0804,1,unicode(index==0?QString::fromUtf8("集合字体甲"):QString::fromUtf8("集合字体乙"))}}:alternatives.at(index));
        put32(bytes,face.first,0x00010000);put16(bytes,face.first+4,1);bytes.replace(face.first+12,4,"name");
        put32(bytes,face.first+20,quint32(face.second));put32(bytes,face.first+24,quint32(names.size()));bytes.replace(face.second,names.size(),names);++index;}
    return bytes;
}
}

class FontTests:public QObject {
    Q_OBJECT
private slots:
    void cleanup(){AppFonts::apply({},{});}
    void metadataOnlyDoesNotRegisterUnusedFonts(){
        QTemporaryDir temp;const auto files=copyCaskaydia(temp.path());QCOMPARE(files.size(),4);
        auto names=nameTable({{3,1,0x0409,1,unicode("Never Selected")}});
        const auto unused=temp.filePath("unused-large.otf");write(unused,sfnt(names,"OTTO"));
        // A large sparse font proves discovery does not need the whole file in
        // memory. This metadata-only fixture is deliberately not a real face.
        {QFile file(unused);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.resize(64*1024*1024));}
        AppFonts::apply({}, {temp.path()});QVERIFY(AppFonts::loadedFiles().isEmpty());
        QVERIFY(AppFonts::availableFamilies().contains("Never Selected"));
        QVERIFY(AppFonts::availableFamilies().contains("CaskaydiaCove NFP"));
        QCOMPARE(AppFonts::resolve("Arial"),QString("Arial"));
    }
    void selectedFamilyLoadsItsFourFacesAndUnloadsOnChange(){
        QTemporaryDir temp;const auto files=copyCaskaydia(temp.path());QCOMPARE(files.size(),4);
        write(temp.filePath("unused.ttf"),sfnt(nameTable({{3,1,0x0409,1,unicode("Unused Family")}})));
        AppFonts::apply({"CaskaydiaCove NFP"},{temp.path()});QCOMPARE(AppFonts::loadedFiles(),files);
        QVERIFY(!AppFonts::resolve("CaskaydiaCove NFP").isEmpty());
        QCOMPARE(AppFonts::resolve("Unknown System Family"),QString("Unknown System Family"));
        AppFonts::apply({"CaskaydiaCove Nerd Font Propo"},{temp.path()});QCOMPARE(AppFonts::loadedFiles(),files);
        AppFonts::apply({"Arial"},{temp.path()});QVERIFY(AppFonts::loadedFiles().isEmpty());
        QCOMPARE(AppFonts::resolve("CaskaydiaCove NFP"),QString("CaskaydiaCove NFP"));
        AppFonts::apply({"CaskaydiaCove NFP"},{temp.path()});QCOMPARE(AppFonts::loadedFiles(),files);
    }
    void directoryPriorityAndDuplicateRoots(){
        QTemporaryDir first,second;const auto a=copyCaskaydia(first.path()),b=copyCaskaydia(second.path());QCOMPARE(a.size(),4);QCOMPARE(b.size(),4);
        AppFonts::apply({"CaskaydiaCove NFP"},{first.path(),first.path()+"/.",second.path()});QCOMPARE(AppFonts::loadedFiles(),a);
        AppFonts::apply({"CaskaydiaCove NFP"},{second.path(),first.path()});QCOMPARE(AppFonts::loadedFiles(),b);
    }
    void crossDirectoryAliasesKeepPriorityAndUnrelatedTtcFacesStaySeparate(){
        QTemporaryDir preferred,fallback;const auto files=copyCaskaydia(preferred.path());QCOMPARE(files.size(),4);
        const auto aliasFile=fallback.filePath("new-alias.ttf");
        write(aliasFile,sfnt(nameTable({{3,1,0x0409,1,unicode("Cross Directory Alias")},
            {3,1,0x0409,16,unicode("CaskaydiaCove Nerd Font Propo")}})));
        // Only the fallback metadata knows this alias. Its common ID 16 must
        // still select the earlier directory's four real faces, not that file.
        AppFonts::apply({"Cross Directory Alias"},{preferred.path(),fallback.path()});QCOMPARE(AppFonts::loadedFiles(),files);
        QCOMPARE(AppFonts::resolve("Cross Directory Alias"),AppFonts::resolve("CaskaydiaCove NFP"));
        AppFonts::apply({},{});QVERIFY(QFile::remove(aliasFile));
        write(fallback.filePath("unrelated-faces.ttc"),collection({
            {{3,1,0x0409,16,unicode("Only Metadata Family")}},
            {{3,1,0x0409,16,unicode("CaskaydiaCove Nerd Font Propo")},{3,1,0x0409,1,unicode("CaskaydiaCove NFP")}}}));
        // The metadata fixture is not a real loadable font. If its unrelated
        // second face accidentally joined the alias closure, the real Caskaydia
        // files above would be registered even though none was requested.
        AppFonts::apply({"Only Metadata Family"},{preferred.path(),fallback.path()});
        for(const auto &file:AppFonts::loadedFiles())QVERIFY(!files.contains(file));
        QCOMPARE(AppFonts::resolve("CaskaydiaCove NFP"),QString("CaskaydiaCove NFP"));
    }
    void unicodeNamesWwsOtfAndCollectionOffsets(){
        QTemporaryDir temp;
        write(temp.filePath("unicode.OTF"),sfnt(nameTable({{3,1,0x0409,1,unicode("Index Meta English")},
            {3,10,0x0804,16,unicode(QString::fromUtf8("中文索引字族"))},
            {0,4,0,21,unicode("Index NFP Alias")},
            {3,1,0x0409,4,unicode("Not A Family")}},1),"OTTO"));
        write(temp.filePath("collection.ttc"),collection());
        write(temp.filePath("roman.ttf"),sfnt(nameTable({{1,0,0,1,QByteArray("Caf")+QByteArray::fromHex("8e")+" Meta"}})));
        AppFonts::apply({}, {temp.path()});const auto names=AppFonts::availableFamilies();
        for(const auto &name:QStringList{"Index Meta English",QString::fromUtf8("中文索引字族"),"Index NFP Alias","Collection Alpha","Collection Beta",QString::fromUtf8("集合字体甲"),QString::fromUtf8("集合字体乙"),QString::fromUtf8("Café Meta")})QVERIFY2(names.contains(name),qPrintable(name));
        QVERIFY(!names.contains("Not A Family"));QVERIFY(AppFonts::loadedFiles().isEmpty());
    }
    void malformedFilesAreSkippedAndMetadataChangesAreObserved(){
        QTemporaryDir temp;
        write(temp.filePath("short.ttf"),"OTTO");
        auto invalid=sfnt(nameTable({{3,1,0x0409,1,unicode("Out Of Bounds")}}));put32(invalid,20,0xfffffff0);write(temp.filePath("bad-offset.ttf"),invalid);
        auto excessive=nameTable({{3,1,0x0409,1,unicode("Excessive Records")}});put16(excessive,2,65535);write(temp.filePath("bad-count.ttf"),sfnt(excessive));
        auto odd=nameTable({{3,1,0x0409,1,QByteArray("X")}});write(temp.filePath("bad-utf16.ttf"),sfnt(odd));
        const auto path=temp.filePath("editable.ttf");
        write(path,sfnt(nameTable({{3,1,0x0804,16,unicode(QString::fromUtf8("中文索引字族"))}})));
        AppFonts::apply({}, {temp.path()});QCOMPARE(AppFonts::availableFamilies(),QStringList{QString::fromUtf8("中文索引字族")});
        write(path,sfnt(nameTable({{3,1,0x0804,16,unicode(QString::fromUtf8("中文改变字族"))}})));
        AppFonts::apply({}, {temp.path()});QCOMPARE(AppFonts::availableFamilies(),QStringList{QString::fromUtf8("中文改变字族")});
        QVERIFY(AppFonts::loadedFiles().isEmpty());QVERIFY(QFile::remove(path));
        AppFonts::apply({}, {temp.path()});QVERIFY(AppFonts::availableFamilies().isEmpty());
    }
    void newlyAddedFacesAreRegisteredOnlyWhenTheirFamilyIsUsed(){
        QTemporaryDir source,destination;const auto all=copyCaskaydia(source.path());QCOMPARE(all.size(),4);
        const auto first=QDir(destination.path()).filePath(QFileInfo(all.at(0)).fileName());QVERIFY(QFile::copy(all.at(0),first));
        AppFonts::apply({"Arial"},{destination.path()});QVERIFY(AppFonts::loadedFiles().isEmpty());
        AppFonts::apply({"CaskaydiaCove NFP"},{destination.path()});QCOMPARE(AppFonts::loadedFiles().size(),1);
        const auto second=QDir(destination.path()).filePath(QFileInfo(all.at(1)).fileName());QVERIFY(QFile::copy(all.at(1),second));
        AppFonts::apply({"CaskaydiaCove NFP"},{destination.path()});QCOMPARE(AppFonts::loadedFiles().size(),2);
        AppFonts::apply({}, {destination.path()});QVERIFY(AppFonts::loadedFiles().isEmpty());
    }
};
QTEST_MAIN(FontTests)
#include "fonts_test.moc"
