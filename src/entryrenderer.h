#pragma once
#include <QJsonObject>
#include <QString>
#include <QTextBrowser>
#include <QWidget>
namespace MyDict {
QJsonObject flattenStyles(const QJsonObject &settings);
QString styleValue(const QJsonObject &settings, const QString &key, const QString &fallback = {});
double styleNumber(const QJsonObject &settings, const QString &key, double fallback);
class EntryRenderer {
public:
    static QString render(const QJsonObject &entry, const QJsonObject &settings, const QString &selectedPart = {});
    static QString daily(const QJsonObject &article, const QJsonObject &settings);
    static QString reviewSentence(const QString &text, const QJsonArray &segments, const QJsonObject &settings);
    static QString message(const QString &title, const QString &body, const QJsonObject &settings);
};
class EntryBrowser : public QTextBrowser {
    Q_OBJECT
public:
    explicit EntryBrowser(QWidget *parent = nullptr);
public slots:
    void setHtml(const QString &html);
private:
    QWidget *partBar = nullptr;
protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object,QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool viewportEvent(QEvent *event) override;
};
class ReviewBrowser : public EntryBrowser {
    Qt::TextFormat format = Qt::PlainText;
    void fitHeight();
public:
    explicit ReviewBrowser(QWidget *parent=nullptr);
    void setTextFormat(Qt::TextFormat value){format=value;}
    void setWordWrap(bool){}
    void setText(const QString &value);
protected:
    void resizeEvent(QResizeEvent *event) override;
};

}
