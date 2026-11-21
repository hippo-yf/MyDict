#include "appfonts.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QHash>
#include <QThread>
#include <algorithm>
#include <stdexcept>

namespace MyDict {
namespace {
constexpr qint64 MetadataBudget = 4 * 1024 * 1024;
constexpr int MaxFaces = 256;
constexpr int MaxTables = 256;
constexpr int MaxNames = 8192;

QString familyKey(QString name) { return name.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded(); }
QString pathKey(QString path) {
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}
quint16 u16(const QByteArray &data, int at) {
    return (quint16(quint8(data.at(at))) << 8) | quint8(data.at(at+1));
}
quint32 u32(const QByteArray &data, int at) {
    return (quint32(u16(data,at)) << 16) | u16(data,at+2);
}
struct Reader {
    QFile file;
    qint64 remaining = MetadataBudget;
    QCryptographicHash digest{QCryptographicHash::Sha256};
    explicit Reader(QString path):file(path){}
    QByteArray read(qint64 at,qint64 size) {
        if(at<0 || size<0 || size>remaining || at>file.size() || size>file.size()-at || !file.seek(at)) return {};
        auto bytes=file.read(size);
        if(bytes.size()!=size)return {};
        remaining-=size;digest.addData(bytes);return bytes;
    }
};
struct Name {QString text;int priority=0;};
struct Face {
    QSet<QString> aliases;
    QSet<QString> styles;
    QList<Name> names;
};
struct Font {
    QString path,key;
    int directory=0;
    qint64 size=0,modified=0;
    QByteArray digest;
    QList<Face> faces;
};
struct Loaded {int id=-1;Font font;QStringList actualFamilies;};
struct Binding {QString requested;QSet<QString> aliases;QStringList files;QList<Name> names;};
struct State {
    QList<Font> index;
    QHash<QString,Loaded> loaded;
    QHash<QString,QString> resolved;
    QStringList available;
};
State &state(){static State value;return value;}

QString decodeName(QByteArray bytes,int platform,int encoding) {
    QString result;
    if(platform==0 || (platform==3 && (encoding==0 || encoding==1 || encoding==10)) || (platform==2 && encoding==1)) {
        if(bytes.size()%2)return {};
        result.reserve(bytes.size()/2);
        for(int i=0;i<bytes.size();i+=2)result.append(QChar(u16(bytes,i)));
    } else if(platform==1 && encoding==0) {
        // Mac Roman names occur alongside the Unicode records in older SFNTs.
        static const ushort roman[128]={
            0x00c4,0x00c5,0x00c7,0x00c9,0x00d1,0x00d6,0x00dc,0x00e1,0x00e0,0x00e2,0x00e4,0x00e3,0x00e5,0x00e7,0x00e9,0x00e8,
            0x00ea,0x00eb,0x00ed,0x00ec,0x00ee,0x00ef,0x00f1,0x00f3,0x00f2,0x00f4,0x00f6,0x00f5,0x00fa,0x00f9,0x00fb,0x00fc,
            0x2020,0x00b0,0x00a2,0x00a3,0x00a7,0x2022,0x00b6,0x00df,0x00ae,0x00a9,0x2122,0x00b4,0x00a8,0x2260,0x00c6,0x00d8,
            0x221e,0x00b1,0x2264,0x2265,0x00a5,0x00b5,0x2202,0x2211,0x220f,0x03c0,0x222b,0x00aa,0x00ba,0x03a9,0x00e6,0x00f8,
            0x00bf,0x00a1,0x00ac,0x221a,0x0192,0x2248,0x2206,0x00ab,0x00bb,0x2026,0x00a0,0x00c0,0x00c3,0x00d5,0x0152,0x0153,
            0x2013,0x2014,0x201c,0x201d,0x2018,0x2019,0x00f7,0x25ca,0x00ff,0x0178,0x2044,0x20ac,0x2039,0x203a,0xfb01,0xfb02,
            0x2021,0x00b7,0x201a,0x201e,0x2030,0x00c2,0x00ca,0x00c1,0x00cb,0x00c8,0x00cd,0x00ce,0x00cf,0x00cc,0x00d3,0x00d4,
            0xf8ff,0x00d2,0x00da,0x00db,0x00d9,0x0131,0x02c6,0x02dc,0x00af,0x02d8,0x02d9,0x02da,0x00b8,0x02dd,0x02db,0x02c7};
        result.reserve(bytes.size());
        for(char c:bytes){auto ch=quint8(c);result.append(QChar(ch<128?ushort(ch):roman[ch-128]));}
    } else if(platform==2 && encoding==0) {
        for(char c:bytes)if(quint8(c)>=128)return {};
        result=QString::fromLatin1(bytes);
    } else return {}; // Unsupported legacy code pages are not guessed as UTF-8.
    while(result.endsWith(QChar(0)))result.chop(1);
    result=result.trimmed().normalized(QString::NormalizationForm_C);
    if(result.isEmpty() || result.size()>512)return {};
    for(int i=0;i<result.size();++i){
        const auto c=result.at(i);
        if(c.category()==QChar::Other_Control)return {};
        if(c.isHighSurrogate()){if(i+1>=result.size()||!result.at(++i).isLowSurrogate())return {};}
        else if(c.isLowSurrogate())return {};
    }
    return result;
}

Face readFace(Reader &reader,quint32 offset) {
    Face result;
    const auto head=reader.read(offset,12);if(head.size()!=12)return result;
    const auto signature=u32(head,0);
    if(signature!=0x00010000 && signature!=0x4f54544f && signature!=0x74727565 && signature!=0x74797031)return result;
    const auto count=u16(head,4);if(count==0||count>MaxTables)return result;
    const auto tables=reader.read(qint64(offset)+12,qint64(count)*16);if(tables.size()!=count*16)return result;
    qint64 tableOffset=-1,tableLength=0;
    for(int i=0;i<count;++i)if(tables.mid(i*16,4)=="name"){
        // In a TTC, table offsets remain absolute from the collection start.
        tableOffset=u32(tables,i*16+8);tableLength=u32(tables,i*16+12);break;
    }
    if(tableOffset<0||tableLength<6||tableOffset>reader.file.size()||tableLength>reader.file.size()-tableOffset)return result;
    const auto nameHead=reader.read(tableOffset,6);if(nameHead.size()!=6)return result;
    const auto format=u16(nameHead,0),names=u16(nameHead,2),strings=u16(nameHead,4);
    if(format>1||names>MaxNames||strings<6+names*12||strings>tableLength)return result;
    const auto records=reader.read(tableOffset+6,qint64(names)*12);if(records.size()!=names*12)return result;
    for(int i=0;i<names;++i){
        int at=i*12,platform=u16(records,at),encoding=u16(records,at+2),language=u16(records,at+4),id=u16(records,at+6);
        const bool familyName=id==1||id==16||id==21;
        const bool styleName=id==2||id==17||id==22;
        if(!familyName&&!styleName)continue;
        qint64 length=u16(records,at+8),relative=qint64(strings)+u16(records,at+10);
        if(length==0||relative>tableLength||length>tableLength-relative)continue;
        const auto bytes=reader.read(tableOffset+relative,length);if(bytes.size()!=length)continue;
        const auto name=decodeName(bytes,platform,encoding);if(name.isEmpty())continue;
        if(styleName){result.styles.insert(name);continue;}
        const auto key=familyKey(name);result.aliases.insert(key);
        // Typographic/WWS names describe the family shared by all weights.
        int priority=(id==16?0:id==21?1:2)*100;
        if(platform==3 && (language&0x3ff)==9)priority+=0;
        else if(platform==1 && language==0)priority+=1;
        else if(platform==0)priority+=2;
        else priority+=10;
        result.names.append({name,priority});
    }
    return result;
}

Font inspectFont(QString path,int directory) {
    Font info;info.path=path;info.key=pathKey(path);info.directory=directory;
    const QFileInfo fileInfo(path);info.size=fileInfo.size();info.modified=fileInfo.lastModified().toMSecsSinceEpoch();
    Reader reader(path);if(!reader.file.open(QIODevice::ReadOnly))return info;
    const auto header=reader.read(0,12);if(header.size()!=12)return info;
    if(header.left(4)=="ttcf"){
        const auto version=u32(header,4),count=u32(header,8);
        if((version!=0x00010000&&version!=0x00020000)||count==0||count>MaxFaces)return info;
        const auto offsets=reader.read(12,qint64(count)*4);if(offsets.size()!=qint64(count)*4)return info;
        QSet<quint32> seen;
        for(quint32 i=0;i<count;++i){auto offset=u32(offsets,int(i)*4);if(seen.contains(offset))continue;seen.insert(offset);
            auto face=readFace(reader,offset);if(!face.aliases.isEmpty())info.faces.append(face);}
    }else{
        auto face=readFace(reader,0);if(!face.aliases.isEmpty())info.faces.append(face);
    }
    info.digest=reader.digest.result();return info;
}

QList<Font> discover(const QStringList &directories) {
    QList<Font> result;QSet<QString> seenFiles,seenDirectories;int rank=0;
    for(const auto &directory:directories){
        auto root=QFileInfo(directory).canonicalFilePath();if(root.isEmpty()||!QFileInfo(root).isDir()||seenDirectories.contains(pathKey(root)))continue;
        seenDirectories.insert(pathKey(root));QStringList files;
        QDirIterator it(root,QDir::Files|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
        while(it.hasNext()){
            const auto path=it.next();const auto info=it.fileInfo();const auto extension=info.suffix().toLower();
            if(extension!="ttf"&&extension!="otf"&&extension!="ttc"&&extension!="otc")continue;
            const auto canonical=info.canonicalFilePath();if(!canonical.isEmpty())files.append(canonical);
        }
        std::sort(files.begin(),files.end(),[](const QString &a,const QString &b){int order=a.compare(b,Qt::CaseInsensitive);return order==0?a<b:order<0;});
        for(const auto &path:files){
            auto key=pathKey(path);if(seenFiles.contains(key))continue;seenFiles.insert(key);
            auto font=inspectFont(path,rank);if(!font.faces.isEmpty())result.append(font);
        }
        ++rank;
    }
    return result;
}
bool intersects(const QSet<QString> &left,const QSet<QString> &right) {
    for(const auto &key:left)if(right.contains(key))return true;return false;
}
bool sameFile(const Font &a,const Font &b){return a.size==b.size&&a.modified==b.modified&&a.digest==b.digest;}

Binding select(QString family,const QList<Font> &index) {
    Binding binding;binding.requested=familyKey(family);binding.aliases.insert(binding.requested);
    bool changed=true;
    // Alias closure links e.g. a short ID 1 NFP family name to the same full
    // typographic ID 16 name in the bold/italic files. Unrelated TTC faces do
    // not pull their other files into this selection.
    // First identify the logical family across all metadata. A newer fallback
    // copy may expose the requested short name while an earlier preferred copy
    // exposes only their common typographic name. Directory choice comes after
    // this discovery, so that alias spelling cannot reverse directory priority.
    while(changed){
        changed=false;
        for(const auto &font:index)for(const auto &face:font.faces)if(intersects(face.aliases,binding.aliases)){
            const auto before=binding.aliases.size();binding.aliases.unite(face.aliases);changed|=before!=binding.aliases.size();
        }
    }
    int rank=-1;QSet<QString> files;
    for(const auto &font:index){
        if(rank>=0&&font.directory!=rank)continue;
        for(const auto &face:font.faces)if(intersects(face.aliases,binding.aliases)){
            if(rank<0)rank=font.directory;
            if(!files.contains(font.key)){files.insert(font.key);binding.files.append(font.key);}
            binding.names.append(face.names);
        }
    }
    return binding;
}
QString actualFamily(const Binding &binding,const State &s) {
    QStringList actual;
    for(const auto &file:binding.files)if(s.loaded.contains(file)){
        const auto families=s.loaded.value(file).actualFamilies;
        for(const auto &name:families)if(binding.aliases.contains(familyKey(name))&&!actual.contains(name))actual.append(name);
    }
    for(const auto &name:actual)if(familyKey(name)==binding.requested)return name;
    auto names=binding.names;
    std::stable_sort(names.begin(),names.end(),[](const Name &a,const Name &b){return a.priority<b.priority;});
    for(const auto &candidate:names)for(const auto &name:actual)if(familyKey(name)==familyKey(candidate.text))return name;
    return actual.isEmpty()?QString():actual.first();
}
}

void AppFonts::apply(const QSet<QString> &requiredFamilies,const QStringList &fontDirectories) {
    auto app=qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if(!app||QThread::currentThread()!=app->thread())throw std::runtime_error("Application fonts must be updated on the GUI thread.");
    auto &s=state();s.index=discover(fontDirectories);s.available.clear();QSet<QString> available;
    QHash<QString,Font> indexed;
    for(const auto &font:s.index){indexed.insert(font.key,font);for(const auto &face:font.faces)for(const auto &name:face.names){
        const auto key=familyKey(name.text);if(!available.contains(key)){available.insert(key);s.available.append(name.text);}}}
    s.available.sort(Qt::CaseInsensitive);
    QList<Binding> bindings;QSet<QString> selected;
    QStringList required=requiredFamilies.values();required.sort(Qt::CaseInsensitive);
    for(const auto &family:required)if(!family.trimmed().isEmpty()){
        auto binding=select(family,s.index);for(const auto &key:binding.files)selected.insert(key);bindings.append(binding);
    }
    // Unregister only files whose families are no longer used, whose priority
    // changed, or whose on-disk metadata/mtime/size changed.
    for(auto it=s.loaded.begin();it!=s.loaded.end();){
        if(!selected.contains(it.key())||!indexed.contains(it.key())||!sameFile(it.value().font,indexed.value(it.key()))){
            QFontDatabase::removeApplicationFont(it.value().id);it=s.loaded.erase(it);
        }else ++it;
    }
    for(const auto &font:s.index)if(selected.contains(font.key)&&!s.loaded.contains(font.key)){
        const int id=QFontDatabase::addApplicationFont(font.path);
        if(id>=0){
            // This is the per-registration name list, not QFontDatabase::families()
            // or a system-font scan. Discovery already matched the SFNT aliases.
            s.loaded.insert(font.key,Loaded{id,font,QFontDatabase::applicationFontFamilies(id)});
        }
    }
    s.resolved.clear();
    for(const auto &binding:bindings){
        const auto actual=actualFamily(binding,s);if(actual.isEmpty())continue;
        for(const auto &alias:binding.aliases)if(!s.resolved.contains(alias))s.resolved.insert(alias,actual);
        s.resolved.insert(binding.requested,actual);
    }
}
QString AppFonts::resolve(QString family){return state().resolved.value(familyKey(family),family);}
QFont AppFonts::styledFont(QFont font) {
    if(!font.bold()||!font.italic()||!font.styleName().isEmpty())return font;
    QSet<QString> localStyles;
    for(const auto &loaded:state().loaded)for(const auto &face:loaded.font.faces)
        if(face.aliases.contains(familyKey(font.family())))localStyles.unite(face.styles);
    if(localStyles.isEmpty())return font;
    // Query only the actual family being displayed. Installed older versions
    // can advertise e.g. Semibold Italic as weight 700, so prefer an exact style
    // supplied by the selected local files rather than a same-weight impostor.
    const auto family=QFontInfo(font).family();
    QString best;int bestScore=1000000;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    const auto styles=QFontDatabase::styles(family);
#else
    const QFontDatabase database;
    const auto styles=database.styles(family);
#endif
    for(const auto &style:styles){
        if(!localStyles.contains(style))continue;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        const int weight=QFontDatabase::weight(family,style);
        if(!QFontDatabase::italic(family,style)||weight<=QFont::Medium)continue;
        const auto candidate=QFontDatabase::font(family,style,12);
#else
        const int weight=database.weight(family,style);
        if(!database.italic(family,style)||weight<=QFont::Medium)continue;
        const auto candidate=database.font(family,style,12);
#endif
        const int score=qAbs(weight-int(font.weight()))*1000+qAbs(candidate.stretch()-font.stretch());
        if(score<bestScore){best=style;bestScore=score;}
    }
    if(!best.isEmpty())font.setStyleName(best);
    return font;
}
QStringList AppFonts::loadedFiles(){QStringList files;for(const auto &loaded:state().loaded)files.append(loaded.font.path);files.sort(Qt::CaseInsensitive);return files;}
QStringList AppFonts::availableFamilies(){return state().available;}

}
