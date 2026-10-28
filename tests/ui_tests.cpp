#include <QtTest>
#include <QTemporaryDir>
#include <QMenuBar>
#include <QToolButton>
#include <QDialog>
#include "ui/mainwindow.h"
class UiTests : public QObject {
    Q_OBJECT
private slots:
    void menusTabsAndView() {
        QTemporaryDir dir;aten::Profiles profiles(dir.filePath("profiles.json"));aten::DebugLog log(dir.filePath("log"));
        MainWindow window(&profiles,&log,true);window.show();
        auto *tabs=window.findChild<QTabWidget*>();QVERIFY(tabs);QCOMPARE(tabs->count(),1);
        const auto menus=window.menuBar()->actions();QCOMPARE(menus.size(),8);
        QStringList expected={"&Power","&Keys","&Mouse","&View","&Connection","&Logging","&Storage","&Help"};
        for(int i=0;i<expected.size();++i) QCOMPARE(menus[i]->text(),expected[i]);
        auto *plus=tabs->findChild<QToolButton*>();QVERIFY(plus);
        // New-tab opens a modal connection dialog; cancel it without network access.
        QTimer dismiss;dismiss.setInterval(30);
        connect(&dismiss,&QTimer::timeout,&window,[&]{
            if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {dialog->reject();dismiss.stop();}
        });
        dismiss.start();
        plus->click();QCOMPARE(tabs->count(),2);
        tabs->setCurrentIndex(0);QCOMPARE(tabs->currentIndex(),0);
        QVERIFY(QMetaObject::invokeMethod(tabs,"tabCloseRequested",Q_ARG(int,1)));QCOMPARE(tabs->count(),1);
        bool found=false;
        for(auto *a:window.findChildren<QAction*>()) if(a->text()=="Full screen") {a->trigger();QVERIFY(window.isFullScreen());a->trigger();QVERIFY(!window.isFullScreen());found=true;}
        QVERIFY(found);
        for(auto c:QString("aA1!\\|~\n")) {bool shift=false;QVERIFY(Console::characterUsage(c,shift)!=0);}
        bool shift=false;QCOMPARE(Console::characterUsage(QChar(0x20ac),shift),quint32(0));
        window.close();
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
