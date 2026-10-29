#pragma once
#include "decoder.h"
#include "protocol.h"
#include "videotrace.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <QVariantMap>
#include <functional>

namespace aten {
class Session : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
public:
    explicit Session(QObject *parent=nullptr);
    ~Session() override;
    void setKeepMedia(bool value);
    bool busy() const { return phase!=Phase::Idle; }
    bool connected() const { return phase==Phase::Messages; }
    QString status() const { return statusText; }
    Q_INVOKABLE void open(const QVariantMap &options);
    Q_INVOKABLE void close();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void releaseInput();
    Q_INVOKABLE void ctrlAltDelete();
    void key(quint32 usage, bool down);
    Launch mediaEndpoint() const { return mediaLaunch; }
    bool untrustedAllowed() const { return allowUntrusted; }
    void power(int command);
    void hotplug();
    void combo(const QList<quint32> &keys);
    void setDisplay(int quality,int mode);
    void setAutoReconnect(bool enabled,const QVariantMap &options);
    void setStreaming(bool active,int interval,int fullSeconds);
    void setDuplicateEndpoint(bool duplicate);
    bool startVideoTrace(const QString &path);
    void stopVideoTrace();
    QString information() const;
    QStringList users() const { return sessionUsers; }
    int hostPower() const { return powerState; }

    void pointer(int x,int y,int buttons);
signals:
    void videoPacket(const QByteArray &packet,quint32 encoding);
    void diagnostic(const QString &message);
    void unsupportedFrame(const QByteArray &packet);
    void stateChanged();
    void frameReady(const QImage &image);
    void clearFrame();
private:
    VideoTrace videoTrace;
    enum class Phase { Idle, Reconnecting, Web, Connecting, Banner, Security, Challenge, Auth, Init, Messages };
    bool keepMedia=false;
    QList<std::function<void()>> deferredLogouts;
    Phase phase=Phase::Idle;
    QString statusText="Disconnected", tlsFailure;
    QPointer<QSslSocket> socket;
    QPointer<QNetworkReply> reply;
    QPointer<QObject> issuerFetch;
    QNetworkAccessManager web;
    QTimer deadline, heartbeat, nextFrame, retry;
    QVariantMap reconnectOptions;
    bool established=false;
    int retryCount=0;
    QElapsedTimer received, fullFrame, frameRequested;
    QByteArray buffer;
    Launch launch,mediaLaunch;
    QStringList sessionUsers,userBurst;
    int powerState=-1,quality=11,imageMode=444,fullSeconds=5;
    bool streaming=true;
    bool duplicateEndpoint=false;
    quint64 videoBytes=0,frames=0;
    quint64 requests=0,fullRequests=0,updates=0,keyEvents=0,pointerEvents=0,rejectedInput=0;
    QElapsedTimer uptime,powerPoll;
    std::unique_ptr<Decoder> decoder;
    QUrl baseUrl;
    bool allowUntrusted=false, switchJava=false, controlling=true, pending=false, forceFull=false;
    bool closing=false, webAuthenticated=false;
    quint64 generation=0;
    QSet<quint32> held;
    QPoint lastPointer;
    int buttons=0, frameInterval=100;
    void reportTlsErrors(const QString &endpoint,const QList<QSslError> &errors);
    void setStatus(const QString &text);
    void fail(const QString &text, bool transient=false);
    void resetTransport();
    void startAttempt(const QVariantMap &options);
    void connectKvm();
    void process();
    bool consumeMessage();
    void send(const QByteArray &bytes);
    void requestFrame();
    void requestWeb(const QString &path,const QByteArray &body,int step,const QByteArray &csrf={},bool chainRetried=false);
    void handleWeb(const QByteArray &body,int step);
};
}
