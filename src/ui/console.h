#pragma once
#include <QWidget>
#include <QHash>
#include "core/session.h"

class Console : public QWidget {
    Q_OBJECT
    Q_PROPERTY(aten::Session* session READ session WRITE setSession NOTIFY sessionChanged)
public:
    explicit Console(QWidget *parent=nullptr);
    aten::Session *session() const { return connection; }
    void setSession(aten::Session *s);
    void paintEvent(QPaintEvent *event) override;
    QSize frameSize() const { return frame.size(); }
    void setView(bool actual,bool smooth);
    void cancelInput() { release(); }
    void paste(const QString &text);
    static quint32 characterUsage(QChar c,bool &shift);
    Q_INVOKABLE bool saveScreenshot(const QUrl &file);
signals:
    void sessionChanged();
    void isoDropped(const QString &path);
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool event(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    QPointer<aten::Session> connection;
    QImage frame;
    bool actual=false,smooth=false;
    QTimer pasteTimer;
    QString pasteText;
    qsizetype pasteAt=0;
    QHash<quint64,quint32> held;
    QRectF imageRect() const;
    void pointer(QPointF point,Qt::MouseButtons buttons,int wheel=0);
    void release();
};
