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
#include "ui/console.h"

int main(int argc,char **argv) {
    // Preserve Ctrl/Meta meanings for the remote USB keyboard on macOS.
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
    QGuiApplication app(argc,argv);
    QCoreApplication::setApplicationName("QATENtion");
    QCoreApplication::setOrganizationName("qatention");
    QCoreApplication::setApplicationVersion("0.1.0");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<Console>("QATENtion.Console",1,0,"Console");
    qmlRegisterUncreatableType<aten::Session>("QATENtion.Console",1,0,"Session","Provided by the application");
    aten::Session session;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("kvm",&session);
    engine.loadFromModule("QATENtion","Main");
    if(engine.rootObjects().isEmpty()) return 1;
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&session,&aten::Session::close);
    if(app.arguments().contains("--smoke-test")) {
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
