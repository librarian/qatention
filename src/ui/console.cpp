#include "console.h"

#include "core/input.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QCryptographicHash>
#include <QUrl>

// Semantic US-keyboard fallback. Physical layout support is deliberately
// isolated here.
static quint32 usageFor(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return quint32(4 + key - Qt::Key_A);
    }
    if (key >= Qt::Key_1 && key <= Qt::Key_9) {
        return quint32(0x1e + key - Qt::Key_1);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return quint32(0x3a + key - Qt::Key_F1);
    }
    switch (key) {
        case Qt::Key_0:
        case Qt::Key_ParenRight:
            return 0x27;
        case Qt::Key_Exclam:
            return 0x1e;
        case Qt::Key_At:
            return 0x1f;
        case Qt::Key_NumberSign:
            return 0x20;
        case Qt::Key_Dollar:
            return 0x21;
        case Qt::Key_Percent:
            return 0x22;
        case Qt::Key_AsciiCircum:
            return 0x23;
        case Qt::Key_Ampersand:
            return 0x24;
        case Qt::Key_Asterisk:
            return 0x25;
        case Qt::Key_ParenLeft:
            return 0x26;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return 0x28;
        case Qt::Key_Escape:
            return 0x29;
        case Qt::Key_Backspace:
            return 0x2a;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            return 0x2b;
        case Qt::Key_Space:
            return 0x2c;
        case Qt::Key_Minus:
        case Qt::Key_Underscore:
            return 0x2d;
        case Qt::Key_Equal:
        case Qt::Key_Plus:
            return 0x2e;
        case Qt::Key_BracketLeft:
        case Qt::Key_BraceLeft:
            return 0x2f;
        case Qt::Key_BracketRight:
        case Qt::Key_BraceRight:
            return 0x30;
        case Qt::Key_Backslash:
        case Qt::Key_Bar:
            return 0x31;
        case Qt::Key_Semicolon:
        case Qt::Key_Colon:
            return 0x33;
        case Qt::Key_Apostrophe:
        case Qt::Key_QuoteDbl:
            return 0x34;
        case Qt::Key_QuoteLeft:
        case Qt::Key_AsciiTilde:
            return 0x35;
        case Qt::Key_Comma:
        case Qt::Key_Less:
            return 0x36;
        case Qt::Key_Period:
        case Qt::Key_Greater:
            return 0x37;
        case Qt::Key_Slash:
        case Qt::Key_Question:
            return 0x38;
        case Qt::Key_CapsLock:
            return 0x39;
        case Qt::Key_ScrollLock:
            return 0x47;
        case Qt::Key_NumLock:
            return 0x53;
        case Qt::Key_Print:
            return 0x46;
        case Qt::Key_Pause:
            return 0x48;
        case Qt::Key_Insert:
            return 0x49;
        case Qt::Key_Home:
            return 0x4a;
        case Qt::Key_PageUp:
            return 0x4b;
        case Qt::Key_Delete:
            return 0x4c;
        case Qt::Key_End:
            return 0x4d;
        case Qt::Key_PageDown:
            return 0x4e;
        case Qt::Key_Right:
            return 0x4f;
        case Qt::Key_Left:
            return 0x50;
        case Qt::Key_Down:
            return 0x51;
        case Qt::Key_Up:
            return 0x52;
        case Qt::Key_Control:
            return 0xe0;
        case Qt::Key_Shift:
            return 0xe1;
        case Qt::Key_Alt:
            return 0xe2;
        case Qt::Key_Meta:
            return 0xe3;
        case Qt::Key_AltGr:
            return 0xe6;
        default:
            return 0;
    }
}

static quint32 physicalUsage(QKeyEvent* e)
{
    auto scan = e->nativeScanCode();
#ifdef Q_OS_LINUX
    if (scan >= 8) {
        scan -= 8;
    } else {
        scan = 0;   // XKB evdev keycodes.
    }
#endif
#ifdef Q_OS_WIN
    // Qt marks Windows E0 scan codes with bit 8. Convert the right modifiers
    // and extended keypad keys to the evdev values used below.
    switch (scan) {
        case 0x11d:
            scan = 97;
            break;   // Right Control.
        case 0x138:
            scan = 100;
            break;   // Right Alt.
        case 0x15b:
            return 0xe3;   // Left Windows.
        case 0x15c:
            return 0xe7;   // Right Windows.
        case 0x11c:
            scan = 96;
            break;   // Keypad Enter.
        case 0x135:
            scan = 98;
            break;   // Keypad Divide.
        default:
            break;
    }
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // Preserve physical identity, independently of Num Lock and layout.
    switch (scan) {
        case 29:
            return 0xe0;   // Left Control.
        case 42:
            return 0xe1;   // Left Shift.
        case 54:
            return 0xe5;   // Right Shift.
        case 56:
            return 0xe2;   // Left Alt.
        case 97:
            return 0xe4;   // Right Control.
        case 100:
            return 0xe6;   // Right Alt.
#ifdef Q_OS_LINUX
        case 125:
            return 0xe3;   // Left Meta.
        case 126:
            return 0xe7;   // Right Meta.
#endif
        case 55:
            return 0x55;   // Keypad multiply.
        case 71:
            return 0x5f;   // Keypad 7.
        case 72:
            return 0x60;   // Keypad 8.
        case 73:
            return 0x61;   // Keypad 9.
        case 74:
            return 0x56;   // Keypad minus.
        case 75:
            return 0x5c;   // Keypad 4.
        case 76:
            return 0x5d;   // Keypad 5.
        case 77:
            return 0x5e;   // Keypad 6.
        case 78:
            return 0x57;   // Keypad plus.
        case 79:
            return 0x59;   // Keypad 1.
        case 80:
            return 0x5a;   // Keypad 2.
        case 81:
            return 0x5b;   // Keypad 3.
        case 82:
            return 0x62;   // Keypad 0.
        case 83:
            return 0x63;   // Keypad decimal.
        case 96:
            return 0x58;   // Keypad Enter.
        case 98:
            return 0x54;   // Keypad divide.
        default:
            break;
    }
    if (scan >= 2 && scan <= 10) {
        return 0x1e + scan - 2;
    }
    if (scan == 11) {
        return 0x27;
    }
    if (scan == 12) {
        return 0x2d;
    }
    if (scan == 13) {
        return 0x2e;
    }
    const quint8 top[] =
        {0x14, 0x1a, 8, 0x15, 0x17, 0x1c, 0x18, 0xc, 0x12, 0x13, 0x2f, 0x30};
    const quint8 home[] =
        {4, 0x16, 7, 9, 0xa, 0xb, 0xd, 0xe, 0xf, 0x33, 0x34, 0x35};
    const quint8 bottom[] =
        {0x1d, 0x1b, 6, 0x19, 5, 0x11, 0x10, 0x36, 0x37, 0x38};
    if (scan >= 0x10 && scan < 0x1c) {
        return top[scan - 0x10];
    }
    if (scan >= 0x1e && scan < 0x2a) {
        return home[scan - 0x1e];
    }
    if (scan >= 0x2c && scan < 0x36) {
        return bottom[scan - 0x2c];
    }
    if (scan == 0x2b) {
        return 0x31;
    }
    if (scan == 0x56) {
        return 0x64;
    }
    if (scan == 0x73) {
        return 0x87;
    }
    if (scan == 0x7d) {
        return 0x89;
    }
#endif
    if (e->modifiers().testFlag(Qt::KeypadModifier)) {
        if (e->key() >= Qt::Key_1 && e->key() <= Qt::Key_9) {
            return 0x59 + quint32(e->key() - Qt::Key_1);
        }
        switch (e->key()) {
            case Qt::Key_0:
                return 0x62;
            case Qt::Key_Enter:
            case Qt::Key_Return:
                return 0x58;
            case Qt::Key_Plus:
                return 0x57;
            case Qt::Key_Minus:
                return 0x56;
            case Qt::Key_Asterisk:
                return 0x55;
            case Qt::Key_Slash:
                return 0x54;
            case Qt::Key_Period:
            case Qt::Key_Comma:
                return 0x63;
            default:
                break;
        }
    }
    return usageFor(e->key());
}

Console::Console(QWidget* parent)
    : QWidget(parent)
{
    auto* stats = new QTimer(this);
    stats->setInterval(1000);
    connect(
        stats,
        &QTimer::timeout,
        this,
        [this]
        {
            if (diagnostics && connection && connection->connected()) {
                const auto hash = frame.isNull()
                                      ? QByteArray("none")
                                      : QCryptographicHash::hash(
                                            QByteArrayView(
                                                reinterpret_cast<const char*>(
                                                    frame.constBits()),
                                                frame.sizeInBytes()),
                                            QCryptographicHash::Sha256)
                                            .toHex();
                emit diagnostic(
                    QString(
                        "View: received=%1 painted=%2 paints=%3 visible=%4 "
                        "focused=%5 size=%6x%7 imageSha256=%8")
                        .arg(receivedFrames)
                        .arg(paintedFrame)
                        .arg(paintEvents)
                        .arg(isVisible())
                        .arg(hasFocus())
                        .arg(width())
                        .arg(height())
                        .arg(QString::fromLatin1(hash)));
            }
        });
    stats->start();
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    pasteTimer.setInterval(20);
    connect(
        &pasteTimer,
        &QTimer::timeout,
        this,
        [this]
        {
            if (!connection || !connection->connected() ||
                pasteAt >= pasteText.size())
            {
                pasteTimer.stop();
                pasteText.clear();
                return;
            }
            bool shift = false;
            auto usage = characterUsage(pasteText[pasteAt++], shift);
            if (usage) {
                connection->combo(
                    shift ? QList<quint32>{0xe1, usage}
                          : QList<quint32>{usage});
            }
        });
    connect(
        qGuiApp,
        &QGuiApplication::applicationStateChanged,
        this,
        [this](Qt::ApplicationState s)
        {
            if (s != Qt::ApplicationActive) {
                release();
            }
        });
}

Console::~Console()
{
    // Child sessions emit clearFrame during QWidget teardown, after our members
    // have been destroyed. Disconnect while this is still a Console.
    release();
    if (connection) {
        disconnect(connection, nullptr, this, nullptr);
    }
}

void Console::setView(bool a, bool s)
{
    actual = a;
    smooth = s;
    setMinimumSize(actual ? frame.size() : QSize(1, 1));
    update();
}

quint32 Console::characterUsage(QChar c, bool& shift)
{
    return aten::characterUsage(c, shift);
}

void Console::paste(const QString& text)
{
    release();
    pasteText = text;
    pasteAt = 0;
    pasteTimer.start();
}

void Console::setSession(aten::Session* s)
{
    if (connection == s) {
        return;
    }
    release();
    if (connection) {
        disconnect(connection, nullptr, this, nullptr);
    }
    connection = s;
    if (s) {
        connect(
            s,
            &aten::Session::frameReady,
            this,
            [this](const QImage& image)
            {
                ++receivedFrames;
                frame = image;
                if (actual) {
                    setMinimumSize(frame.size());
                }
                update();
            });
        connect(
            s,
            &aten::Session::clearFrame,
            this,
            [this]
            {
                release();
                frame = {};
                setMinimumSize(1, 1);
                update();
            });
    }
    emit sessionChanged();
}

QRectF Console::imageRect() const
{
    if (frame.isNull()) {
        return {};
    }
    QSizeF scaled = frame.size();
    if (!actual) {
        scaled.scale(size(), Qt::KeepAspectRatio);
    }
    return {
        (width() - scaled.width()) / 2,
        (height() - scaled.height()) / 2,
        scaled.width(),
        scaled.height()};
}

void Console::paintEvent(QPaintEvent*)
{
    ++paintEvents;
    paintedFrame = receivedFrames;
    QPainter painter(this);
    auto* p = &painter;
    p->fillRect(rect(), Qt::black);
    if (frame.isNull()) {
        p->setPen(Qt::white);
        p->drawText(
            rect().adjusted(20, 20, -20, -20),
            Qt::AlignCenter | Qt::TextWordWrap,
            connection ? connection->status() : "Disconnected");
        return;
    }
    p->setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    p->drawImage(imageRect(), frame);
}

bool Console::saveScreenshot(const QUrl& file)
{
    return !frame.isNull() && file.isLocalFile() &&
           frame.save(file.toLocalFile(), "PNG");
}

void Console::keyPressEvent(QKeyEvent* e)
{
    if (!connection) {
        e->accept();
        return;
    }
    auto usage = physicalUsage(e);
#ifdef Q_OS_WIN
    if (usage == 0x39 || usage == 0x47 || usage == 0x53) {
        const int vk = usage == 0x39   ? VK_CAPITAL
                       : usage == 0x47 ? VK_SCROLL
                                       : VK_NUMLOCK;
        if (!(quint16(GetKeyState(vk)) & 1U)) {
            usage |= 0xff00;
        }
    }
#endif
    const auto id = e->nativeScanCode()
                        ? quint64(e->nativeScanCode())
                        : (quint64(1) << 32U) | quint32(e->key());
    if (e->isAutoRepeat()) {
        if (held.contains(id)) {
            connection->key(held.value(id), true);
        }
        e->accept();
        return;
    }
    if (usage && !held.contains(id)) {
        held[id] = usage;
        connection->key(usage, true);
    }
    e->accept();
}

void Console::keyReleaseEvent(QKeyEvent* e)
{
    if (connection && !e->isAutoRepeat()) {
        const auto id = e->nativeScanCode()
                            ? quint64(e->nativeScanCode())
                            : (quint64(1) << 32U) | quint32(e->key());
        auto it = held.find(id);
        if (it != held.end()) {
            connection->key(it.value(), false);
            held.erase(it);
        }
    }
    e->accept();
}

void Console::pointer(QPointF point, Qt::MouseButtons state, int wheel)
{
    const auto rect = imageRect();
    if (!connection || rect.isEmpty()) {
        return;
    }
    int mask = (state.testFlag(Qt::LeftButton) ? 1U : 0U) |
               (state.testFlag(Qt::MiddleButton) ? 2U : 0U) |
               (state.testFlag(Qt::RightButton) ? 4U : 0U) | unsigned(wheel);
    int x = qBound(
        0,
        int((point.x() - rect.x()) * frame.width() / rect.width()),
        frame.width() - 1);
    int y = qBound(
        0,
        int((point.y() - rect.y()) * frame.height() / rect.height()),
        frame.height() - 1);
    connection->pointer(x, y, mask);
}

void Console::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    pointer(e->position(), e->buttons());
    e->accept();
}

void Console::mouseReleaseEvent(QMouseEvent* e)
{
    pointer(e->position(), e->buttons());
    e->accept();
}

void Console::mouseMoveEvent(QMouseEvent* e)
{
    pointer(e->position(), e->buttons());
    e->accept();
}

void Console::wheelEvent(QWheelEvent* e)
{
    if (e->angleDelta().y()) {
        pointer(e->position(), e->buttons(), e->angleDelta().y() > 0 ? 8 : 16);
        pointer(e->position(), e->buttons());
    }
    e->accept();
}

void Console::release()
{
    pasteTimer.stop();
    pasteText.clear();
    held.clear();
    if (connection) {
        connection->releaseInput();
    }
}

void Console::focusOutEvent(QFocusEvent* e)
{
    release();
    QWidget::focusOutEvent(e);
}

bool Console::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride &&
        static_cast<QKeyEvent*>(e)->key() != Qt::Key_F11)
    {
        e->accept();
        return true;
    }
    if (e->type() == QEvent::UngrabMouse ||
        e->type() == QEvent::WindowDeactivate || e->type() == QEvent::Hide)
    {
        release();
    }
    if (e->type() == QEvent::KeyPress &&
        (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Tab ||
         static_cast<QKeyEvent*>(e)->key() == Qt::Key_Backtab))
    {
        keyPressEvent(static_cast<QKeyEvent*>(e));
        return true;
    }
    return QWidget::event(e);
}

void Console::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->urls().size() == 1 &&
        e->mimeData()->urls().first().isLocalFile() &&
        e->mimeData()->urls().first().toLocalFile().endsWith(
            ".iso",
            Qt::CaseInsensitive))
    {
        e->acceptProposedAction();
    }
}

void Console::dropEvent(QDropEvent* e)
{
    if (e->mimeData()->urls().size() == 1) {
        emit isoDropped(e->mimeData()->urls().first().toLocalFile());
        e->acceptProposedAction();
    }
}
