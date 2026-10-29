#pragma once
#include "core/debuglog.h"
#include "core/media.h"
#include "core/session.h"

#include <QMarginsF>
#include <QQuickPaintedItem>
#include <QSettings>

class Mobile: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap safeInsets READ safeInsets NOTIFY safeInsetsChanged)
    Q_PROPERTY(int modifiers READ modifiers NOTIFY modifiersChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString lastHost READ lastHost CONSTANT)
    Q_PROPERTY(QString lastUser READ lastUser CONSTANT)
    Q_PROPERTY(
        QVariantMap viewSettings READ viewSettings NOTIFY viewSettingsChanged)
    Q_PROPERTY(bool mediaBusy READ mediaBusy NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaStatus READ mediaStatus NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaPath READ mediaPath NOTIFY mediaChanged)
    Q_PROPERTY(QString powerStatus READ powerStatus NOTIFY powerStatusChanged)
public:
    Mobile();
    QVariantMap viewSettings() const;
    Q_INVOKABLE void setViewOption(const QString& key, const QVariant& value);
    Q_INVOKABLE void mountIso(const QUrl& url);
    Q_INVOKABLE void unmountIso();
    Q_INVOKABLE void power(int command);
    Q_INVOKABLE void hotplug();

    bool mediaBusy() const
    {
        return media.busy();
    }

    QString mediaStatus() const
    {
        return media.status();
    }

    QString mediaPath() const
    {
        return media.path();
    }

    QString powerStatus() const;

    QVariantList profiles() const
    {
        return settings.value("mobileProfiles").toList();
    }

    Q_INVOKABLE void removeProfile(const QString& host, const QString& user);

    QVariantMap safeInsets() const
    {
        return {
            {"left", insets.left()},
            {"top", insets.top()},
            {"right", insets.right()},
            {"bottom", insets.bottom()}};
    }

    int modifiers() const
    {
        return modifierMask;
    }

    Q_INVOKABLE void toggleModifier(int usage);
    Q_INVOKABLE QString savedPassword(const QString& host, const QString& user);
    aten::Session session;
    aten::DebugLog log;

    QString lastHost() const
    {
        return settings.value("host").toString();
    }

    QString lastUser() const
    {
        return settings.value("user", "ADMIN").toString();
    }

    Q_INVOKABLE void connectTo(
        const QString& host,
        const QString& user,
        const QString& password,
        bool unverified,
        bool remember);
    Q_INVOKABLE void disconnectSession();
    Q_INVOKABLE void trustConsole();

    Q_INVOKABLE void rejectConsole()
    {
        pendingTrust.clear();
    }

    Q_INVOKABLE void tapKey(int usage);
    Q_INVOKABLE void typeText(const QString& text);
    Q_INVOKABLE void copyLogs();
    void background(bool active);
signals:
    void viewSettingsChanged();
    void mediaChanged();
    void powerStatusChanged();
    void profilesChanged();
    void safeInsetsChanged();
    void modifiersChanged();
    void trustRequested(const QString& endpoint, const QString& fingerprint);
    void notice(const QString& message);

private:
    QSettings settings;
    aten::Media media;
    bool foreground = true;
    int appliedQuality = -1, appliedMode = -1;
    void applyViewSettings();
    QMarginsF insets;
    QTimer insetTimer;
    int modifierMask = 0;
    void releaseModifiers();
    QVariantMap options, pendingTrust;
    QString trustKey() const;
};

class MobileVideo: public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY zoomChanged)
    Q_PROPERTY(aten::Session* session READ session WRITE setSession NOTIFY
                   sessionChanged)
public:
    explicit MobileVideo(QQuickItem* parent = nullptr)
        : QQuickPaintedItem(parent)
    {}

    aten::Session* session() const
    {
        return connection;
    }

    void setSession(aten::Session* session);
    void paint(QPainter* painter) override;

    QSize frameSize() const
    {
        return frame.isNull() ? QSize(640, 480) : frame.size();
    }

    qreal zoom() const
    {
        return magnification;
    }

    Q_INVOKABLE void fit();
    Q_INVOKABLE void adjustView(qreal scale, qreal dx, qreal dy);
    void geometryChange(
        const QRectF& newGeometry,
        const QRectF& oldGeometry) override;
    Q_INVOKABLE void pointer(qreal x, qreal y, bool down);
    Q_INVOKABLE void release();
signals:
    void sessionChanged();
    void zoomChanged();
    void frameSizeChanged();

private:
    QPointer<aten::Session> connection;
    QImage frame;
    qreal magnification = 1;
    QPointF pan;
    void constrainPan();
    QRectF imageRect() const;
};
