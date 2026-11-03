#include "entryrenderer.h"
#include "core.h"
#include "appfonts.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextFragment>
#include <QHash>
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
    QString render(const QJsonObject &entry, const QString &selectedPart) const {
        const auto record = entry.value("Record").toObject();
        const auto meanings = record.value("Meanings").toArray();
        QStringList parts;
        for (auto value : meanings) { QString part = value.toObject().value("PartOfSpeech").toString(); if (!parts.contains(part)) parts << part; }
        QString out = "<html><body style=\"margin:4px;\">";
        if (!parts.isEmpty()) {
            out += "<table cellspacing=\"0\" cellpadding=\"5\" bgcolor=\"" + clr("#E2F1E0") + "\"><tr>";
            for (const auto &part : parts + QStringList{"All"}) {
                const bool active = part == selectedPart || (part == "All" && selectedPart.isEmpty());
                out += "<td bgcolor=\"" + QString(active ? "#4E8848" : "#A9D6A5") + "\"><a href=\"pos:" + escape(QString::fromLatin1(QUrl::toPercentEncoding(part == "All" ? QString() : part))) + "\" style=\"font-size:" + QString::number(14 * scale) + "px;color:" + (active ? "#FFFFFF" : "#315E35") + ";text-decoration:none;font-weight:bold\">" + escape(part == "All" ? part : englishPart(part)) + "</a></td><td width=\"3\"></td>";
            }
            out += "</tr></table>";
        }
        out += paragraph("<b>" + escape(entry.value("Word").toString()) + "</b> <span style=\"font-size:" + QString::number(17 * scale) + "px;font-family:'" + font("font.doc.body", "sans-serif") + "';color:" + clr("#008577") + ";\">" + escape(shortTranslation(entry)) + "</span>", 34, clr("#0074CB"), true, {}, 3);
        QString phonetic = record.value("Phonetic").toString();
        if (!phonetic.isEmpty()) out += paragraph(escape(phonetic), 20, clr("#0874C4"), false, "font-family:'" + font("font.doc.phonetic", "sans-serif") + "';", 1);
        if (meanings.isEmpty()) out += paragraph(escape(entry.value("Translation").toString()), 19, clr("#008577"));
        if (!entry.value("DetailNote").toString().isEmpty()) out += paragraph(escape(entry.value("DetailNote").toString()), 12, "#777777");
        out += "<hr color=\""+clr("#A7D6A4")+"\" size=\"1\" />";
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
                            out += paragraph("<span style=\"color:#BADCF5\">│ </span>" + sentence(example.value("Text").toString(), example.value("Segments").toArray(), "#707070"), 18, "#707070", true, "font-style:italic;margin-left:22px;", 2);
                            if (!example.value("Chinese").toString().isEmpty()) out += paragraph("<span style=\"color:#BADCF5\">│ </span>" + escape(example.value("Chinese").toString()), 14, clr("#008577"), false, "margin-left:22px;", 1);
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
QString EntryRenderer::message(const QString &title, const QString &body, const QJsonObject &settings) { Renderer r(settings); return "<html><body>" + r.paragraph(escape(title), 26, "#0874C4", true, "font-weight:bold", 8) + r.paragraph(escape(body), 14, "#777777") + "</body></html>"; }

EntryBrowser::EntryBrowser(QWidget *parent) : QTextBrowser(parent) {
    setOpenLinks(false); setOpenExternalLinks(false); setMouseTracking(true); viewport()->setMouseTracking(true);
    setTextInteractionFlags(Qt::TextBrowserInteraction);
    setFrameShape(QFrame::NoFrame);
}
void EntryBrowser::setHtml(const QString &html) {
    QTextBrowser::setHtml(html);
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
void EntryBrowser::mouseMoveEvent(QMouseEvent *event) {
    QTextBrowser::mouseMoveEvent(event);
    QString anchor = anchorAt(event->pos());
    QList<QTextEdit::ExtraSelection> selections;
    if (!anchor.isEmpty()) {
        QTextCursor c = cursorForPosition(event->pos());
        int position = c.position();
        auto isAnchorAt = [this, &anchor](int pos) { QTextCursor p(document()); p.setPosition(pos); p.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor); return p.charFormat().anchorHref() == anchor; };
        if (position > 0 && !isAnchorAt(position)) --position;
        int start = position, end = position;
        while (start > 0 && isAnchorAt(start - 1)) --start;
        while (end < document()->characterCount() - 1 && isAnchorAt(end)) ++end;
        if (end > start) { c.setPosition(start); c.setPosition(end, QTextCursor::KeepAnchor); QTextEdit::ExtraSelection selection; selection.cursor = c; selection.format.setFontUnderline(true); selections << selection; }
    }
    setExtraSelections(selections);
}
void EntryBrowser::leaveEvent(QEvent *event) { setExtraSelections({}); QTextBrowser::leaveEvent(event); }
bool EntryBrowser::viewportEvent(QEvent *event) { if (event->type() == QEvent::ToolTip) return true; return QTextBrowser::viewportEvent(event); }
}
