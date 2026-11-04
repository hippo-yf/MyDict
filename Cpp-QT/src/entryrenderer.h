#pragma once
#include <QJsonObject>
#include <QString>
#include <QTextBrowser>
#include <QTextObjectInterface>
namespace MyDict {
QJsonObject flattenStyles(const QJsonObject &settings);
QString styleValue(const QJsonObject &settings, const QString &key, const QString &fallback = {});
double styleNumber(const QJsonObject &settings, const QString &key, double fallback);
class EntryRenderer {
public:
    static QString render(const QJsonObject &entry, const QJsonObject &settings, const QString &selectedPart = {});
    static QString message(const QString &title, const QString &body, const QJsonObject &settings);
};
class EntryBrowser : public QTextBrowser, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit EntryBrowser(QWidget *parent = nullptr);
    QSizeF intrinsicSize(QTextDocument *document, int position, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int position, const QTextFormat &format) override;
public slots:
    void setHtml(const QString &html);
protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool viewportEvent(QEvent *event) override;
};
}
