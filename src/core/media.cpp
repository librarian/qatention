#include "media.h"

#include <QCoreApplication>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSslKey>
#include <QtEndian>

namespace aten {
static quint32 le32(const QByteArray& b, int p)
{
    return qFromLittleEndian<quint32>(b.constData() + p);
}

static void putLe(QByteArray& b, int p, quint32 v)
{
    qToLittleEndian(v, b.data() + p);
}

QByteArray mediaControl(quint32 type, const QByteArray& payload)
{
    QByteArray b(8, 0);
    put32(b, 0, type);
    putLe(b, 4, quint32(payload.size()));
    return b + payload;
}

QByteArray
mediaAttach(const QString& user, const QString& password, quint32 stamp)
{
    for (const auto& s: {user, password}) {
        if (s.isEmpty() || s.size() > 15) {
            throw Error("Media tokens must contain 1 to 15 ASCII characters");
        }
        for (auto c: s) {
            if (c.unicode() < 32 || c.unicode() > 126) {
                throw Error("Media tokens must be ASCII");
            }
        }
    }
    QByteArray b(246, 0);
    b[1] = char(0x80);
    b[3] = 1;
    b[4] = 0x2c;
    b.replace(8, user.size(), user.toLatin1());
    b.replace(24, password.size(), password.toLatin1());
    putLe(b, 44, stamp);
    b[48] = char(0x85);
    b[49] = 3;
    const auto descriptor = QByteArray::fromHex(
        "121201000200000040a00e111100020000000127090227000101008064"
        "090400000308055000070501020002ff070582020002ff0705830302000104"
        "0403090422220346006c0061007300680020004400690073006b0020002000"
        "20002000200020002222033400450038004600300039003200430033004600"
        "4400370046003800460037001a1a0353004e00300030003000500051004900"
        "300030003900200001000a0a060002000000400100");
    b.replace(52, descriptor.size(), descriptor);
    b.replace(
        226,
        20,
        QMessageAuthenticationCode::hash(
            b.first(226),
            (user + password).toLatin1(),
            QCryptographicHash::Sha1));
    return b;
}

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

CdRom::CdRom(const QString& path)
    : file(path)
{
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0) {
        throw Error("Cannot open a nonempty ISO image");
    }
    if (blocks() > 0xffffffffULL) {
        throw Error("ISO exceeds supported capacity");
    }
}

ScsiResult CdRom::fail(quint8 code)
{
    sense = 5;
    asc = code;
    return {{}, 1};
}

ScsiResult CdRom::execute(const QByteArray& c, quint32 allocation)
{
    if (c.size() < 16 || allocation > 16LL * 1024 * 1024) {
        return fail(0x24);
    }
    QByteArray b;
    switch (quint8(c[0])) {
        case 0:
        case 0x1b:
        case 0x1e:
            break;
        case 3:
            b = QByteArray(18, 0);
            b[0] = 0x70;
            b[2] = sense;
            b[7] = 10;
            b[12] = asc;
            sense = asc = 0;
            break;
        case 0x12:
            b = QByteArray::fromHex("058000211f000000") +
                "IPMI    Virtual CDROM   3000";
            break;
        case 0x25:
            b = QByteArray(8, 0);
            put32(b, 0, quint32(blocks() - 1));
            put32(b, 4, 2048);
            break;
        case 0x28:
        case 0xa8: {
            const quint64 lba = be32(c, 2),
                          n = quint8(c[0]) == 0x28 ? be16(c, 7) : be32(c, 6);
            if (!n) {
                break;
            }
            if (lba + n > blocks()) {
                return fail(0x21);
            }
            const auto bytes = qsizetype(qMin(n * 2048, quint64(allocation)));
            b = QByteArray(bytes, 0);
            if (!file.seek(qint64(lba * 2048))) {
                return fail(0x21);
            }
            const auto available =
                qMin(qint64(bytes), file.size() - file.pos());
            if (file.read(b.data(), available) != available) {
                sense = 3;
                asc = 0x11;
                return {{}, 1};
            }
            break;
        }
        case 0x1a:
            b = QByteArray::fromHex("03000000");
            break;
        case 0x5a:
            b = QByteArray::fromHex(
                (c[2] & 0x3f) == 0x2a || (c[2] & 0x3f) == 0x3f
                    ? "00240100000000002a1c3f00717729231b90010000c61b9000100000"
                      "00000010000000000000"
                    : "0006010000000000");
            break;
        case 0x43: {
            int format = c[2] & 15;
            if (!format) {
                format = (c[9] >> 6) & 3;
            }
            const int start = quint8(c[6]);
            const bool single = format == 1, lead = start == 0xaa,
                       msf = c[1] & 2;
            if (!single && start > 1 && !lead) {
                return fail(0x24);
            }
            b = QByteArray(single || lead ? 12 : 20, 0);
            put16(b, 0, quint16(b.size() - 2));
            b[2] = b[3] = 1;
            auto track = [&](int at, int number, quint64 lba)
            {
                b[at + 1] = 0x14;
                b[at + 2] = char(number);
                if (msf) {
                    lba += 150;
                    b[at + 5] = char(lba / 4500);
                    b[at + 6] = char((lba / 75) % 60);
                    b[at + 7] = char(lba % 75);
                } else {
                    put32(b, at + 4, quint32(lba));
                }
            };
            if (single) {
                track(4, 1, 0);
            } else {
                if (!lead) {
                    track(4, 1, 0);
                }
                track(lead ? 4 : 12, 0xaa, blocks());
            }
            break;
        }
        case 0x46:
            b = QByteArray::fromHex(
                be16(c, 2) == 0x28 ? "0000004c00000008002a000400010000"
                : be16(c, 2) == 0x20
                    ? "00000054000000080024000400000000002a000400010000"
                    : "00000098000000080000030800100000000801000001030400000002"
                      "00020704"
                      "000000000003030429000000001001080000080000000100001d0100"
                      "001e0504"
                      "03000000001f0004000001000024000400000000002a000400010000"
                      "002b0004"
                      "00000000003b00040000000001000300010300040700010001040300"
                      "01050704"
                      "0000000001060004000000010107c00408000000010b0004");
            break;
        case 0x4a:
            b = QByteArray::fromHex("0006045e00020000");
            break;
        case 0x51:
            b = QByteArray::fromHex(
                "00200e01010101200000000000000000ffffffffffffffff00000000000000"
                "000000");
            break;
        case 0x52:
            b = QByteArray::fromHex(
                "001c0101000401000000000000000000000000000000000000000000");
            put32(b, 24, quint32(blocks()));
            break;
        default:
            return fail(0x20);
    }
    return {b.first(qMin(qsizetype(allocation), b.size())), 0};
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
        [this] {
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
                send(mediaControl(8));
            }
        });
}

Media::~Media()
{
    blockSignals(true);
    if (data) {
        data->write(mediaControl(5));
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

void Media::mount(const Launch& endpoint, bool allow, const QString& path)
{
    if (busy()) {
        return;
    }
    try {
        probeOnly = false;
        cd = std::make_unique<CdRom>(path);
        mediaAttach(endpoint.user, endpoint.password, 0);
        launch = endpoint;
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

void Media::probe(const Launch& endpoint, bool allow)
{
    if (busy()) {
        return;
    }
    try {
        probeOnly = true;
        launch = endpoint;
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
        send(mediaControl(5));
        deadline.start(1000);
    } else {
        stop("Media unmounted");
    }
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
        [this] {
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
            if (trust && channel) {
                channel->ignoreSslErrors();
            }
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
        channel->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        if (phase == Health || phase == PostHealth) {
            send(mediaControl(8));
        } else if (phase == Initialize) {
            send(mediaControl(10));
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
        while (buffer.size() >= 8 && ++work <= 64) {
            const bool command = quint8(buffer[0]) == 0x11;
            const auto n = le32(buffer, 4), type = be32(buffer, 0);
            if (n > 1024LL * 1024 || (command && n != 31)) {
                throw Error("Invalid virtual-media frame");
            }
            if (buffer.size() < 8 + n) {
                return;
            }
            const auto body = buffer.mid(8, n);
            const quint8 lun = quint8(buffer[2]);
            buffer.remove(0, 8 + n);
            if (phase == Health || phase == Initialize || phase == PostHealth) {
                if (type != (phase == Initialize ? 11u : 9u)) {
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
                if (type != 2 || body.isEmpty() || body[0] != 0) {
                    throw Error(
                        "BMC refused ISO attachment (slot busy or expired "
                        "token)");
                }
                send(mediaControl(7, QByteArray::fromHex("0503011002200330")));
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
                            !channel->bytesToWrite()) {
                            deadline.stop();
                        }
                    });
                keepalive.start();
                message = "Mounted: " + isoPath;
                emit diagnostic(message);
                emit changed();
            } else if (type == 4 || type == 6) {
                stop("Media unmounted by BMC");
                return;
            } else if (command && phase == Serving) {
                if (body.first(4) != "USBC" || (quint8(body[14]) & 31) == 0 ||
                    (quint8(body[14]) & 31) > 16)
                {
                    throw Error("Invalid USB command block");
                }
                const auto allocation = le32(body, 8);
                const bool input = quint8(body[12]) & 0x80;
                auto result =
                    cd->execute(body.mid(15, 16), input ? allocation : 0);
                auto header = [lun](quint32 size, bool status)
                {
                    QByteArray h(8, 0);
                    h[0] = 0x22;
                    h[2] = char(lun);
                    h[3] = status ? char(0xff) : 0;
                    putLe(h, 4, size);
                    return h;
                };
                if (input && !result.data.isEmpty()) {
                    send(
                        header(quint32(result.data.size()), false) +
                        result.data);
                }
                QByteArray csw("USBS");
                csw += QByteArray(9, 0);
                putLe(csw, 4, le32(body, 4));
                putLe(csw, 8, allocation - quint32(result.data.size()));
                csw[12] = char(result.status);
                send(header(13, true) + csw);
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
