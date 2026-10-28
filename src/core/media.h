#pragma once
#include "protocol.h"
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>
namespace aten {
QByteArray mediaControl(quint32 type,const QByteArray &payload={});
QByteArray mediaAttach(const QString &user,const QString &password,quint32 stamp);
struct ScsiResult { QByteArray data; quint8 status=0; };
class CdRom {
public:
    explicit CdRom(const QString &path);
    ScsiResult execute(const QByteArray &cdb,quint32 allocation);
    quint64 blocks() const { return (quint64(file.size())+2047)/2048; }
private:
    QFile file;
    quint8 sense=0,asc=0;
    ScsiResult fail(quint8 code);
};
class Media : public QObject {
    Q_OBJECT
public:
    explicit Media(QObject *parent=nullptr);
    ~Media();
    void mount(const Launch &endpoint,bool allowUntrusted,const QString &path);
    void unmount();
    void probe(const Launch &endpoint,bool allowUntrusted);
    bool busy() const { return phase!=Idle; }
    QString status() const { return message; }
    QString path() const { return isoPath; }
signals:
    void changed();
    void diagnostic(const QString &message);
private:
    enum Phase { Idle, Health, Initialize, DataConnect, PostHealth, Attach, Serving, Detach } phase=Idle;
    Launch launch;
    bool trust=false,probeOnly=false;
    QString message="No media mounted",isoPath;
    QPointer<QSslSocket> channel,data;
    QByteArray buffer;
    QTimer deadline,keepalive;
    std::unique_ptr<CdRom> cd;
    void openChannel();
    void ready();
    void consume();
    void send(const QByteArray &bytes);
    void stop(const QString &reason);
};
void configureClientTls(QSslSocket *socket);
}
