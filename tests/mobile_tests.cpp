#include "mobile/mobile.h"
#include <QtTest>
#include <QPainter>

class MobileTests : public QObject {
    Q_OBJECT
private slots:
    void fitZoomPanAndResize() {
        aten::Session session;
        MobileVideo video;
        video.setWidth(400);video.setHeight(300);video.setSession(&session);
        QImage source(800,400,QImage::Format_RGB32);source.fill(Qt::red);
        {QPainter p(&source);p.fillRect(400,0,400,400,Qt::blue);}
        emit session.frameReady(source);
        auto render=[&] {
            QImage image(int(video.width()),int(video.height()),QImage::Format_RGB32);
            QPainter painter(&image);video.paint(&painter);return image;
        };
        auto image=render();
        QCOMPARE(image.pixelColor(200,0),QColor(Qt::black));
        QCOMPARE(image.pixelColor(100,100),QColor(Qt::red));
        QCOMPARE(image.pixelColor(300,100),QColor(Qt::blue));
        QCOMPARE(image.pixelColor(200,299),QColor(Qt::black));
        video.adjustView(2,0,0);
        QCOMPARE(video.zoom(),2.0);
        QCOMPARE(render().pixelColor(100,0),QColor(Qt::red));
        video.adjustView(1,10000,10000);
        QCOMPARE(render().pixelColor(399,299),QColor(Qt::red));
        video.adjustView(1,-10000,-10000);
        QCOMPARE(render().pixelColor(0,0),QColor(Qt::blue));
        video.adjustView(100,0,0);QCOMPARE(video.zoom(),8.0);
        video.fit();QCOMPARE(video.zoom(),1.0);
        video.setWidth(200);video.setHeight(400);
        image=render();
        QCOMPARE(image.pixelColor(100,149),QColor(Qt::black));
        QCOMPARE(image.pixelColor(50,200),QColor(Qt::red));
        QCOMPARE(image.pixelColor(150,200),QColor(Qt::blue));
        QCOMPARE(image.pixelColor(100,250),QColor(Qt::black));
        video.adjustView(4,0,0);
        QImage resized(640,480,QImage::Format_RGB32);resized.fill(Qt::green);
        emit session.frameReady(resized);
        QCOMPARE(video.zoom(),1.0);
        QCOMPARE(render().pixelColor(100,200),QColor(Qt::green));
    }
};
QTEST_MAIN(MobileTests)
#include "mobile_tests.moc"
