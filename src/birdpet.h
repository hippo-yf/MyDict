#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QVector>

namespace MyDict {
class BirdPet : public QWidget {
public:
    explicit BirdPet(QWidget *owner);
    void setPetVisible(bool visible);
protected:
    bool eventFilter(QObject *, QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    QWidget *host;
    QVector<QImage> frames;
    QTimer timer;
    QVector<QPair<int,int>> sequence;
    QPoint offset, pressGlobal, pressPosition;
    bool enabled=true;
    bool pressed=false, dragged=false, ready=false;
    int variant=0, frame=0, step=0;
    void follow();
    void play(bool clicked);
    void advance();
    void displayFrame();
};
}
