#include "mobile/mobile.h"

#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

class MobileTests: public QObject
{
    Q_OBJECT
    QTemporaryDir settingsDir;
private slots:

    void initTestCase()
    {
        QVERIFY(settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
            QSettings::IniFormat,
            QSettings::UserScope,
            settingsDir.path());
        QCoreApplication::setOrganizationName("qatention-tests");
        QCoreApplication::setApplicationName("mobile-tests");
        QQuickStyle::setStyle("Material");
        qmlRegisterUncreatableType<aten::Session>(
            "Qatention.Native",
            1,
            0,
            "Session",
            "Test session");
        qmlRegisterType<MobileVideo>("Qatention.Native", 1, 0, "ConsoleVideo");
    }

    void phoneLayoutAndRenderedFrame()
    {
        Mobile mobile;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("mobile", &mobile);
        engine.rootContext()->setContextProperty("connection", &mobile.session);
        engine.rootContext()->setContextProperty("debugLog", &mobile.log);
        engine.loadFromModule("MobileTest", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window =
            qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(393, 800);
        auto* dialog = window->findChild<QObject*>("connectDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        auto* video = window->findChild<MobileVideo*>("consoleVideo");
        auto* viewport = window->findChild<QQuickItem*>("consoleViewport");
        auto* keyboard = window->findChild<QQuickItem*>("keyboard");
        QVERIFY(video && viewport && keyboard);
        QImage frame(1024, 768, QImage::Format_RGB32);
        frame.fill(Qt::green);
        emit mobile.session.frameReady(frame);
        QTest::qWait(250);
        QCOMPARE(qRound(viewport->width()), 393);
        QCOMPARE(qRound(video->width()), 393);
        auto image = window->grabWindow();
        QVERIFY(!image.isNull());
        const auto ratio = image.width() / 393.0;
        const auto middle = video->mapToScene(QPointF(0, video->height() / 2));
        QCOMPARE(
            image.pixelColor(int(5 * ratio), int(middle.y() * ratio)),
            QColor(Qt::green));
        QCOMPARE(
            image.pixelColor(int(388 * ratio), int(middle.y() * ratio)),
            QColor(Qt::green));
        const auto screenshot =
            qEnvironmentVariable("QATENTION_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QVERIFY(image.save(screenshot));
        }
        int keys = 0;
        for (auto* child: keyboard->childItems()) {
            if (child->property("text").isValid()) {
                ++keys;
                QVERIFY(child->x() >= 0);
                QVERIFY(child->x() + child->width() <= keyboard->width() + 0.1);
            }
        }
        QCOMPARE(keys, 74);
        QVERIFY(
            QMetaObject::invokeMethod(
                viewport,
                "zoomAt",
                Q_ARG(QVariant, 2.0),
                Q_ARG(QVariant, 196.5),
                Q_ARG(QVariant, 100.0)));
        QCOMPARE(viewport->property("zoom").toDouble(), 2.0);
        QTest::qWait(30);
        QVERIFY(video->width() > viewport->width());
        QVERIFY(QMetaObject::invokeMethod(viewport, "fit"));
        QCOMPARE(viewport->property("contentX").toDouble(), 0.0);
        QCOMPARE(qRound(video->width()), 393);
        auto* device = QTest::createTouchDevice();
        const int cy = qRound(
            viewport->mapToScene(QPointF(0, viewport->height() / 2)).y());
        QTest::touchEvent(window, device)
            .press(0, QPoint(140, cy), window)
            .press(1, QPoint(250, cy), window);
        QTest::qWait(30);
        QTest::touchEvent(window, device)
            .move(0, QPoint(100, cy), window)
            .move(1, QPoint(290, cy), window);
        QTest::qWait(30);
        QTest::touchEvent(window, device)
            .move(0, QPoint(60, cy), window)
            .move(1, QPoint(330, cy), window);
        QTest::qWait(30);
        QTest::touchEvent(window, device)
            .release(0, QPoint(60, cy), window)
            .release(1, QPoint(330, cy), window);
        QTRY_VERIFY(viewport->property("zoom").toDouble() > 1.1);
        const auto oldX = viewport->property("contentX").toDouble();
        QTest::touchEvent(window, device).press(0, QPoint(240, cy), window);
        QTest::qWait(30);
        QTest::touchEvent(window, device).move(0, QPoint(180, cy), window);
        QTest::qWait(30);
        QTest::touchEvent(window, device).move(0, QPoint(110, cy), window);
        QTest::qWait(30);
        QTest::touchEvent(window, device).release(0, QPoint(110, cy), window);
        QTRY_VERIFY(viewport->property("contentX").toDouble() > oldX);
    }

    void profilesPersistAndRemainSeparate()
    {
        QSettings().clear();
        Mobile mobile;
        mobile.connectTo(
            "first.invalid",
            "alice",
            "fixture-password",
            false,
            true);
        mobile.disconnectSession();
        mobile.connectTo(
            "second.invalid",
            "bob",
            "fixture-password",
            true,
            false);
        mobile.disconnectSession();
        QCOMPARE(mobile.profiles().size(), 2);
        for (const auto& entry: mobile.profiles()) {
            QVERIFY(!entry.toMap().contains("password"));
        }
        mobile.connectTo(
            "first.invalid",
            "alice",
            "new-fixture-password",
            true,
            true);
        mobile.disconnectSession();
        QCOMPARE(mobile.profiles().size(), 2);
        Mobile restored;
        QCOMPARE(restored.profiles().size(), 2);
        QVERIFY(restored.profiles()[0].toMap().value("unverified").toBool());
        restored.removeProfile("first.invalid", "alice");
        QCOMPARE(restored.profiles().size(), 1);
        QCOMPARE(
            restored.profiles()[0].toMap().value("host").toString(),
            QString("second.invalid"));
        Mobile afterDelete;
        QCOMPARE(afterDelete.profiles().size(), 1);
        QSettings().clear();
    }

    void sceneFit()
    {
        QQuickWindow window;
        window.resize(400, 500);
        aten::Session session;
        MobileVideo video(window.contentItem());
        video.setWidth(400);
        video.setHeight(500);
        video.setSession(&session);
        QImage source(1024, 768, QImage::Format_RGB32);
        source.fill(Qt::green);
        emit session.frameReady(source);
        window.show();
        QTest::qWait(200);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        const auto ratio = image.width() / 400.0;
        QCOMPARE(
            image.pixelColor(int(390 * ratio), int(250 * ratio)),
            QColor(Qt::green));
        QCOMPARE(
            image.pixelColor(int(10 * ratio), int(250 * ratio)),
            QColor(Qt::green));
        QCOMPARE(
            image.pixelColor(int(200 * ratio), int(10 * ratio)),
            QColor(Qt::black));
    }

    void viewPreferencesPersistAndRejectInvalidValues()
    {
        QSettings().clear();
        {
            Mobile mobile;
            QCOMPARE(mobile.viewSettings().value("fps").toInt(), 20);
            QCOMPARE(mobile.viewSettings().value("fullSeconds").toInt(), 5);
            mobile.setViewOption("fps", 30);
            mobile.setViewOption("fullSeconds", -1);
            mobile.setViewOption("quality", 6);
            mobile.setViewOption("mode", 422);
            mobile.setViewOption("smooth", true);
            mobile.setViewOption("actualSize", true);
            mobile.setViewOption("keyboard", false);
            const auto valid = mobile.viewSettings();
            mobile.setViewOption("fps", -10);
            mobile.setViewOption("fullSeconds", 42);
            mobile.setViewOption("quality", 12);
            mobile.setViewOption("mode", 123);
            mobile.setViewOption("unknown", true);
            QCOMPARE(mobile.viewSettings(), valid);
            mobile.background(false);
            mobile.background(true);
            QCOMPARE(mobile.viewSettings(), valid);
            QVERIFY(!mobile.mediaBusy());
            mobile.mountIso(QUrl::fromLocalFile("/missing.iso"));
            mobile.power(2);
            QVERIFY(!mobile.session.busy());
            QVERIFY(!mobile.mediaBusy());
        }
        Mobile restored;
        QCOMPARE(restored.viewSettings().value("fps").toInt(), 30);
        QCOMPARE(restored.viewSettings().value("fullSeconds").toInt(), -1);
        QCOMPARE(restored.viewSettings().value("quality").toInt(), 6);
        QCOMPARE(restored.viewSettings().value("mode").toInt(), 422);
        QVERIFY(restored.viewSettings().value("smooth").toBool());
        QVERIFY(restored.viewSettings().value("actualSize").toBool());
        QVERIFY(!restored.viewSettings().value("keyboard").toBool());
        QSettings().clear();
    }

    void settingsDialogsAndKeyboardVisibility()
    {
        Mobile mobile;
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.rootContext()->setContextProperty("mobile", &mobile);
        engine.rootContext()->setContextProperty("connection", &mobile.session);
        engine.rootContext()->setContextProperty("debugLog", &mobile.log);
        engine.loadFromModule("MobileTest", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window =
            qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(393, 800);
        QVERIFY(
            QMetaObject::invokeMethod(
                window->findChild<QObject*>("connectDialog"),
                "close"));
        auto* keyboard = window->findChild<QQuickItem*>("keyboard");
        auto* viewport = window->findChild<QQuickItem*>("consoleViewport");
        QVERIFY(keyboard && viewport);
        auto* video = window->findChild<MobileVideo*>("consoleVideo");
        QVERIFY(video);
        QImage frame(1024, 768, QImage::Format_RGB32);
        frame.fill(Qt::green);
        emit mobile.session.frameReady(frame);
        QTest::qWait(50);
        const auto oldHeight = viewport->height();
        mobile.setViewOption("keyboard", false);
        QTRY_VERIFY(!keyboard->isVisible());
        QTRY_VERIFY(viewport->height() > oldHeight);
        mobile.setViewOption("actualSize", true);
        QTRY_COMPARE(qRound(video->width()), 1024);
        mobile.setViewOption("smooth", true);
        QTRY_VERIFY(video->smooth());
        mobile.setViewOption("actualSize", false);
        QTRY_COMPARE(qRound(video->width()), 393);
        for (const auto& size: {QSize(393, 800), QSize(800, 393)}) {
            window->resize(size);
            for (const auto* name: {"viewDialog", "mediaDialog", "powerDialog"})
            {
                auto* dialog = window->findChild<QObject*>(name);
                QVERIFY(dialog);
                QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
                QTest::qWait(100);
                QVERIFY(dialog->property("height").toReal() < window->height());
                QVERIFY(dialog->property("width").toReal() < window->width());
                const auto screenshot =
                    qEnvironmentVariable("QATENTION_SETTINGS_SCREENSHOT");
                if (!screenshot.isEmpty() &&
                    QString::fromLatin1(name) == "viewDialog" &&
                    size.width() == 393)
                {
                    QVERIFY(window->grabWindow().save(screenshot));
                }
                QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            }
        }
        QCOMPARE(warnings.size(), 0);
        QSettings().clear();
    }

    void fitZoomPanAndResize()
    {
        aten::Session session;
        MobileVideo video;
        video.setWidth(400);
        video.setHeight(300);
        video.setSession(&session);
        QImage source(800, 400, QImage::Format_RGB32);
        source.fill(Qt::red);
        {
            QPainter p(&source);
            p.fillRect(400, 0, 400, 400, Qt::blue);
        }
        emit session.frameReady(source);
        auto render = [&]
        {
            QImage image(
                int(video.width()),
                int(video.height()),
                QImage::Format_RGB32);
            QPainter painter(&image);
            video.paint(&painter);
            return image;
        };
        auto image = render();
        QCOMPARE(image.pixelColor(200, 0), QColor(Qt::black));
        QCOMPARE(image.pixelColor(100, 100), QColor(Qt::red));
        QCOMPARE(image.pixelColor(300, 100), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(200, 299), QColor(Qt::black));
        video.adjustView(2, 0, 0);
        QCOMPARE(video.zoom(), 2.0);
        QCOMPARE(render().pixelColor(100, 0), QColor(Qt::red));
        video.adjustView(1, 10000, 10000);
        QCOMPARE(render().pixelColor(399, 299), QColor(Qt::red));
        video.adjustView(1, -10000, -10000);
        QCOMPARE(render().pixelColor(0, 0), QColor(Qt::blue));
        video.adjustView(100, 0, 0);
        QCOMPARE(video.zoom(), 8.0);
        video.fit();
        QCOMPARE(video.zoom(), 1.0);
        video.setWidth(200);
        video.setHeight(400);
        image = render();
        QCOMPARE(image.pixelColor(100, 149), QColor(Qt::black));
        QCOMPARE(image.pixelColor(50, 200), QColor(Qt::red));
        QCOMPARE(image.pixelColor(150, 200), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(100, 250), QColor(Qt::black));
        video.adjustView(4, 0, 0);
        QImage resized(640, 480, QImage::Format_RGB32);
        resized.fill(Qt::green);
        emit session.frameReady(resized);
        QCOMPARE(video.zoom(), 1.0);
        QCOMPARE(render().pixelColor(100, 200), QColor(Qt::green));
    }
};
QTEST_MAIN(MobileTests)
#include "mobile_tests.moc"
