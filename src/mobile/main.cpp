#include "core/media.h"
#include "mobile.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char** argv)
{
#ifdef Q_OS_ANDROID
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
#endif
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QATENTION_VERSION);
    app.setOrganizationName("qatention");
    app.setApplicationName("QATENtion Android");
    QQuickStyle::setStyle("Material");
    qmlRegisterUncreatableType<aten::Session>(
        "Qatention.Native",
        1,
        0,
        "Session",
        "Provided by the application");
    qmlRegisterType<MobileVideo>("Qatention.Native", 1, 0, "ConsoleVideo");
    Mobile mobile;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mobile", &mobile);
    engine.rootContext()->setContextProperty("connection", &mobile.session);
    engine.rootContext()->setContextProperty("debugLog", &mobile.log);
    QObject::connect(
        &app,
        &QGuiApplication::applicationStateChanged,
        &mobile,
        [&](Qt::ApplicationState state)
        { mobile.background(state == Qt::ApplicationActive); });
    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        &mobile,
        &Mobile::disconnectSession);
    engine.loadFromModule("QatentionMobile", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    if (app.arguments().contains("--smoke-test")) {
        if (!QSslSocket::supportsSsl()) {
            return 2;
        }
        try {
            QSslSocket socket;
            aten::configureClientTls(&socket);
        } catch (...) {
            return 3;
        }
        QTimer::singleShot(
            300,
            &app,
            [&]
            {
                const auto path =
                    qEnvironmentVariable("QATENTION_MOBILE_SCREENSHOT");
                if (!path.isEmpty()) {
                    auto* window = qobject_cast<QQuickWindow*>(
                        engine.rootObjects().first());
                    if (!window || !window->grabWindow().save(path)) {
                        app.exit(4);
                        return;
                    }
                }
                app.quit();
            });
    }
    return app.exec();
}
