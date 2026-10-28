#pragma once
#include <QQuickPaintedItem>
#include <QHash>
#include "core/session.h"

class Console : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(aten::Session* session READ session WRITE setSession NOTIFY sessionChanged)
public:
    explicit Console(QQuickItem *parent=nullptr);
    aten::Session *session() const { return connection; }
    void setSession(aten::Session *s);
    void paint(QPainter *painter) override;
    Q_INVOKABLE bool saveScreenshot(const QUrl &file);
signals:
    void sessionChanged();
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mouseUngrabEvent() override;
private:
    QPointer<aten::Session> connection;
    QImage frame;
    QHash<quint64,quint32> held;
    QRectF imageRect() const;
    void pointer(QPointF point,Qt::MouseButtons buttons,int wheel=0);
    void release();
};
