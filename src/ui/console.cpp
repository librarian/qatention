#include "console.h"
#include <QGuiApplication>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHoverEvent>
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
    case Qt::Key_Print:return 0x46;case Qt::Key_Pause:return 0x48;
    case Qt::Key_Insert:return 0x49;case Qt::Key_Home:return 0x4a;case Qt::Key_PageUp:return 0x4b;
    case Qt::Key_Delete:return 0x4c;case Qt::Key_End:return 0x4d;case Qt::Key_PageDown:return 0x4e;
    case Qt::Key_Right:return 0x4f;case Qt::Key_Left:return 0x50;case Qt::Key_Down:return 0x51;case Qt::Key_Up:return 0x52;
    case Qt::Key_Control:return 0xe0;case Qt::Key_Shift:return 0xe1;
    case Qt::Key_Alt:return 0xe2;case Qt::Key_Meta:return 0xe3;case Qt::Key_AltGr:return 0xe6;
    default:return 0;
    }
}
Console::Console(QQuickItem *parent):QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton|Qt::RightButton|Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope,true);
    connect(qGuiApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState s){
        if(s!=Qt::ApplicationActive) release();
    });
}
void Console::setSession(aten::Session *s) {
    if(connection==s) return;
    release(); if(connection) disconnect(connection,nullptr,this,nullptr);
    connection=s;
    if(s) {
        connect(s,&aten::Session::frameReady,this,[this](const QImage &image){frame=image;update();});
        connect(s,&aten::Session::clearFrame,this,[this]{held.clear();frame={};update();});
    }
    emit sessionChanged();
}
QRectF Console::imageRect() const {
    if(frame.isNull()) return {};
    QSizeF scaled=frame.size(); scaled.scale(boundingRect().size(),Qt::KeepAspectRatio);
    return {(width()-scaled.width())/2,(height()-scaled.height())/2,scaled.width(),scaled.height()};
}
void Console::paint(QPainter *p) {
    p->fillRect(boundingRect(),QColor("#15191e"));
    if(frame.isNull()) {p->setPen(QColor("#9aa5b1"));p->drawText(boundingRect(),Qt::AlignCenter,"Console display");return;}
    p->setRenderHint(QPainter::SmoothPixmapTransform,false);
    p->drawImage(imageRect(),frame);
}
bool Console::saveScreenshot(const QUrl &file) {return !frame.isNull() && file.isLocalFile() && frame.save(file.toLocalFile(),"PNG");}
void Console::keyPressEvent(QKeyEvent *e) {
    if(!connection || e->isAutoRepeat()) {e->accept();return;}
    const auto usage=usageFor(e->key());
    const auto id=e->nativeScanCode()? quint64(e->nativeScanCode()) : (quint64(1)<<32)|quint32(e->key());
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
void Console::mousePressEvent(QMouseEvent *e) {forceActiveFocus();pointer(e->position(),e->buttons());e->accept();}
void Console::mouseReleaseEvent(QMouseEvent *e) {pointer(e->position(),e->buttons());e->accept();}
void Console::mouseMoveEvent(QMouseEvent *e) {pointer(e->position(),e->buttons());e->accept();}
void Console::hoverMoveEvent(QHoverEvent *e) {if(hasActiveFocus()) pointer(e->position(),Qt::NoButton);e->accept();}
void Console::wheelEvent(QWheelEvent *e) {
    if(e->angleDelta().y()) {pointer(e->position(),e->buttons(),e->angleDelta().y()>0?8:16);pointer(e->position(),e->buttons());}
    e->accept();
}
void Console::release() {held.clear();if(connection) connection->releaseInput();}
void Console::focusOutEvent(QFocusEvent *e) {release();QQuickPaintedItem::focusOutEvent(e);}
void Console::mouseUngrabEvent() {release();}
