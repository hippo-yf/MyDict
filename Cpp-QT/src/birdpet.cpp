#include "birdpet.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QBitmap>
#include <QRegion>

namespace MyDict {
static QPoint globalMouse(QMouseEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    return e->globalPosition().toPoint();
#else
    return e->globalPos();
#endif
}
BirdPet::BirdPet(QWidget *owner):QWidget(owner,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus),host(owner){
    setObjectName("birdPet");setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(220,230);setCursor(Qt::OpenHandCursor);
    variant=QRandomGenerator::global()->bounded(5);setProperty("variant",variant);
    offset=QPoint(qMax(0,owner->width()-260),-157);
    host->installEventFilter(this);
    timer.setSingleShot(true);connect(&timer,&QTimer::timeout,this,&BirdPet::advance);
    auto *watcher=new QFutureWatcher<QVector<QImage>>(this);
    connect(watcher,&QFutureWatcher<QVector<QImage>>::finished,this,[this,watcher]{
        frames=watcher->result();watcher->deleteLater();if(frames.size()!=8)return;
        ready=true;displayFrame();follow();timer.start(3000+QRandomGenerator::global()->bounded(4000));
    });
    const int chosen=variant;
    watcher->setFuture(QtConcurrent::run([chosen]{
        // Decode only the selected atlas, then discard it; retain eight small display frames.
        QImage atlas(QString(":/birds/%1.png").arg(chosen));QVector<QImage> result;if(atlas.isNull())return result;
        const double w=atlas.width()/4.,h=atlas.height()/2.;
        for(int i=0;i<8;++i){
            double scale=chosen<3?.275:.17*1024/h;
            double fx=chosen==2?.42:chosen==3?.43:chosen==4?.64:.61;
            double fy=chosen==0?454/h:chosen==1?(i<4?468:452)/h:chosen==2?390/h:chosen==3?.66:.76;
            QImage image(220,230,QImage::Format_ARGB32);image.fill(Qt::transparent);
            QPainter p(&image);p.setRenderHint(QPainter::SmoothPixmapTransform,chosen!=0);
            p.drawImage(QRectF(110-fx*w*scale,160-fy*h*scale,w*scale,h*scale),atlas,QRectF((i%4)*w,(i/4)*h,w,h));p.end();
            // Same clean-alpha display treatment used in the approved previews.
            for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x){auto c=row[x];int a=qAlpha(c);a=chosen<2?(a>=240?255:0):qBound(0,2*a-128,255);row[x]=a?qRgba(qRed(c),qGreen(c),qBlue(c),a):0;}}
            result.append(image);
        }return result;
    }));
}
void BirdPet::follow(){
    if(!ready)return;
    if(!host->isVisible()||host->isMinimized()){hide();return;}
    move(host->mapToGlobal(QPoint())+offset);show();raise();
}
bool BirdPet::eventFilter(QObject *object,QEvent *event){
    if(object==host){
        if(event->type()==QEvent::Hide||event->type()==QEvent::Close){hide();timer.stop();}
        else if(event->type()==QEvent::Move||event->type()==QEvent::Resize||event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange){
            follow();if(ready&&!timer.isActive())timer.start(3000);
        }
    }return QWidget::eventFilter(object,event);
}
void BirdPet::paintEvent(QPaintEvent *){if(ready){QPainter p(this);p.drawImage(0,0,frames[frame]);}}
void BirdPet::displayFrame(){setMask(QRegion(QBitmap::fromImage(frames[frame].createAlphaMask())));update();}
void BirdPet::play(bool clicked){
    if(!ready||pressed)return;
    const bool blink=!clicked&&QRandomGenerator::global()->bounded(100)<55;
    if(variant<2){
        sequence=clicked?QVector<QPair<int,int>>{{4,110},{5,100},{6,150},{5,100},{6,130},{7,130},{2,240},{0,150}}:
          blink?QVector<QPair<int,int>>{{1,130},{0,150}}:QVector<QPair<int,int>>{{2,350},{0,140},{3,350},{0,150}};
    }else sequence=blink?QVector<QPair<int,int>>{{1,100},{2,130},{1,80},{3,150},{0,100}}:
          QVector<QPair<int,int>>{{4,180},{5,clicked?950:650},{6,180},{7,100},{0,100}};
    step=0;advance();
}
void BirdPet::advance(){
    if(!isVisible()||pressed){timer.start(3000);return;}
    if(sequence.isEmpty()){play(false);return;}
    if(step>=sequence.size()){sequence.clear();frame=0;displayFrame();timer.start(3000+QRandomGenerator::global()->bounded(4000));return;}
    const auto next=sequence[step++];frame=next.first;displayFrame();timer.start(next.second);
}
void BirdPet::mousePressEvent(QMouseEvent *e){if(e->button()!=Qt::LeftButton)return;pressed=true;dragged=false;pressGlobal=globalMouse(e);pressPosition=pos();timer.stop();setCursor(Qt::ClosedHandCursor);grabMouse();e->accept();}
void BirdPet::mouseMoveEvent(QMouseEvent *e){if(!pressed)return;auto delta=globalMouse(e)-pressGlobal;if(delta.manhattanLength()>=QApplication::startDragDistance())dragged=true;if(dragged){move(pressPosition+delta);offset=pos()-host->mapToGlobal(QPoint());}e->accept();}
void BirdPet::mouseReleaseEvent(QMouseEvent *e){if(e->button()!=Qt::LeftButton||!pressed)return;pressed=false;releaseMouse();setCursor(Qt::OpenHandCursor);if(dragged){sequence.clear();frame=0;displayFrame();timer.start(3000);}else play(true);e->accept();}
}
