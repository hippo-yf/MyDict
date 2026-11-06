#include "birdpet.h"
#include <QtTest>
#include <QApplication>
#include <QMouseEvent>
using namespace MyDict;
class BirdTest:public QObject {
    Q_OBJECT
private slots:
    void loadMoveAndVisibility(){
        QWidget owner;owner.resize(600,500);owner.move(200,250);owner.show();
        BirdPet pet(&owner);
        QVERIFY(pet.property("variant").toInt()>=0&&pet.property("variant").toInt()<5);
        QTRY_VERIFY(pet.isVisible());QVERIFY(!pet.mask().isEmpty());
        const QPoint original=pet.pos(),origin=owner.pos();
        owner.move(origin+QPoint(25,15));QTRY_COMPARE(pet.pos(),original+QPoint(25,15));
        QPoint local=pet.mask().boundingRect().center();QPoint global=pet.mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&pet,&press);
        QMouseEvent move(QEvent::MouseMove,local+QPoint(35,20),global+QPoint(35,20),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&pet,&move);
        QMouseEvent release(QEvent::MouseButtonRelease,local,global+QPoint(35,20),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&pet,&release);
        const QPoint dropped=pet.pos();QCOMPARE(dropped,original+QPoint(60,35));
        QTest::qWait(200);QCOMPARE(pet.pos(),dropped);
        owner.hide();QTRY_VERIFY(!pet.isVisible());owner.show();QTRY_VERIFY(pet.isVisible());QCOMPARE(pet.pos(),dropped);
    }
};
int main(int argc,char **argv){QApplication app(argc,argv);Q_INIT_RESOURCE(resources);BirdTest test;return QTest::qExec(&test,argc,argv);}
#include "birdpet_test.moc"
