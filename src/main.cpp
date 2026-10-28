#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QFile>
#include <QQuickWindow>
#include "core/session.h"
#include "core/profiles.h"
#include "core/debuglog.h"
#include <QSettings>
#include <QStandardPaths>
#include "ui/console.h"

int main(int argc,char **argv) {
    // Preserve Ctrl/Meta meanings for the remote USB keyboard on macOS.
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
    QGuiApplication app(argc,argv);
    QCoreApplication::setApplicationName("QATENtion");
    QCoreApplication::setOrganizationName("qatention");
    QCoreApplication::setApplicationVersion("0.1.0");
    qmlRegisterType<Console>("QATENtion.Console",1,0,"Console");
    qmlRegisterUncreatableType<aten::Session>("QATENtion.Console",1,0,"Session","Provided by the application");
    aten::Session session;
    aten::Profiles profiles(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/profiles.json");
    QSettings settings;
    aten::DebugLog log(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs/qatention.log");
    log.setEnabled(settings.value("debugLogging",false).toBool());
    QObject::connect(&log,&aten::DebugLog::enabledChanged,&settings,[&]{
        if(settings.value("debugLogging",false).toBool()!=log.enabled()) settings.setValue("debugLogging",log.enabled());
    });
    QObject::connect(&session,&aten::Session::diagnostic,&log,&aten::DebugLog::write);
    QObject::connect(&session,&aten::Session::unsupportedFrame,&log,&aten::DebugLog::capture);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("kvm",&session);
    engine.rootContext()->setContextProperty("profiles",&profiles);
    engine.rootContext()->setContextProperty("debugLog",&log);
    engine.loadFromModule("QATENtion","Main");
    if(engine.rootObjects().isEmpty()) return 1;
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&session,&aten::Session::close);
    if(app.arguments().contains("--smoke-test")) {
        auto *dialog=engine.rootObjects().first()->findChild<QObject*>("connectionDialog");
        if(!dialog || !QMetaObject::invokeMethod(dialog,"open")) return 4;
        QFile pfx(":/resources/client.pfx"); pfx.open(QIODevice::ReadOnly);
        QSslKey key; QSslCertificate cert;
        if(!QSslSocket::supportsSsl() || !QSslCertificate::importPkcs12(&pfx,&key,&cert)) return 2;
        QTimer::singleShot(300,&app,[&]{
            const auto path=qEnvironmentVariable("QATENTION_SMOKE_SCREENSHOT");
            if(!path.isEmpty()) {
                auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                if(!window || !window->grabWindow().save(path)) {app.exit(3);return;}
            }
            app.quit();
        });
    }
    return app.exec();
}
