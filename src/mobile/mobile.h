#pragma once
#include "core/session.h"
#include "core/debuglog.h"
#include <QQuickPaintedItem>
#include <QSettings>

class Mobile : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastHost READ lastHost CONSTANT)
    Q_PROPERTY(QString lastUser READ lastUser CONSTANT)
public:
    Mobile();
    aten::Session session;
    aten::DebugLog log;
    QString lastHost() const {return settings.value("host").toString();}
    QString lastUser() const {return settings.value("user","ADMIN").toString();}
    Q_INVOKABLE void connectTo(const QString &host,const QString &user,const QString &password,bool unverified);
    Q_INVOKABLE void disconnectSession();
    Q_INVOKABLE void trustConsole();
    Q_INVOKABLE void rejectConsole() {pendingTrust.clear();}
    Q_INVOKABLE void tapKey(int usage);
    Q_INVOKABLE void typeText(const QString &text);
    Q_INVOKABLE void copyLogs();
    void background(bool active);
signals:
    void trustRequested(const QString &endpoint,const QString &fingerprint);
    void notice(const QString &message);
private:
    QSettings settings;
    QVariantMap options,pendingTrust;
    QTimer typing;
    QString queuedText;
    QString trustKey() const;
};

class MobileVideo : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(aten::Session* session READ session WRITE setSession NOTIFY sessionChanged)
public:
    explicit MobileVideo(QQuickItem *parent=nullptr):QQuickPaintedItem(parent) {}
    aten::Session *session() const {return connection;}
    void setSession(aten::Session *session);
    void paint(QPainter *painter) override;
    Q_INVOKABLE void pointer(qreal x,qreal y,bool down);
    Q_INVOKABLE void release();
signals:
    void sessionChanged();
private:
    QPointer<aten::Session> connection;
    QImage frame;
    QRectF imageRect() const;
};
