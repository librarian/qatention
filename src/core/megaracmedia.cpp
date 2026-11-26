#include "media.h"

#include <QFileInfo>
#include <QNetworkRequest>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>
#include <QtEndian>

namespace aten {
namespace {

constexpr qsizetype IusbHeaderSize = 32, ScsiHeaderSize = 61;
constexpr qsizetype MaxReplySize = 128LL * 1024;
constexpr qsizetype MaxRequestSize = 1024;
constexpr quint8 AttachAck = 0xf1, Authenticate = 0xf2, KeepAlive = 0xf3,
                 Ejected = 0xf6, Disconnect = 0xf7, ImageInfo = 0xf8;
constexpr auto Signature = QByteArrayView("IUSB    ");

QByteArray mediaPacket(quint8 command, quint8 instance, qsizetype size)
{
    QByteArray packet(size, 0);
    packet.replace(0, Signature.size(), Signature);
    packet[8] = 1;
    packet[10] = char(IusbHeaderSize);
    packet[17] = 5;   // CD-ROM
    packet[18] = 1;   // SCSI
    packet[23] = char(instance);
    packet[41] = char(command);
    return packet;
}

quint32 scsiAllocation(const QByteArray& cdb)
{
    switch (ScsiOpcode(quint8(cdb[0]))) {
        case ScsiOpcode::ReadCapacity:
            return 8;
        case ScsiOpcode::Read10:
        case ScsiOpcode::Read12:
            return quint32(MaxReplySize);
        case ScsiOpcode::RequestSense:
        case ScsiOpcode::Inquiry:
        case ScsiOpcode::ModeSense6:
            return quint8(cdb[4]);
        case ScsiOpcode::ModeSense10:
        case ScsiOpcode::ReadToc:
        case ScsiOpcode::GetConfiguration:
        case ScsiOpcode::GetEventStatus:
        case ScsiOpcode::ReadDiscInformation:
        case ScsiOpcode::ReadTrackInformation:
            return be16(cdb, 7);
        default:
            return 0;
    }
}

}   // namespace

void Media::mountMegaRac(
    const QNetworkRequest& request,
    const QByteArray& token,
    const QString& path)
{
    if (busy()) {
        return;
    }
    try {
        if (request.url().scheme() != "wss" || token.isEmpty() ||
            token.contains('\0') || token.size() > 98)
        {
            throw Error("Invalid MegaRAC media credentials");
        }
        cd = std::make_unique<CdRom>(path);
        isoPath = path;
        phase = Attach;
        message = "Attaching ISO…";
        mediaSocket = new QWebSocket(
            QString::fromUtf8(request.rawHeader("Origin")),
            QWebSocketProtocol::VersionLatest,
            this);
        auto upgrade = request;
        upgrade.setRawHeader("Origin", {});
        mediaSocket->setSslConfiguration(request.sslConfiguration());
        mediaSocket->setMaxAllowedIncomingFrameSize(MaxRequestSize);
        mediaSocket->setMaxAllowedIncomingMessageSize(MaxRequestSize);
        connect(
            mediaSocket,
            &QWebSocket::connected,
            this,
            [this, token]
            {
                // The H5Viewer uses a web authentication record and then
                // provides the drive's display name as an independent IUSB
                // record.
                auto auth = mediaPacket(Authenticate, 0, 193);
                auth.replace(63, token.size(), token);
                sendMegaRac(auth);
                auto info = mediaPacket(ImageInfo, 0, 291);
                qToLittleEndian(quint32(3), info.data() + 62);   // HTML5 client
                const auto name =
                    QFileInfo(isoPath).fileName().toUtf8().left(224);
                info.replace(66, name.size(), name);
                sendMegaRac(info);
            });
        connect(
            mediaSocket,
            &QWebSocket::binaryMessageReceived,
            this,
            &Media::receiveMegaRac);
        connect(
            mediaSocket,
            &QWebSocket::textMessageReceived,
            this,
            [this](const QString& message)
            {
                const auto decoded = QByteArray::fromBase64Encoding(
                    message.toLatin1(),
                    QByteArray::AbortOnBase64DecodingErrors);
                if (!decoded) {
                    stop("Invalid MegaRAC media WebSocket message");
                    return;
                }
                receiveMegaRac(decoded.decoded);
            });
        connect(
            mediaSocket,
            &QWebSocket::errorOccurred,
            this,
            [this](QAbstractSocket::SocketError error)
            {
                if (phase == Detach &&
                    error == QAbstractSocket::RemoteHostClosedError)
                {
                    stop("Media unmounted");
                } else {
                    stop(
                        "MegaRAC media connection failed: " +
                        mediaSocket->errorString());
                }
            });
        connect(
            mediaSocket,
            &QWebSocket::disconnected,
            this,
            [this]
            {
                stop(
                    phase == Detach ? "Media unmounted"
                                    : "MegaRAC media disconnected");
            });
        QWebSocketHandshakeOptions options;
        options.setSubprotocols({"binary", "base64"});
        deadline.start(15000);
        mediaSocket->open(upgrade, options);
        emit changed();
    } catch (const std::exception& error) {
        stop(QString::fromUtf8(error.what()));
    }
}

void Media::sendMegaRac(const QByteArray& packet)
{
    if (!mediaSocket || mediaSocket->state() != QAbstractSocket::ConnectedState)
    {
        return;
    }
    if (mediaSocket->bytesToWrite() > 1024LL * 1024) {
        stop("MegaRAC stopped reading virtual media");
        return;
    }
    QByteArray response = packet;
    response[19] = char(0x80);   // Client to BMC.
    response[11] = 0;
    qToLittleEndian(
        quint32(response.size() - IusbHeaderSize),
        response.data() + 12);
    quint8 checksum = 0;
    for (const auto byte: response) {
        checksum = quint8(checksum + quint8(byte));
    }
    response[11] = char(-checksum);
    if (mediaSocket->subprotocol() == "binary") {
        mediaSocket->sendBinaryMessage(response);
    } else {
        mediaSocket->sendTextMessage(QString::fromLatin1(response.toBase64()));
    }
}

void Media::receiveMegaRac(const QByteArray& bytes)
{
    buffer += bytes;
    if (buffer.size() > 2 * MaxRequestSize) {
        stop("MegaRAC media receive limit exceeded");
        return;
    }
    try {
        while (buffer.size() >= IusbHeaderSize) {
            // The ASUS CD server sends the same fixed header with the
            // version and header-size bytes zeroed in its replies.
            const bool fixedHeader =
                buffer[8] == 1 && buffer[10] == IusbHeaderSize;
            const bool implicitHeader = buffer[8] == 0 && buffer[10] == 0;
            if (!buffer.startsWith(Signature) ||
                (!fixedHeader && !implicitHeader))
            {
                throw Error("Invalid MegaRAC media header");
            }
            const auto payload =
                qFromLittleEndian<quint32>(buffer.constData() + 12);
            if (payload < ScsiHeaderSize - IusbHeaderSize ||
                payload > MaxRequestSize - IusbHeaderSize)
            {
                throw Error("Invalid MegaRAC media request length");
            }
            const auto size = IusbHeaderSize + payload;
            if (buffer.size() < size) {
                return;
            }
            const QByteArray packet = buffer.first(size);
            buffer.remove(0, size);
            handleMegaRac(packet);
            if (!mediaSocket) {
                return;
            }
        }
    } catch (const std::exception& error) {
        stop(QString::fromUtf8(error.what()));
    }
}

void Media::handleMegaRac(QByteArrayView packet)
{
    const auto command = quint8(packet[41]);
    if (command == AttachAck && phase == Attach) {
        if (packet.size() < 63) {
            throw Error("Truncated MegaRAC media response");
        }
        switch (quint8(packet[62])) {
            case 1:
            case 27:
            case 28:
                break;
            case 3:
                throw Error("MegaRAC rejected the media token");
            case 4:
                throw Error("MegaRAC CD drive is occupied");
            case 5:
                throw Error("MegaRAC denied virtual media permission");
            case 8:
                throw Error("MegaRAC virtual media session limit reached");
            case 9:
                throw Error("MegaRAC virtual media service is unavailable");
            default:
                throw Error("MegaRAC refused the virtual media session");
        }
        megaRacInstance = quint8(packet[23]);
        phase = Serving;
        deadline.stop();
        message = "Mounted: " + isoPath;
        emit diagnostic(message);
        emit changed();
        return;
    }
    if (command == Ejected || command == Disconnect) {
        stop("Media unmounted by BMC");
        return;
    }
    if (phase != Serving) {
        return;
    }
    if (command == KeepAlive) {
        sendMegaRac(mediaPacket(KeepAlive, megaRacInstance, ScsiHeaderSize));
        return;
    }
    QByteArray cdb(16, 0);
    cdb.replace(0, 12, packet.sliced(41, 12));
    if (command == quint8(ScsiOpcode::Read12)) {
        // AMI sends READ(12)'s sector count in little-endian order. The
        // shared CD-ROM handler expects a standard big-endian SCSI CDB.
        put32(cdb, 6, qFromLittleEndian<quint32>(cdb.constData() + 6));
    }
    auto allocation = scsiAllocation(cdb);
    const auto transferLength = qFromLittleEndian<quint32>(packet.data() + 32);
    if (transferLength != 0) {
        allocation = qMin(allocation, transferLength);
    }
    const auto result = cd->execute(cdb, allocation);
    QByteArray response = packet.first(ScsiHeaderSize).toByteArray();
    const auto sense = result.status == 0 ? ScsiSense{} : cd->lastSense();
    response[53] = char(result.status != 0);
    response[54] = char(sense.key);
    response[55] = char(sense.additional);
    response[56] = 0;
    qToLittleEndian(quint32(result.data.size()), response.data() + 57);
    response.append(result.data);
    if (command == quint8(ScsiOpcode::ModeSense6) && result.data.size() >= 3) {
        response[ScsiHeaderSize + 2] =
            char(quint8(response[ScsiHeaderSize + 2]) | 0x80U);
    } else if (
        command == quint8(ScsiOpcode::ModeSense10) && result.data.size() >= 4)
    {
        response[ScsiHeaderSize + 3] =
            char(quint8(response[ScsiHeaderSize + 3]) | 0x80U);
    }
    sendMegaRac(response);
    if (mediaSocket && command == quint8(ScsiOpcode::StartStopUnit) &&
        (quint32(quint8(cdb[4])) & 3U) == 2U)
    {
        unmount();
    }
}

void Media::detachMegaRac()
{
    sendMegaRac(mediaPacket(Disconnect, megaRacInstance, ScsiHeaderSize));
}

}   // namespace aten
