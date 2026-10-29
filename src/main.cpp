#include "ui/mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QSslKey>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("QATENtion");
    QCoreApplication::setOrganizationName("qatention");
    QCoreApplication::setApplicationVersion("0.2.0");
    const bool smoke = app.arguments().contains("--smoke-test");
    aten::Profiles profiles(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        "/profiles.json");
    aten::DebugLog log(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
        "/logs/qatention.log");
    MainWindow window(&profiles, &log, smoke);
    window.show();
    if (smoke) {
        if (!QSslSocket::supportsSsl()) {
            return 2;
        }
        try {
            QSslSocket socket;
            aten::configureClientTls(&socket);
        } catch (const std::exception&) {
            return 2;
        }
        QTimer::singleShot(
            0,
            &window,
            [&]
            {
                const auto path =
                    qEnvironmentVariable("QATENTION_SMOKE_MAIN_SCREENSHOT");
                if (!path.isEmpty() && !window.grab().save(path)) {
                    app.exit(3);
                    return;
                }
                window.connectionDialog();
                app.quit();
            });
    }
    if (!smoke) {
        QTimer::singleShot(0, &window, &MainWindow::connectionDialog);
    }
    return app.exec();
}
