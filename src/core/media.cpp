#include "media.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QSslKey>

namespace aten {
void configureClientTls(QSslSocket* socket)
{
    const auto override =
        QCoreApplication::instance()->property("clientPfx").toString();
    const auto custom =
        override.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/client.pfx"
            : override;
    QSslKey key;
    QSslCertificate cert;
    if (!override.isEmpty() || QFile::exists(custom)) {
        QFile pfx(custom);
        if (!pfx.open(QIODevice::ReadOnly)) {
            throw Error("Cannot open client.pfx");
        }
        if (!QSslCertificate::importPkcs12(&pfx, &key, &cert)) {
            throw Error(("Cannot import custom client.pfx with TLS backend " +
                         QSslSocket::activeBackend())
                            .toStdString());
        }
    } else {
        // SecureTransport implements PEM key/certificate loading, but not Qt's
        // PKCS#12 importer. These are the same public vendor identity as
        // client.pfx.
        QFile certificate(":/resources/client-cert.pem"),
            privateKey(":/resources/client-key.pem");
        if (!certificate.open(QIODevice::ReadOnly) ||
            !privateKey.open(QIODevice::ReadOnly))
        {
            throw Error("Cannot open bundled vendor client identity");
        }
        cert = QSslCertificate(&certificate, QSsl::Pem);
        key = QSslKey(&privateKey, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    }
    if (cert.isNull() || key.isNull()) {
        throw Error("Cannot load vendor client certificate or key");
    }
    socket->setLocalCertificate(cert);
    socket->setPrivateKey(key);
    socket->setProtocol(QSsl::TlsV1_2);
}

Media::Media(QObject* parent)
    : QObject(parent)
{
    deadline.setSingleShot(true);
    keepalive.setInterval(10000);
    connect(
        &deadline,
        &QTimer::timeout,
        this,
        [this]
        {
            stop(
                phase == Detach ? "Media unmounted"
                                : "Virtual media timed out");
        });
    connect(
        &keepalive,
        &QTimer::timeout,
        this,
        [this]
        {
            if (phase == Serving) {
                send(mediaControl(MediaControl::HealthRequest));
            }
        });
}

Media::~Media()
{
    blockSignals(true);
    if (data) {
        data->write(mediaControl(MediaControl::Detach));
    }
    stop("Media closed");
}

void Media::stop(const QString& reason)
{
    phase = Idle;
    deadline.stop();
    keepalive.stop();
    if (channel) {
        channel->disconnect(this);
        channel->abort();
        channel->deleteLater();
    }
    if (data && data != channel) {
        data->disconnect(this);
        data->abort();
        data->deleteLater();
    }
    channel = nullptr;
    data = nullptr;
    buffer.clear();
    cd.reset();
    launch = {};
    isoPath.clear();
    message = reason;
    emit diagnostic(reason);
    emit changed();
}

void Media::mount(
    const Launch& endpoint,
    bool allow,
    const QString& path,
    const QSslCertificate& certificate)
{
    if (busy()) {
        return;
    }
    try {
        probeOnly = false;
        cd = std::make_unique<CdRom>(path);
        mediaAttach(endpoint.user, endpoint.password, 0);
        launch = endpoint;
        trustedCertificate = certificate;
        trust = allow;
        isoPath = path;
        phase = Health;
        message = "Attaching ISO…";
        emit changed();
        openChannel();
    } catch (const std::exception& e) {
        stop(QString::fromUtf8(e.what()));
    }
}

void Media::probe(
    const Launch& endpoint,
    bool allow,
    const QSslCertificate& certificate)
{
    if (busy()) {
        return;
    }
    try {
        probeOnly = true;
        launch = endpoint;
        trustedCertificate = certificate;
        trust = allow;
        phase = Health;
        message = "Checking media slots…";
        emit changed();
        openChannel();
    } catch (const std::exception& e) {
        stop(QString::fromUtf8(e.what()));
    }
}

void Media::unmount()
{
    if (phase == Serving) {
        keepalive.stop();
        phase = Detach;
        send(mediaControl(MediaControl::Detach));
        deadline.start(1000);
    } else {
        stop("Media unmounted");
    }
}

bool Media::certificateMatches() const
{
    return channel && !trustedCertificate.isNull() &&
           channel->peerCertificate().digest(QCryptographicHash::Sha256) ==
               trustedCertificate.digest(QCryptographicHash::Sha256);
}

void Media::openChannel()
{
    if (channel && channel != data) {
        channel->disconnect(this);
        channel->abort();
        channel->deleteLater();
    }
    channel = new QSslSocket(this);
    buffer.clear();
    channel->setReadBufferSize(1024LL * 1024 + 8);
    deadline.start(15000);
    connect(channel, &QSslSocket::readyRead, this, &Media::consume);
    connect(
        channel,
        &QSslSocket::disconnected,
        this,
        [this]
        {
            stop(
                phase == Detach ? "Media unmounted"
                                : "Virtual media disconnected");
        });
    connect(
        channel,
        &QSslSocket::errorOccurred,
        this,
        [this]
        {
            if (channel) {
                stop("Virtual media: " + channel->errorString());
            }
        });
    connect(
        channel,
        &QSslSocket::sslErrors,
        this,
        [this](const QList<QSslError>& errors)
        {
            for (const auto& e: errors) {
                emit diagnostic("Virtual media TLS: " + e.errorString());
            }
            if (!channel) {
                return;
            }
            if (trustedCertificate.isNull()) {
                if (trust) {
                    channel->ignoreSslErrors();
                }
                return;
            }
            if (!certificateMatches()) {
                stop(
                    "Virtual media certificate differs from the trusted "
                    "console certificate");
                return;
            }
            for (const auto& error: errors) {
                switch (error.error()) {
                    case QSslError::HostNameMismatch:
                    case QSslError::SelfSignedCertificate:
                    case QSslError::SelfSignedCertificateInChain:
                    case QSslError::UnableToGetIssuerCertificate:
                    case QSslError::UnableToGetLocalIssuerCertificate:
                    case QSslError::UnableToVerifyFirstCertificate:
                        break;
                    default:
                        return;
                }
            }
            emit diagnostic(
                "Virtual media TLS: trusted console certificate fingerprint "
                "matched");
            channel->ignoreSslErrors(errors);
        });
    connect(
        channel,
        launch.tls ? &QSslSocket::encrypted : &QSslSocket::connected,
        this,
        &Media::ready);
    if (launch.tls) {
        configureClientTls(channel);
        channel->connectToHostEncrypted(launch.host, launch.mediaPort);
    } else {
        channel->connectToHost(launch.host, launch.mediaPort);
    }
}

void Media::ready()
{
    try {
        if (!trustedCertificate.isNull() &&
            (!channel->isEncrypted() || !certificateMatches()))
        {
            stop(
                "Virtual media certificate differs from the trusted console "
                "certificate");
            return;
        }
        channel->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        if (phase == Health || phase == PostHealth) {
            send(mediaControl(MediaControl::HealthRequest));
        } else if (phase == Initialize) {
            send(mediaControl(MediaControl::Initialize));
        } else if (phase == DataConnect) {
            data = channel;
            phase = PostHealth;
            openChannel();
        }
    } catch (const std::exception& e) {
        stop(QString::fromUtf8(e.what()));
    }
}

void Media::send(const QByteArray& bytes)
{
    if (!channel) {
        return;
    }
    if (channel->bytesToWrite() > 32LL * 1024 * 1024 ||
        channel->write(bytes) != bytes.size())
    {
        stop("Virtual media write failed");
        return;
    }
    // Idle reads are legal; a stuck writer is not.
    if (phase == Serving && channel->bytesToWrite() > 0 && !deadline.isActive())
    {
        deadline.start(30000);
    }
}

void Media::consume()
{
    if (!channel) {
        return;
    }
    buffer += channel->readAll();
    if (buffer.size() > 1024LL * 1024 + 8) {
        stop("Virtual media receive limit exceeded");
        return;
    }
    try {
        int work = 0;
        while (buffer.size() >= MediaFrameHeader::WireSize && ++work <= 64) {
            const auto header = MediaFrameHeader::parse(buffer);
            const auto frameSize =
                MediaFrameHeader::WireSize + header.payloadLength;
            if (buffer.size() < frameSize) {
                return;   // Wait for a fragmented payload.
            }
            const auto body =
                buffer.mid(MediaFrameHeader::WireSize, header.payloadLength);
            buffer.remove(0, frameSize);
            const auto type = MediaControl(header.controlType);
            if (phase == Health || phase == Initialize || phase == PostHealth) {
                if (type != (phase == Initialize
                                 ? MediaControl::InitializeResult
                                 : MediaControl::HealthResult))
                {
                    throw Error("Unexpected media handshake response");
                }
                if (phase == Health) {
                    if (probeOnly) {
                        QStringList states;
                        if (!body.isEmpty() && quint8(body[0]) > 0 &&
                            body.size() > quint8(body[0]))
                        {
                            for (int i = 1; i <= quint8(body[0]); ++i) {
                                states.append(QString("Slot %1: %2")
                                                  .arg(i)
                                                  .arg(
                                                      quint8(body[i]) == 0xff
                                                          ? "empty"
                                                          : "occupied"));
                            }
                        }
                        stop(
                            states.isEmpty() ? "Media slot status unavailable"
                                             : states.join("; "));
                        return;
                    }
                    if (body.size() >= 3 && quint8(body[0]) >= 2 &&
                        quint8(body[2]) != 0xff)
                    {
                        throw Error(
                            "Virtual CD-ROM slot is occupied by another "
                            "client");
                    }
                    phase = Initialize;
                    openChannel();
                } else if (phase == Initialize) {
                    phase = DataConnect;
                    openChannel();
                } else {
                    channel->disconnect(this);
                    channel->abort();
                    channel->deleteLater();
                    channel = data;
                    buffer.clear();
                    phase = Attach;
                    send(mediaAttach(
                        launch.user,
                        launch.password,
                        QRandomGenerator::global()->generate()));
                }
                return;
            }
            if (phase == Attach) {
                if (type != MediaControl::AttachResult || body.isEmpty() ||
                    body[0] != 0)
                {
                    throw Error(
                        "BMC refused ISO attachment (slot busy or expired "
                        "token)");
                }
                send(mediaControl(
                    MediaControl::ConfigureDevice,
                    mediaDeviceConfiguration()));
                if (!channel) {
                    return;
                }
                phase = Serving;
                deadline.stop();
                connect(
                    channel,
                    &QSslSocket::bytesWritten,
                    this,
                    [this]
                    {
                        if (phase == Serving && channel &&
                            !channel->bytesToWrite())
                        {
                            deadline.stop();
                        }
                    });
                keepalive.start();
                message = "Mounted: " + isoPath;
                emit diagnostic(message);
                emit changed();
            } else if (
                type == MediaControl::Disconnected ||
                type == MediaControl::DetachResult)
            {
                stop("Media unmounted by BMC");
                return;
            } else if (header.scsiCommand && phase == Serving) {
                const auto command = UsbCommandBlock::parse(body);
                const auto result = cd->execute(
                    command.commandDescriptor,
                    command.dataIn ? command.transferLength : 0);
                if (command.dataIn && !result.data.isEmpty()) {
                    send(
                        mediaDataFrame(header.logicalUnit, result.data, false));
                }
                send(mediaDataFrame(
                    header.logicalUnit,
                    command.status(quint32(result.data.size()), result.status),
                    true));
            }
            if (!channel) {
                return;
            }
        }
        if (work > 64) {
            QTimer::singleShot(0, this, &Media::consume);
        }
    } catch (const std::exception& e) {
        stop(QString::fromUtf8(e.what()));
    }
}
}   // namespace aten
