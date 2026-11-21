#include "entryrenderer.h"
#include "core.h"
#include "appfonts.h"
#include <QFile>
#include <QLabel>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextFragment>
#include <QHash>
#include <QFontMetricsF>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QTextImageFormat>
#include <QUrl>
#include <QColor>
#include <QSet>
#include <cmath>

namespace MyDict {
static void flattenInto(QJsonObject &out, const QJsonObject &object) {
    for (auto i = object.begin(); i != object.end(); ++i) {
        if (i.key().startsWith('_')) continue;
        if (i.value().isObject()) flattenInto(out, i.value().toObject());
        else out.insert(i.key(), i.value());
    }
}
QJsonObject flattenStyles(const QJsonObject &settings) {
    static QJsonObject defaults = [] {
        QFile file(":/styles.defaults.json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }();
    QJsonObject result = defaults;
    flattenInto(result, settings.value("Styles").toObject());
    return result;
}
QString styleValue(const QJsonObject &settings, const QString &key, const QString &fallback) {
    auto value = flattenStyles(settings).value(key);
    return value.isUndefined() || value.isNull() ? fallback : value.toVariant().toString();
}
double styleNumber(const QJsonObject &settings, const QString &key, double fallback) {
    bool ok = false;
    double value = styleValue(settings, key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}
static QString escape(const QString &text) { return text.toHtmlEscaped(); }
static QString color(const QJsonObject &styles, const QString &key, const QString &fallback) {
    QColor c(styles.value(key).toVariant().toString());
    return c.isValid() ? c.name() : fallback;
}
class Renderer {
    QJsonObject settings, styles;
    QString self;
    double scale, leading;
public:
    explicit Renderer(QJsonObject s, QString word = {}) : settings(s), styles(flattenStyles(s)), self(word),
        scale(qBound(.65, styles.value("number.doc.scale").toVariant().toDouble(), 2.2)),
        leading(qBound(.65, styles.value("number.doc.leading_scale").toVariant().toDouble(), 2.2)) {
        if (!styles.contains("number.doc.scale")) scale = .9;
        if (!styles.contains("number.doc.leading_scale")) leading = .85;
    }
    QString font(const QString &key, const QString &fallback) const {
        return escape(AppFonts::resolve(styles.value(key).toString(fallback)));
    }
    QString clr(const QString &hex) const { return color(styles, "color.c" + hex.mid(1).toLower(), hex); }
    QString roleColor(const QString &role,const QString &fallback="#363636") const {
        static const QHash<QString,QString> defaults={{"subject","#2563A6"},{"predicate","#7445A5"},{"object","#087F72"},{"complement","#A36B24"},{"adverbial","#66758D"},{"modifier","#687368"},{"connector","#AB5268"}};
        return color(styles,"color.grammar."+role,defaults.value(role,fallback));
    }
    QString weight(const QString &key,const QString &fallback="Normal") const {
        QString value=styles.value(key).toVariant().toString();if(value.isEmpty())value=fallback;
        static const QHash<QString,QString> weights={{"thin","100"},{"extralight","200"},{"light","300"},{"normal","400"},{"regular","400"},{"medium","500"},{"semibold","600"},{"demibold","600"},{"bold","700"},{"extrabold","800"},{"black","900"}};
        return weights.value(value.toLower(),"400");
    }
    QString fontStyle(const QString &key,const QString &fallback="Normal") const {QString value=styles.value(key).toString(fallback).toLower();return value=="italic"||value=="oblique"?"italic":"normal";}
    QString words(const QString &text, const QString &c = "#363636") const {
        static const QRegularExpression re("[A-Za-z]+(?:['’\\-][A-Za-z]+)*");
        QString out;
        int offset = 0;
        auto matches = re.globalMatch(text);
        while (matches.hasNext()) {
            auto m = matches.next();
            out += escape(text.mid(offset, m.capturedStart() - offset));
            const QString word = m.captured();
            if (word.compare(self, Qt::CaseInsensitive) == 0) out += escape(word);
            else out += "<a style=\"color:" + c + ";text-decoration:none\" href=\"word:" + escape(QString::fromLatin1(QUrl::toPercentEncoding(QString(word).replace(QChar(0x2019), '\'')))) + "\">" + escape(word) + "</a>";
            offset = m.capturedEnd();
        }
        return out + escape(text.mid(offset));
    }
    QString sentence(const QString &text, const QJsonArray &segments, const QString &fallback) const {
        QString whole;
        static const QSet<QString> roles = {"subject", "predicate", "object", "complement", "adverbial", "modifier", "connector", "other"};
        bool valid = !segments.isEmpty();
        for (auto value : segments) {
            auto segment = value.toObject();
            whole += segment.value("Text").toString();
            valid = valid && segment.contains("Text") && roles.contains(segment.value("Role").toString("other"));
        }
        if (!valid || whole != text) return words(text, fallback);
        QString out;
        for (auto value : segments) {
            auto segment = value.toObject();
            QString role = segment.value("Role").toString("other"), pos = segment.value("Pos").toString();
            QString c = roleColor(role,fallback);
            QString css = "color:" + c + ";";
            css += "font-weight:"+weight(role=="predicate"?"weight.grammar.predicate":role=="subject"||role=="object"?"weight.grammar.core":"weight.doc.normal",role=="predicate"?"Bold":role=="subject"||role=="object"?"SemiBold":"Normal")+";";
            css += "font-style:"+fontStyle(role=="adverbial"||role=="modifier"||pos=="adjective"||pos=="adverb"?"fontstyle.doc.italic":"fontstyle.doc.normal",role=="adverbial"||role=="modifier"||pos=="adjective"||pos=="adverb"?"Italic":"Normal")+";";
            out += "<span style=\"" + css + "\">" + words(segment.value("Text").toString(), c) + "</span>";
        }
        return out;
    }
    QString paragraph(const QString &body, double size, const QString &c, bool serif = false, QString extra = {}, int margin = 2) const {
        double actualSize = size;
        if (styles.contains("number.doc.size." + QString::number(size))) actualSize = styles.value("number.doc.size." + QString::number(size)).toVariant().toDouble();
        return QString("<p style=\"margin:%1px 0px;line-height:%2%;font-size:%3px;font-family:'%4';color:%5;%6\">%7</p>")
            .arg(margin).arg(qRound(qMax(.8, styleNumber(settings,"number.doc.line_spacing",1.35) * leading) * 100)).arg(actualSize * scale).arg(serif ? font("font.doc.serif", "Georgia") : font("font.doc.body", "sans-serif")).arg(c, extra, body);
    }
    QString relation(QString label, const QString &text, QString c = "#0874C4") const {
        if (text.trimmed().isEmpty()) return {};
        return paragraph("<b>" + escape(label) + "</b>  " + words(text, c), 13, c, false, {}, 3);
    }
    QString forms(const QJsonObject &record, QString part) const {
        auto forms = record.value("Forms").toObject();
        part = part.toLower();
        if (part.startsWith("n.") || part == "noun") {
            const QString plural = forms.value("Plural").toString();
            return !plural.isEmpty() ? "复数 " + plural : forms.value("Uncountable").toBool() ? "不可数" : QString();
        }
        if (!part.startsWith("v.") && part != "verb") return {};
        QStringList result;
        for (auto pair : QList<QPair<QString,QString>>{{"ThirdPerson","三单"},{"PresentParticiple","现在分词"},{"Past","过去式"},{"PastParticiple","过去分词"}}) {
            QString value = forms.value(pair.first).toString();
            if (!value.isEmpty()) result << pair.second + " " + value;
        }
        return result.join("  ·  ");
    }
    static QString englishPart(QString part) {
        if (part.startsWith("adj.")) return "adjective";
        if (part.startsWith("adv.")) return "adverb";
        if (part.startsWith("n.")) return "noun";
        if (part.startsWith("v.")) return "verb";
        if (part.startsWith("pron.")) return "pronoun";
        if (part.startsWith("prep.")) return "preposition";
        if (part.startsWith("conj.")) return "conjunction";
        return part;
    }
    QString render(const QJsonObject &entry, const QString &selectedPart, int portion = 0, bool inlineParts = false) const {
        const auto record = entry.value("Record").toObject();
        const auto meanings = record.value("Meanings").toArray();
        QStringList parts;
        for (auto value : meanings) { QString part = value.toObject().value("PartOfSpeech").toString(); if (!parts.contains(part)) parts << part; }
        QString out = "<html><body style=\"margin:4px;\">";
        if (portion == 0 && !parts.isEmpty()) {
            out += "<table cellspacing=\"5\" cellpadding=\"6\"><tr>";
            for(const auto &part:parts+QStringList{"All"}) {

                const bool active=part==selectedPart||(part=="All"&&selectedPart.isEmpty());
                const auto label=part=="All"?part:englishPart(part);
                const auto href=escape(QString::fromLatin1(QUrl::toPercentEncoding(part=="All"?QString():part)));
                out += "<td bgcolor=\""+QString(active?"#4E8848":"#DCEED9")+"\">";
                out += "<a style=\"font-family:'"+font("font.mainwindow.window.1.fontfamily","Microsoft YaHei UI")+"';font-size:"+QString::number(18*scale)+"px;font-weight:900;text-decoration:none;color:"+(active?"#FFFFFF":"#315E35")+"\" href=\"pos:"+href+"\">&nbsp;"+escape(label)+"&nbsp;</a></td>";
            }
            out += "</tr></table>";
        }
        if (portion != 2) {
        out += paragraph("<b>" + escape(entry.value("Word").toString()) + "</b> <span style=\"font-size:" + QString::number(17 * scale) + "px;font-family:'" + font("font.doc.body", "sans-serif") + "';color:" + clr("#008577") + ";\">" + escape(shortTranslation(entry)) + "</span>", 34, clr("#0074CB"), true, {}, 3);
        QString phonetic = record.value("Phonetic").toString();
        QString phoneticLine = escape(phonetic);
        if (inlineParts) {
            QStringList headParts;
            for (const auto &value : meanings) {
                const auto part = value.toObject().value("PartOfSpeech").toString().trimmed();
                if (!part.isEmpty() && !headParts.contains(part)) headParts << part;
            }
            if (!headParts.isEmpty()) {
                if (!phoneticLine.isEmpty()) phoneticLine += "&nbsp;&nbsp;";
                phoneticLine += "<span style=\"font-family:'" + font("font.doc.serif", "Georgia")
                    + "';font-size:" + QString::number(14 * scale)
                    + "px;color:#B27D48;font-weight:bold;font-style:italic;\">"
                    + escape(headParts.join(" · ")) + "</span>";
            }
        }
        if (!phoneticLine.isEmpty()) out += paragraph(phoneticLine, 20, clr("#0874C4"), false, "font-family:'" + font("font.doc.phonetic", "sans-serif") + "';", 1);
        if (meanings.isEmpty()) out += paragraph(escape(entry.value("Translation").toString()), 19, clr("#008577"));
        if (!entry.value("DetailNote").toString().isEmpty()) out += paragraph(escape(entry.value("DetailNote").toString()), 12, "#777777");
        out += "<hr color=\""+clr("#A7D6A4")+"\" size=\"1\" />";
        }
        if (portion == 1) return out + "</body></html>";
        bool hasSegments = false;
        for (auto value : meanings) for (auto sense : value.toObject().value("Senses").toArray()) {
            if (!sense.toObject().value("DefinitionSegments").toArray().isEmpty()) hasSegments = true;
            for (auto example : sense.toObject().value("Examples").toArray()) if (!example.toObject().value("Segments").toArray().isEmpty()) hasSegments = true;
        }
        if(hasSegments){QStringList labels;for(auto item:QList<QPair<QString,QString>>{{"subject","主语"},{"predicate","谓语"},{"object","宾语"},{"complement","表语/补语"},{"adverbial","状语"},{"modifier","修饰语"}})labels<<"<span style=\"color:"+roleColor(item.first)+";font-weight:"+weight(item.first=="predicate"?"weight.grammar.predicate":"weight.doc.normal",item.first=="predicate"?"Bold":"Normal")+"\">"+item.second+"</span>";out+=paragraph(labels.join("　"),styleNumber(settings,"number.grammar.legend_size",11),"#777777");}
        for (const auto &part : parts) {
            if (!selectedPart.isEmpty() && selectedPart != part) continue;
            out += paragraph("<b><i>" + escape(englishPart(part)) + "</i></b>　<span style=\"font-family:'" + font("font.doc.body", "sans-serif") + "';font-size:" + QString::number(16 * scale) + "px;color:#666666\">" + words(forms(record, part), "#666666") + "</span>", 15, clr("#D88632"), true, {}, 6);
            QSet<QString> seen;
            int index = 0;
            QStringList synonyms, antonyms;
            for (auto value : meanings) {
                auto meaning = value.toObject();
                if (meaning.value("PartOfSpeech").toString() != part) continue;
                for (auto syn : meaning.value("Synonyms").toArray()) if (!synonyms.contains(syn.toString())) synonyms << syn.toString();
                for (auto ant : meaning.value("Antonyms").toArray()) if (!antonyms.contains(ant.toString())) antonyms << ant.toString();
                for (auto value : meaning.value("Senses").toArray()) {
                    auto sense = value.toObject();
                    QString definition = sense.value("Definition").toString();
                    if (seen.contains(definition)) continue;
                    seen.insert(definition);
                    out += paragraph("<span style=\"color:#D60032;font-weight:bold;font-style:italic\">" + QString::number(++index) + "　</span>" + sentence(definition, sense.value("DefinitionSegments").toArray(), clr("#363636")), 20, clr("#363636"), true, {}, 5);
                    QString chinese = sense.value("Chinese").toString();
                    out += paragraph("<span style=\"background-color:" + clr("#D7EAE6") + "\">" + escape(chinese.isEmpty() ? "中文释义待补充" : chinese) + "</span>", 17, clr("#008577"), false, "margin-left:22px;", 1);
                    if (!sense.value("UsageNote").toString().isEmpty()) out += relation("用法", sense.value("UsageNote").toString(), "#666666");
                    QStringList collocations;
                    for (auto collocation : sense.value("Collocations").toArray()) {
                        if (collocation.isString()) collocations << collocation.toString();
                        else { auto c = collocation.toObject(); collocations << c.value("text").toString() + "（" + c.value("chinese").toString() + "）"; }
                    }
                    if (!collocations.isEmpty()) out += relation("搭配", collocations.join(" · "));
                    if (settings.value("ShowExamples").toBool(true)) {
                        auto examples = sense.value("Examples").toArray();
                        if (examples.isEmpty() && !sense.value("Example").toString().isEmpty()) examples.append(QJsonObject{{"Text", sense.value("Example")}});
                        for (auto value : examples) {
                            auto example = value.toObject();
                            const auto marker = "<span style=\"color:#BADCF5;font-family:\'" + font("font.doc.body", "sans-serif") + "\';font-size:" + QString::number(14 * scale) + "px;font-style:normal;font-weight:400\">│ </span>";
                            out += paragraph(marker + sentence(example.value("Text").toString(), example.value("Segments").toArray(), "#707070"), 18, "#707070", true, "font-style:italic;margin-left:22px;", 2);
                            if (!example.value("Chinese").toString().isEmpty()) out += paragraph(marker + escape(example.value("Chinese").toString()), 14, clr("#008577"), false, "margin-left:22px;", 1);
                        }
                    }
                }
            }
            out += relation("同义词", synonyms.join(" · ")) + relation("反义词", antonyms.join(" · "), "#9C5666");
        }
        if (!record.value("Origin").toString().isEmpty()) out += relation("词源", record.value("Origin").toString(), "#707070");
        if (settings.value("ShowDerived").toBool(true) && !record.value("Derived").toString().isEmpty()) out += relation("派生与搭配", record.value("Derived").toString());
        QString notice = record.value("Notice").toString();
        if (!notice.isEmpty() && !notice.contains("DeepSeek AI 生成")) out += paragraph(escape(notice), 12, "#777777");
        if (record.isEmpty() && !entry.value("Details").toString().isEmpty()) out += paragraph(words(entry.value("Details").toString()).replace("\n", "<br />"), 14, "#666666");
        QStringList sources;
        for (auto source : record.value("Sources").toArray()) {
            QString text = source.toString(); QUrl url(text);
            if (url.scheme() == "https" || url.scheme() == "http") sources << "<a style=\"color:#777777\" href=\"" + escape(url.toString()) + "\">" + escape(text) + "</a>";
            else sources << escape(text);
        }
        if (!sources.isEmpty()) out += paragraph(sources.join("<br />"), 11, "#777777");
        return out + "</body></html>";
    }
};
QString EntryRenderer::render(const QJsonObject &entry, const QJsonObject &settings, const QString &selectedPart) { return Renderer(settings, entry.value("Word").toString()).render(entry, selectedPart); }
QString EntryRenderer::daily(const QJsonObject &article,const QJsonObject &settings){
    const auto word=article.value("Word").toString();Renderer r(settings,word);const auto source=article.value("Source").toObject();const auto entry=article.value("Entry").toObject();const auto record=entry.value("Record").toObject();
    QString out="<html><body style=\"margin:8px;\">";
    auto section=[&](const QString &title){return QString("<p style=\"margin:8px 0 3px 0;\"></p><table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr><td width=\"3\" bgcolor=\"#B9A8CE\"></td><td bgcolor=\"%1\" style=\"padding:5px 9px;\">%2</td></tr></table>").arg(escape(styleValue(settings,"color.daily.section_background","#F1EDF6")),r.paragraph(escape(title),15,"#62536F",false,"font-weight:600",0));};
    QUrl link(source.value("Url").toString());QString attribution=escape(source.value("Source").toString()+" · "+source.value("PublishedAt").toString().left(10));
    if(link.scheme()=="https")attribution+="　<a href=\""+escape(link.toString())+"\">阅读原文</a>";
    const auto attributionHtml=r.paragraph(attribution,12,"#82758D");
    auto fragment=[](QString html){int start=html.indexOf('>',html.indexOf("<body"))+1;return html.mid(start,html.lastIndexOf("</body>")-start);};
    auto heading=fragment(r.render(entry,{},1,true));
    heading.remove(QRegularExpression("<hr[^>]*>"));
    const auto imageData=article.value("ImageData").toString();
    if(!imageData.isEmpty()){
        const int imageWidth=qMax(1,article.value("ImageWidth").toInt(432)),imageHeight=qMax(1,article.value("ImageHeight").toInt(288));
        const auto displaySize=QSize(imageWidth,imageHeight).scaled(180,qRound(110*styleNumber(settings,"number.doc.scale",.9)),Qt::KeepAspectRatio);
        out+="<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr><td valign=\"top\">"+attributionHtml+heading+"</td><td width=\""+QString::number(displaySize.width()+8)+"\" align=\"right\" valign=\"top\"><img src=\"data:image/png;base64,"+escape(imageData)+"\" width=\""+QString::number(displaySize.width())+"\" height=\""+QString::number(displaySize.height())+"\" /></td></tr></table>";
    }else out+=attributionHtml+heading;
    out+="<hr color=\""+r.clr("#A7D6A4")+"\" size=\"1\" />";
    out+=section("为什么今天学这个词")+r.paragraph(escape(article.value("Why").toString()),15,"#47404F");
    out+=section("新闻语境");
    const auto title=source.value("Title").toString();QString highlighted;int offset=0;
    auto matches=QRegularExpression("(?<![A-Za-z])"+QRegularExpression::escape(word)+"(?![A-Za-z])",QRegularExpression::CaseInsensitiveOption).globalMatch(title);
    while(matches.hasNext()){auto match=matches.next();highlighted+=r.words(title.mid(offset,match.capturedStart()-offset),"#49698E")+"<b style=\"color:#59477D\">"+escape(match.captured())+"</b>";offset=match.capturedEnd();}
    highlighted+=r.words(title.mid(offset),"#49698E");
    out+=r.paragraph(highlighted,20,"#49698E",true,"font-style:italic",4);
    out+=r.paragraph(escape(article.value("QuoteChinese").toString()),14,"#777387");
    const auto sentences=article.value("SummarySentences").toArray();
    if(!sentences.isEmpty()){
        int sentenceNumber=0;
        for(auto value:sentences){auto sentence=value.toObject();
            const auto marker=QString("<span style=\"font-family:sans-serif;font-size:%1px;font-style:normal;font-weight:400;color:#B5A5C9\">│ </span>").arg(14*styleNumber(settings,"number.doc.scale",.9));
            out+="<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr><td width=\"20\" valign=\"top\">"+r.paragraph(QString::number(++sentenceNumber),14,"#9B87B3",false,{},4)+"</td><td>";
            out+=r.paragraph(marker+r.sentence(sentence.value("Text").toString(),sentence.value("Segments").toArray(),"#707070"),18,"#707070",true,"font-style:italic",4);
            out+=r.paragraph(marker+escape(sentence.value("Chinese").toString()),14,r.clr("#008577"),false,{},2)+"</td></tr></table>";
        }
    }else{
        auto original=source.value("Summary").toString();original.remove(QRegularExpression("Continue reading.*$",QRegularExpression::CaseInsensitiveOption));
        for(const auto &sentence:original.split(QRegularExpression("(?<=[.!?])\\s+"),Qt::SkipEmptyParts))out+=r.paragraph(r.words(sentence,"#707070"),16,"#707070",true,"font-style:italic;margin-left:14px",4);
    }
    out+=section("学会使用");
    auto learning=entry;auto learningRecord=record;
    for(auto key:{"Derived","Origin","Notice","Sources"})learningRecord.remove(key);
    learning.insert("Record",learningRecord);
    out+=fragment(r.render(learning,{},2));
    if(!article.value("Confusion").toString().isEmpty())out+=section("容易混淆的用法")+r.paragraph(escape(article.value("Confusion").toString()),14,"#47404F");
    out+=section("词形与派生");
    {
        for(auto meaning:record.value("Meanings").toArray()){const auto forms=r.forms(record,meaning.toObject().value("PartOfSpeech").toString());if(!forms.isEmpty())out+=r.paragraph(escape(forms),14,"#777387");}
        if(!record.value("Derived").toString().isEmpty())out+=r.paragraph(r.words(record.value("Derived").toString(),"#087F72"),14,"#087F72");
    }
    out+=r.paragraph("<a href=\"word:"+escape(QString::fromLatin1(QUrl::toPercentEncoding(word)))+"\">查看完整词典释义 →</a>",15,"#59477D",false,{},12);
    return out+"</body></html>";
}
QString EntryRenderer::reviewSentence(const QString &text, const QJsonArray &segments, const QJsonObject &settings) {
    Renderer r(settings);
    return r.paragraph(r.sentence(text, segments, "#363636"), 23, "#363636", true, "font-weight:400", 2);
}
QString EntryRenderer::message(const QString &title, const QString &body, const QJsonObject &settings) { Renderer r(settings); return "<html><body>" + r.paragraph(escape(title), 26, "#0874C4", true, "font-weight:bold", 8) + r.paragraph(escape(body), 14, "#777777") + "</body></html>"; }

EntryBrowser::EntryBrowser(QWidget *parent) : QTextBrowser(parent) {
    document()->setDocumentMargin(4);
    setOpenLinks(false); setOpenExternalLinks(false); setMouseTracking(true); viewport()->setMouseTracking(true);
    setTextInteractionFlags(Qt::TextBrowserInteraction);
    setFrameShape(QFrame::NoFrame);
}
void EntryBrowser::setHtml(const QString &html) {
    setExtraSelections({});
    QString content=html;
    delete partBar;partBar=nullptr;setViewportMargins(0,0,0,0);
    const auto table=QRegularExpression("<table cellspacing=\"5\" cellpadding=\"6\"><tr>(.*?)</tr></table>",QRegularExpression::DotMatchesEverythingOption).match(content);
    if(table.hasMatch()&&table.captured().contains("href=\"pos:")){
        partBar=new QWidget(this);partBar->setObjectName("partOfSpeechBar");
        auto *layout=new QHBoxLayout(partBar);layout->setContentsMargins(4,4,4,4);layout->setSpacing(5);
        auto links=QRegularExpression(R"rx(<td bgcolor="([^"]+)"><a style="([^"]+)" href="(pos:[^"]*)">(.*?)</a></td>)rx").globalMatch(table.captured());
        while(links.hasNext()){
            const auto match=links.next();auto *label=new QLabel(partBar);label->setObjectName("partOfSpeechLabel");
            QString text=match.captured(4);text.replace("&nbsp;"," ");label->setText(text.trimmed());label->setTextFormat(Qt::PlainText);
            label->setAlignment(Qt::AlignCenter);label->setCursor(Qt::PointingHandCursor);label->setProperty("partHref",match.captured(3));
            label->setStyleSheet("QLabel {"+match.captured(2)+";background-color:"+match.captured(1)+";border-radius:8px;padding:6px 10px;} QLabel:hover {background-color:"+QString(match.captured(1)=="#4E8848"?"#3D7038":"#C3E2BD")+";} ");
            label->installEventFilter(this);layout->addWidget(label);
        }
        layout->addStretch();const int height=layout->sizeHint().height();partBar->setGeometry(0,0,width(),height);partBar->show();setViewportMargins(0,height,0,0);
        content.remove(table.capturedStart(),table.capturedLength());
    }
    QTextBrowser::setHtml(content);
    struct Change {int position,length;QString style;};
    QList<Change> changes;QHash<QString,QString> styles;
    for(auto block=document()->begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){
        const auto fragment=it.fragment();if(!fragment.isValid())continue;
        const auto font=fragment.charFormat().font().resolve(document()->defaultFont());
        if(!font.bold()||!font.italic()||!font.styleName().isEmpty())continue;
        const auto key=font.families().join('\n')+":"+QString::number(font.weight())+":"+QString::number(font.stretch());
        if(!styles.contains(key))styles.insert(key,AppFonts::styledFont(font).styleName());
        if(!styles.value(key).isEmpty())changes.append({fragment.position(),fragment.length(),styles.value(key)});
    }
    QTextCursor cursor(document());cursor.beginEditBlock();
    for(const auto &change:changes){
        cursor.setPosition(change.position);cursor.setPosition(change.position+change.length,QTextCursor::KeepAnchor);
        QTextCharFormat format;format.setFontStyleName(change.style);cursor.mergeCharFormat(format);
    }
    cursor.endEditBlock();
}
void EntryBrowser::resizeEvent(QResizeEvent *event){
    QTextBrowser::resizeEvent(event);if(partBar)partBar->resize(width(),partBar->height());
}
bool EntryBrowser::eventFilter(QObject *object,QEvent *event){
    if(auto *label=qobject_cast<QLabel*>(object);label&&label->property("partHref").isValid()){
        if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){emit anchorClicked(QUrl(label->property("partHref").toString()));return true;}
    }
    return QTextBrowser::eventFilter(object,event);
}
void EntryBrowser::paintEvent(QPaintEvent *event) {
    QTextBrowser::paintEvent(event);
    // Drawing an overlay avoids Qt shaping runs extending underline to neighbouring words.
    QPainter painter(viewport());
    for(const auto &selection:extraSelections()) {
        auto cursor=selection.cursor;
        const int start=cursor.selectionStart(),end=cursor.selectionEnd();
        for(int i=start;i<end;++i){
            cursor.setPosition(i);const auto left=cursorRect(cursor);
            cursor.setPosition(i+1);const auto right=cursorRect(cursor);
            if(left.top()!=right.top())continue;
            cursor.setPosition(i);cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
            auto color=cursor.charFormat().foreground().color();if(!color.isValid())color=palette().color(QPalette::Text);
            painter.setPen(QPen(color,1));
            painter.drawLine(left.left(),left.bottom()-1,right.left(),left.bottom()-1);
        }
    }
}
void EntryBrowser::mouseMoveEvent(QMouseEvent *event) {
    // Both QTextBrowser and QTextEdit forward hover to QTextControl. Avoid its
    // native link formatting entirely; only delegate while dragging a selection.
    if (event->buttons()!=Qt::NoButton) {
        setExtraSelections({});QTextBrowser::mouseMoveEvent(event);return;
    }
    event->accept();
    QString anchor = anchorAt(event->pos());
    viewport()->setCursor(anchor.isEmpty()?Qt::IBeamCursor:Qt::PointingHandCursor);
    QList<QTextEdit::ExtraSelection> selections;
    if (!anchor.isEmpty()) {
        QTextCursor c = cursorForPosition(event->pos());
        int position = c.position();
        auto isAnchorAt = [this, &anchor](int pos) { QTextCursor p(document()); p.setPosition(pos); p.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor); return p.charFormat().anchorHref() == anchor; };
        if (position > 0 && !isAnchorAt(position)) --position;
        if (!isAnchorAt(position)) { setExtraSelections({}); return; }
        int start = position, end = position;
        while (start > 0 && isAnchorAt(start - 1)) --start;
        while (end < document()->characterCount() - 1 && isAnchorAt(end)) ++end;
        while(start<end && document()->characterAt(start).isSpace())++start;
        while(end>start && document()->characterAt(end-1).isSpace())--end;
        if (end > start) { c.setPosition(start); c.setPosition(end, QTextCursor::KeepAnchor); QTextEdit::ExtraSelection selection; selection.cursor = c; /* Keep layout formatting untouched: underline is painted separately. */ selections << selection; }
    }
    setExtraSelections(selections);
    viewport()->update();
}
void EntryBrowser::leaveEvent(QEvent *event) { setExtraSelections({}); viewport()->update(); QTextBrowser::leaveEvent(event); }
bool EntryBrowser::viewportEvent(QEvent *event) { if (event->type() == QEvent::ToolTip) return true; return QTextBrowser::viewportEvent(event); }
ReviewBrowser::ReviewBrowser(QWidget *parent):EntryBrowser(parent){
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet("background:transparent;border:0;");
    connect(document(),&QTextDocument::contentsChanged,this,[this]{fitHeight();});
}
void ReviewBrowser::fitHeight(){document()->setTextWidth(viewport()->width());const int height=qMax(62,int(document()->size().height()+8));if(height!=this->height())setFixedHeight(height);}
void ReviewBrowser::setText(const QString &value){
    const QString html=format==Qt::PlainText?"<p align=\"center\"><b>"+value.toHtmlEscaped()+"</b></p>":QString(value).replace("<p style=", "<p align=\"center\" style=");
    setHtml(html);fitHeight();
}
void ReviewBrowser::resizeEvent(QResizeEvent *event){EntryBrowser::resizeEvent(event);fitHeight();}

}
