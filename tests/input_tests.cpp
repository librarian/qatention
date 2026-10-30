#include "mockconsole.h"
#include "ui/console.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QVBoxLayout>

class InputTests: public QObject
{
    Q_OBJECT
private slots:

    void physicalKeys_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("scanCode");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<quint32>("usage");
        // Columns hold platform-native scan codes, not just semantic Qt keys.
#ifdef Q_OS_WIN
        constexpr int rightControl = 0x11d, rightShift = 54, keypadOne = 79;
#else
        constexpr int rightControl = 105, rightShift = 62, keypadOne = 87;
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        QTest::newRow("right-control")
            << int(Qt::Key_Control) << rightControl << int(Qt::ControlModifier)
            << quint32(0xe4);
        QTest::newRow("right-shift") << int(Qt::Key_Shift) << rightShift
                                     << int(Qt::ShiftModifier) << quint32(0xe5);
        QTest::newRow("keypad-numlock-on")
            << int(Qt::Key_1) << keypadOne << int(Qt::KeypadModifier)
            << quint32(0x59);
        QTest::newRow("keypad-numlock-off")
            << int(Qt::Key_End) << keypadOne << int(Qt::KeypadModifier)
            << quint32(0x59);
#endif
        QTest::newRow("keypad-semantic-enter")
            << int(Qt::Key_Enter) << 0 << int(Qt::KeypadModifier)
            << quint32(0x58);
        QTest::newRow("keypad-semantic-divide")
            << int(Qt::Key_Slash) << 0 << int(Qt::KeypadModifier)
            << quint32(0x54);
    }

    void physicalKeys()
    {
        QFETCH(int, key);
        QFETCH(int, scanCode);
        QFETCH(int, modifiers);
        QFETCH(quint32, usage);
        MockConsole server;
        server.connectSession();
        QVERIFY(server.session.connected());
        Console console;
        console.setSession(&server.session);
        QKeyEvent press(
            QEvent::KeyPress,
            key,
            Qt::KeyboardModifiers(modifiers),
            quint32(scanCode),
            0,
            0);
        QKeyEvent release(
            QEvent::KeyRelease,
            key,
            Qt::KeyboardModifiers(modifiers),
            quint32(scanCode),
            0,
            0);
        QApplication::sendEvent(&console, &press);
        QApplication::sendEvent(&console, &release);
        QTRY_VERIFY(server.peer->bytesAvailable() >= 36);
        QCOMPARE(
            server.peer->readAll(),
            aten::keyEvent(usage, true) + aten::keyEvent(usage, false));
    }

    void shiftTabStaysRemote()
    {
        MockConsole server;
        server.connectSession();
        QVERIFY(server.session.connected());
        QWidget container;
        QVBoxLayout layout(&container);
        Console console;
        QLineEdit otherFocusableWidget;
        layout.addWidget(&otherFocusableWidget);
        layout.addWidget(&console);
        console.setSession(&server.session);
        container.show();
        console.setFocus();
        QTest::qWait(20);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QKeyEvent release(
            QEvent::KeyRelease,
            Qt::Key_Backtab,
            Qt::ShiftModifier);
        QApplication::sendEvent(&console, &press);
        QApplication::sendEvent(&console, &release);
        QTRY_VERIFY(server.peer->bytesAvailable() >= 36);
        QCOMPARE(
            server.peer->readAll(),
            aten::keyEvent(0x2b, true) + aten::keyEvent(0x2b, false));
        QVERIFY(console.hasFocus());
    }
};
QTEST_MAIN(InputTests)
#include "input_tests.moc"
