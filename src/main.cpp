#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QSslKey>
#include <QTimer>
#include "ui/mainwindow.h"
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
    QApplication app(argc,argv);
    QCoreApplication::setApplicationName("QATENtion");
    QCoreApplication::setOrganizationName("qatention");
    QCoreApplication::setApplicationVersion("0.2.0");
    const bool smoke=app.arguments().contains("--smoke-test");
    aten::Profiles profiles(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/profiles.json");
    QSettings settings;
    aten::DebugLog log(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs/qatention.log");
    if(!smoke) log.setEnabled(settings.value("debugLogging",false).toBool());
    QObject::connect(&log,&aten::DebugLog::enabledChanged,&settings,[&]{if(!smoke) settings.setValue("debugLogging",log.enabled());});
    MainWindow window(&profiles,&log,smoke);window.show();
    if(smoke) {
        if(!QSslSocket::supportsSsl()) return 2;
        try { QSslSocket socket;aten::configureClientTls(&socket); }
        catch(const std::exception &) { return 2; }
        QTimer::singleShot(0,&window,[&]{
            const auto path=qEnvironmentVariable("QATENTION_SMOKE_MAIN_SCREENSHOT");
            if(!path.isEmpty() && !window.grab().save(path)) {app.exit(3);return;}
            window.connectionDialog();app.quit();
        });
    }
    return app.exec();
}
