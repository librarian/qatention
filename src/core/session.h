#pragma once
#include "decoder.h"
#include "protocol.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <QVariantMap>

namespace aten {
class Session : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
public:
    explicit Session(QObject *parent=nullptr);
    bool busy() const { return phase!=Phase::Idle; }
    bool connected() const { return phase==Phase::Messages; }
    QString status() const { return statusText; }
    Q_INVOKABLE void open(const QVariantMap &options);
    Q_INVOKABLE void close();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void releaseInput();
    Q_INVOKABLE void ctrlAltDelete();
    void key(quint32 usage, bool down);
    void pointer(int x,int y,int buttons);
signals:
    void stateChanged();
    void frameReady(const QImage &image);
    void clearFrame();
private:
    enum class Phase { Idle, Reconnecting, Web, Connecting, Banner, Security, Challenge, Auth, Init, Messages };
    Phase phase=Phase::Idle;
    QString statusText="Disconnected";
    QPointer<QSslSocket> socket;
    QPointer<QNetworkReply> reply;
    QNetworkAccessManager web;
    QTimer deadline, heartbeat, nextFrame, retry;
    QVariantMap reconnectOptions;
    bool established=false;
    int retryCount=0;
    QElapsedTimer received, fullFrame;
    QByteArray buffer;
    Launch launch;
    std::unique_ptr<Decoder> decoder;
    QUrl baseUrl;
    bool allowUntrusted=false, switchJava=false, controlling=true, pending=false, forceFull=false;
    bool closing=false, webAuthenticated=false;
    quint64 generation=0;
    QSet<quint32> held;
    QPoint lastPointer;
    int buttons=0, frameInterval=100;
    void setStatus(const QString &text);
    void fail(const QString &text, bool transient=false);
    void resetTransport();
    void startAttempt(const QVariantMap &options);
    void connectKvm();
    void process();
    bool consumeMessage();
    void send(const QByteArray &bytes);
    void requestFrame();
    void requestWeb(const QString &path,const QByteArray &body,int step,const QByteArray &csrf={});
    void handleWeb(const QByteArray &body,int step);
};
}
