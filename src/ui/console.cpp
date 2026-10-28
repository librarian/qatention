#include "console.h"
#include <QGuiApplication>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QUrl>

// Semantic US-keyboard fallback. Physical layout support is deliberately isolated here.
static quint32 usageFor(int key) {
    if(key>=Qt::Key_A && key<=Qt::Key_Z) return quint32(4+key-Qt::Key_A);
    if(key>=Qt::Key_1 && key<=Qt::Key_9) return quint32(0x1e + key-Qt::Key_1);
    if(key>=Qt::Key_F1 && key<=Qt::Key_F12) return quint32(0x3a+key-Qt::Key_F1);
    switch(key) {
    case Qt::Key_0:case Qt::Key_ParenRight:return 0x27;
    case Qt::Key_Exclam:return 0x1e; case Qt::Key_At:return 0x1f;
    case Qt::Key_NumberSign:return 0x20;case Qt::Key_Dollar:return 0x21;
    case Qt::Key_Percent:return 0x22;case Qt::Key_AsciiCircum:return 0x23;
    case Qt::Key_Ampersand:return 0x24;case Qt::Key_Asterisk:return 0x25;case Qt::Key_ParenLeft:return 0x26;
    case Qt::Key_Return:case Qt::Key_Enter:return 0x28;
    case Qt::Key_Escape:return 0x29;case Qt::Key_Backspace:return 0x2a;
    case Qt::Key_Tab:case Qt::Key_Backtab:return 0x2b;case Qt::Key_Space:return 0x2c;
    case Qt::Key_Minus:case Qt::Key_Underscore:return 0x2d;
    case Qt::Key_Equal:case Qt::Key_Plus:return 0x2e;
    case Qt::Key_BracketLeft:case Qt::Key_BraceLeft:return 0x2f;
    case Qt::Key_BracketRight:case Qt::Key_BraceRight:return 0x30;
    case Qt::Key_Backslash:case Qt::Key_Bar:return 0x31;
    case Qt::Key_Semicolon:case Qt::Key_Colon:return 0x33;
    case Qt::Key_Apostrophe:case Qt::Key_QuoteDbl:return 0x34;
    case Qt::Key_QuoteLeft:case Qt::Key_AsciiTilde:return 0x35;
    case Qt::Key_Comma:case Qt::Key_Less:return 0x36;
    case Qt::Key_Period:case Qt::Key_Greater:return 0x37;
    case Qt::Key_Slash:case Qt::Key_Question:return 0x38;
    case Qt::Key_CapsLock:return 0x39;case Qt::Key_ScrollLock:return 0x47;case Qt::Key_NumLock:return 0x53;
    case Qt::Key_Print:return 0x46;case Qt::Key_Pause:return 0x48;
    case Qt::Key_Insert:return 0x49;case Qt::Key_Home:return 0x4a;case Qt::Key_PageUp:return 0x4b;
    case Qt::Key_Delete:return 0x4c;case Qt::Key_End:return 0x4d;case Qt::Key_PageDown:return 0x4e;
    case Qt::Key_Right:return 0x4f;case Qt::Key_Left:return 0x50;case Qt::Key_Down:return 0x51;case Qt::Key_Up:return 0x52;
    case Qt::Key_Control:return 0xe0;case Qt::Key_Shift:return 0xe1;
    case Qt::Key_Alt:return 0xe2;case Qt::Key_Meta:return 0xe3;case Qt::Key_AltGr:return 0xe6;
    default:return 0;
    }
}
static quint32 physicalUsage(QKeyEvent *e) {
    auto scan=e->nativeScanCode();
#ifdef Q_OS_LINUX
    if(scan>=8) scan-=8;else scan=0; // XKB evdev keycodes.
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if(scan>=2 && scan<=10) return 0x1e + scan-2;
    if(scan==11) return 0x27;if(scan==12) return 0x2d;if(scan==13) return 0x2e;
    const quint8 top[]={0x14,0x1a,8,0x15,0x17,0x1c,0x18,0xc,0x12,0x13,0x2f,0x30};
    const quint8 home[]={4,0x16,7,9,0xa,0xb,0xd,0xe,0xf,0x33,0x34,0x35};
    const quint8 bottom[]={0x1d,0x1b,6,0x19,5,0x11,0x10,0x36,0x37,0x38};
    if(scan>=0x10 && scan<0x1c) return top[scan-0x10];
    if(scan>=0x1e && scan<0x2a) return home[scan-0x1e];
    if(scan>=0x2c && scan<0x36) return bottom[scan-0x2c];
    if(scan==0x2b) return 0x31;if(scan==0x56) return 0x64;
    if(scan==0x73) return 0x87;if(scan==0x7d) return 0x89;
#endif
    return usageFor(e->key());
}
Console::Console(QWidget *parent):QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setAcceptDrops(true);
    pasteTimer.setInterval(20);
    connect(&pasteTimer,&QTimer::timeout,this,[this]{
        if(!connection || !connection->connected() || pasteAt>=pasteText.size()) {pasteTimer.stop();pasteText.clear();return;}
        bool shift=false;auto usage=characterUsage(pasteText[pasteAt++],shift);
        if(usage) connection->combo(shift?QList<quint32>{0xe1,usage}:QList<quint32>{usage});
    });
    connect(qGuiApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState s){
        if(s!=Qt::ApplicationActive) release();
    });
}
void Console::setView(bool a,bool s) {actual=a;smooth=s;setMinimumSize(actual?frame.size():QSize(1,1));update();}
quint32 Console::characterUsage(QChar c,bool &shift) {
    shift=false;
    if(c>='a' && c<='z') return 4+c.unicode()-'a';
    if(c>='A' && c<='Z') {shift=true;return 4+c.unicode()-'A';}
    if(c>='1' && c<='9') return 0x1e + c.unicode()-'1';
    if(c=='0') return 0x27;if(c==' ') return 0x2c;if(c=='\t') return 0x2b;if(c=='\n') return 0x28;
    const QString normal=QStringLiteral("1234567890-=[]\\;'")+QChar(96)+",./",upper="!@#$%^&*()_+{}|:\"~<>?";
    const quint32 usages[]={0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
    int i=normal.indexOf(c);if(i>=0) return usages[i];i=upper.indexOf(c);if(i>=0) {shift=true;return usages[i];}return 0;
}
void Console::paste(const QString &text) {release();pasteText=text;pasteAt=0;pasteTimer.start();}
void Console::setSession(aten::Session *s) {
    if(connection==s) return;
    release(); if(connection) disconnect(connection,nullptr,this,nullptr);
    connection=s;
    if(s) {
        connect(s,&aten::Session::frameReady,this,[this](const QImage &image){frame=image;if(actual) setMinimumSize(frame.size());update();});
        connect(s,&aten::Session::clearFrame,this,[this]{release();frame={};setMinimumSize(1,1);update();});
    }
    emit sessionChanged();
}
QRectF Console::imageRect() const {
    if(frame.isNull()) return {};
    QSizeF scaled=frame.size(); if(!actual) scaled.scale(size(),Qt::KeepAspectRatio);
    return {(width()-scaled.width())/2,(height()-scaled.height())/2,scaled.width(),scaled.height()};
}
void Console::paintEvent(QPaintEvent *) {
    QPainter painter(this);auto *p=&painter;p->fillRect(rect(),Qt::black);
    if(frame.isNull()) {p->setPen(Qt::white);p->drawText(rect().adjusted(20,20,-20,-20),Qt::AlignCenter|Qt::TextWordWrap,connection?connection->status():"Disconnected");return;}
    p->setRenderHint(QPainter::SmoothPixmapTransform,smooth);p->drawImage(imageRect(),frame);
}
bool Console::saveScreenshot(const QUrl &file) {return !frame.isNull() && file.isLocalFile() && frame.save(file.toLocalFile(),"PNG");}
void Console::keyPressEvent(QKeyEvent *e) {
    if(!connection) {e->accept();return;}
    auto usage=physicalUsage(e);
#ifdef Q_OS_WIN
    if(usage==0x39 || usage==0x47 || usage==0x53) {const int vk=usage==0x39?VK_CAPITAL:usage==0x47?VK_SCROLL:VK_NUMLOCK;if(!(GetKeyState(vk)&1)) usage|=0xff00;}
#endif
    const auto id=e->nativeScanCode()? quint64(e->nativeScanCode()) : (quint64(1)<<32)|quint32(e->key());
    if(e->isAutoRepeat()) {if(held.contains(id)) connection->key(held.value(id),true);e->accept();return;}
    if(usage && !held.contains(id)) {held[id]=usage;connection->key(usage,true);}
    e->accept();
}
void Console::keyReleaseEvent(QKeyEvent *e) {
    if(connection && !e->isAutoRepeat()) {
        const auto id=e->nativeScanCode()? quint64(e->nativeScanCode()) : (quint64(1)<<32)|quint32(e->key());
        auto it=held.find(id);
        if(it!=held.end()) {connection->key(it.value(),false);held.erase(it);}
    }
    e->accept();
}
void Console::pointer(QPointF point,Qt::MouseButtons state,int wheel) {
    const auto rect=imageRect();
    if(!connection || rect.isEmpty()) return;
    int mask=(state.testFlag(Qt::LeftButton)?1:0)|(state.testFlag(Qt::MiddleButton)?2:0)|(state.testFlag(Qt::RightButton)?4:0)|wheel;
    int x=qBound(0,int((point.x()-rect.x())*frame.width()/rect.width()),frame.width()-1);
    int y=qBound(0,int((point.y()-rect.y())*frame.height()/rect.height()),frame.height()-1);
    connection->pointer(x,y,mask);
}
void Console::mousePressEvent(QMouseEvent *e) {setFocus();pointer(e->position(),e->buttons());e->accept();}
void Console::mouseReleaseEvent(QMouseEvent *e) {pointer(e->position(),e->buttons());e->accept();}
void Console::mouseMoveEvent(QMouseEvent *e) {pointer(e->position(),e->buttons());e->accept();}
void Console::wheelEvent(QWheelEvent *e) {
    if(e->angleDelta().y()) {pointer(e->position(),e->buttons(),e->angleDelta().y()>0?8:16);pointer(e->position(),e->buttons());}
    e->accept();
}
void Console::release() {pasteTimer.stop();pasteText.clear();held.clear();if(connection) connection->releaseInput();}
void Console::focusOutEvent(QFocusEvent *e) {release();QWidget::focusOutEvent(e);}
bool Console::event(QEvent *e) {
    if(e->type()==QEvent::ShortcutOverride && static_cast<QKeyEvent*>(e)->key()!=Qt::Key_F11) {e->accept();return true;}
    if(e->type()==QEvent::UngrabMouse || e->type()==QEvent::WindowDeactivate || e->type()==QEvent::Hide) release();
    if(e->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key()==Qt::Key_Tab) {keyPressEvent(static_cast<QKeyEvent*>(e));return true;}
    return QWidget::event(e);
}
void Console::dragEnterEvent(QDragEnterEvent *e) {if(e->mimeData()->urls().size()==1 && e->mimeData()->urls().first().isLocalFile() && e->mimeData()->urls().first().toLocalFile().endsWith(".iso",Qt::CaseInsensitive)) e->acceptProposedAction();}
void Console::dropEvent(QDropEvent *e) {if(e->mimeData()->urls().size()==1) {emit isoDropped(e->mimeData()->urls().first().toLocalFile());e->acceptProposedAction();}}
