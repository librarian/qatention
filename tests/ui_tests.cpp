#include "ui/mainwindow.h"

#include <QDialog>
#include <QFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class UiTests: public QObject
{
    Q_OBJECT
private slots:

    void restoredTabsStayDisconnected()
    {
        QTemporaryDir dir;
        const auto format = QSettings::defaultFormat();
        const auto organization = QCoreApplication::organizationName(),
                   application = QCoreApplication::applicationName();

        struct Restore
        {
            QSettings::Format format;
            QString organization, application;

            ~Restore()
            {
                QSettings::setDefaultFormat(format);
                QCoreApplication::setOrganizationName(organization);
                QCoreApplication::setApplicationName(application);
            }
        } restore{format, organization, application};

        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
            QSettings::IniFormat,
            QSettings::UserScope,
            dir.path());
        QCoreApplication::setOrganizationName("qatention-tests");
        QCoreApplication::setApplicationName("startup");
        QSettings settings;
        settings.setValue("reopenTabs", true);
        settings.setValue("openTabs", QStringList{"Saved BMC"});
        settings.sync();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        aten::Profiles profiles(dir.filePath("profiles.json"));
        aten::DebugLog log(dir.filePath("log"));
        QVERIFY(profiles.save(
            "Saved BMC",
            {{"host", "127.0.0.1"},
             {"webPort", server.serverPort()},
             {"user", "admin"},
             {"password", "test"},
             {"autoArm", true}}));
        MainWindow window(&profiles, &log, false);
        window.show();
        auto* tabs = window.findChild<QTabWidget*>();
        QCOMPARE(tabs->count(), 1);
        QVERIFY(tabs->tabText(0).contains("Saved BMC"));
        QTest::qWait(150);
        QVERIFY(!server.hasPendingConnections());
        auto* session = window.findChild<aten::Session*>();
        QVERIFY(session);
        QVERIFY(!session->busy());
        QVERIFY(
            !window.findChild<QMessageBox*>("consoleCertificateTrustDialog"));
    }

    void consoleTrustDialogCancel()
    {
        QTemporaryDir dir;
        aten::Profiles profiles(dir.filePath("profiles.json"));
        aten::DebugLog log(dir.filePath("log"));
        MainWindow window(&profiles, &log, true);
        window.show();
        auto* session = window.findChild<aten::Session*>();
        QVERIFY(session);
        QFile f(":/resources/client-cert.pem");
        QVERIFY(f.open(QIODevice::ReadOnly));
        session->consoleCertificateUntrusted(
            "bmc.example",
            5900,
            QSslCertificate(f.readAll()));
        auto* dialog =
            window.findChild<QMessageBox*>("consoleCertificateTrustDialog");
        QVERIFY(dialog);
        QCOMPARE(dialog->defaultButton(), dialog->button(QMessageBox::Cancel));
        QVERIFY(dialog->text().contains("bmc.example:5900"));
        dialog->button(QMessageBox::Cancel)->click();
        QVERIFY(profiles.entries().isEmpty());
        QVERIFY(!session->busy());
    }

    void menusTabsAndView()
    {
        QTemporaryDir dir;
        aten::Profiles profiles(dir.filePath("profiles.json"));
        aten::DebugLog log(dir.filePath("log"));
        MainWindow window(&profiles, &log, true);
        window.show();
        auto* tabs = window.findChild<QTabWidget*>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        const auto menus = window.menuBar()->actions();
        QCOMPARE(menus.size(), 8);
        QStringList expected = {
            "&Power",
            "&Keys",
            "&Mouse",
            "&View",
            "&Connection",
            "&Logging",
            "&Storage",
            "&Help"};
        for (int i = 0; i < expected.size(); ++i) {
            QCOMPARE(menus[i]->text(), expected[i]);
        }
        auto* plus = tabs->findChild<QToolButton*>("newSessionButton");
        QVERIFY(plus);
        // New-tab opens a modal connection dialog; cancel it without network
        // access.
        QTimer dismiss;
        dismiss.setInterval(30);
        bool dialogSeen = false;
        connect(
            &dismiss,
            &QTimer::timeout,
            &window,
            [&]
            {
                if (auto* dialog = qobject_cast<QDialog*>(
                        QApplication::activeModalWidget()))
                {
                    dialogSeen = true;
                    dialog->reject();
                    dismiss.stop();
                }
            });
        dismiss.start();
        QVERIFY(
            QMetaObject::invokeMethod(
                tabs,
                "tabBarDoubleClicked",
                Q_ARG(int, 0)));
        QVERIFY(dialogSeen);
        QCOMPARE(tabs->count(), 1);   // Configure the existing empty tab.
        dismiss.start();
        plus->click();
        QCOMPARE(tabs->count(), 2);
        QVERIFY(tabs->tabText(0).endsWith("[1]"));
        QVERIFY(tabs->tabText(1).endsWith("[2]"));
        tabs->setCurrentIndex(0);
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(
            QMetaObject::invokeMethod(
                tabs,
                "tabCloseRequested",
                Q_ARG(int, 1)));
        QCOMPARE(tabs->count(), 1);
        bool found = false;
        for (auto* a: window.findChildren<QAction*>()) {
            if (a->text() == "Full screen") {
                a->trigger();
                QVERIFY(window.isFullScreen());
                a->trigger();
                QVERIFY(!window.isFullScreen());
                found = true;
            }
        }
        QVERIFY(found);
        for (auto c: QString("aA1!\\|~\n")) {
            bool shift = false;
            QVERIFY(Console::characterUsage(c, shift) != 0);
        }
        bool shift = false;
        QCOMPARE(Console::characterUsage(QChar(0x20ac), shift), quint32(0));
        window.close();
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
