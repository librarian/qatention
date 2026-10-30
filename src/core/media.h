#pragma once
#include "mediaprotocol.h"
#include "scsi.h"

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>

namespace aten {
class Media: public QObject
{
    Q_OBJECT
public:
    explicit Media(QObject* parent = nullptr);
    ~Media() override;
    void mount(
        const Launch& endpoint,
        bool allowUntrusted,
        const QString& path,
        const QSslCertificate& trustedCertificate = QSslCertificate());
    void unmount();
    void probe(
        const Launch& endpoint,
        bool allowUntrusted,
        const QSslCertificate& trustedCertificate = QSslCertificate());

    bool busy() const
    {
        return phase != Idle;
    }

    QString status() const
    {
        return message;
    }

    QString path() const
    {
        return isoPath;
    }

signals:
    void changed();
    void diagnostic(const QString& message);

private:
    enum Phase
    {
        Idle,
        Health,
        Initialize,
        DataConnect,
        PostHealth,
        Attach,
        Serving,
        Detach
    } phase = Idle;

    Launch launch;
    QSslCertificate trustedCertificate;
    bool certificateMatches() const;
    bool trust = false, probeOnly = false;
    QString message = "No media mounted", isoPath;
    QPointer<QSslSocket> channel, data;
    QByteArray buffer;
    QTimer deadline, keepalive;
    std::unique_ptr<CdRom> cd;
    void openChannel();
    void ready();
    void consume();
    void send(const QByteArray& bytes);
    void stop(const QString& reason);
};

void configureClientTls(QSslSocket* socket);
}   // namespace aten
