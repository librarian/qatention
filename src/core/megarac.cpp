#include "megarac.h"

#include "protocol.h"

#include <QNetworkRequest>
#include <QWebSocketHandshakeOptions>
#include <QtEndian>

#include <algorithm>

namespace aten {
namespace {

constexpr qsizetype HeaderSize = 8;
constexpr qsizetype MaxPayload = 16LL * 1024 * 1024;
constexpr qsizetype VideoHeaderSize = 86;

enum Command : quint16
{
    Hid = 1,
    Pause = 4,
    Resume = 6,
    Stop = 8,
    Blank = 9,
    MouseMode = 10,
    FullScreen = 11,
    Validate = 18,
    Validated = 19,
    WebToken = 21,
    SessionLimit = 22,
    ConnectionAllowed = 23,
    Video = 25,
    Sharing = 32,
    ActiveClients = 39,
    NextMaster = 50,
    KeepAlive = 57,
    ConnectionComplete = 58,
};

QByteArray packet(quint16 type, quint16 status, QByteArrayView payload = {})
{
    QByteArray bytes(HeaderSize, 0);
    qToLittleEndian(type, bytes.data());
    qToLittleEndian(quint32(payload.size()), bytes.data() + 2);
    qToLittleEndian(status, bytes.data() + 6);
    bytes.append(payload.data(), payload.size());
    return bytes;
}

QByteArray field(const QString& value, qsizetype size)
{
    const auto bytes = value.toLatin1();
    if (bytes.size() >= size || bytes.contains('\0')) {
        throw Error("Invalid MegaRAC session field");
    }
    QByteArray result(size, 0);
    std::copy(bytes.cbegin(), bytes.cend(), result.begin());
    return result;
}

}   // namespace

MegaRac::MegaRac(const QString& origin, QObject* parent)
    : QObject(parent)
    , socket(origin)
{
    socket.setMaxAllowedIncomingFrameSize(MaxPayload);
    socket.setMaxAllowedIncomingMessageSize(MaxPayload);
    connect(
        &socket,
        &QWebSocket::binaryMessageReceived,
        this,
        &MegaRac::receive);
    connect(
        &socket,
        &QWebSocket::textMessageReceived,
        this,
        [this](const QString& text)
        {
            const auto decoded = QByteArray::fromBase64Encoding(
                text.toLatin1(),
                QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) {
                emit failed("Invalid MegaRAC WebSocket message", false);
                return;
            }
            receive(decoded.decoded);
        });
    connect(
        &socket,
        &QWebSocket::errorOccurred,
        this,
        [this](QAbstractSocket::SocketError error)
        {
            emit failed(
                "MegaRAC console connection failed: " + socket.errorString(),
                error != QAbstractSocket::SslHandshakeFailedError);
        });
    connect(
        &socket,
        &QWebSocket::disconnected,
        this,
        [this] { emit failed("MegaRAC console disconnected", true); });
}

void MegaRac::open(const QNetworkRequest& request, const QJsonObject& settings)
{
    config = settings;
    socket.setSslConfiguration(request.sslConfiguration());
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({"binary", "base64"});
    socket.open(request, options);
}

void MegaRac::close()
{
    releaseInput();
    if (socket.state() == QAbstractSocket::ConnectedState) {
        send(Stop);
        socket.close();
    } else {
        socket.abort();
    }
}

void MegaRac::send(quint16 type, quint16 status, QByteArrayView payload)
{
    if (socket.state() == QAbstractSocket::ConnectedState) {
        if (socket.bytesToWrite() > 1024LL * 1024) {
            emit failed("MegaRAC stopped accepting input", true);
            return;
        }
        socket.sendBinaryMessage(packet(type, status, payload));
    }
}

void MegaRac::receive(const QByteArray& bytes)
{
    if (input.size() + bytes.size() > MaxPayload + HeaderSize) {
        emit failed("MegaRAC packet exceeds receive limit", false);
        return;
    }
    input.append(bytes);
    qsizetype offset = 0;
    try {
        while (input.size() - offset >= HeaderSize) {
            const auto* header = input.constData() + offset;
            const auto size = qFromLittleEndian<quint32>(header + 2);
            if (size > MaxPayload) {
                throw Error("MegaRAC packet exceeds receive limit");
            }
            if (input.size() - offset < HeaderSize + size) {
                break;
            }
            handle(
                qFromLittleEndian<quint16>(header),
                qFromLittleEndian<quint16>(header + 6),
                QByteArrayView(header + HeaderSize, size));
            offset += HeaderSize + size;
            if (socket.state() != QAbstractSocket::ConnectedState) {
                return;
            }
        }
        input.remove(0, offset);
        emit activity();
    } catch (const std::exception& error) {
        emit failed(QString::fromUtf8(error.what()), false);
    }
}

void MegaRac::validate()
{
    if (validationSent || config.value("token").toString().isEmpty()) {
        throw Error("Invalid MegaRAC console token");
    }
    const QByteArray body =
        QByteArray(1, 0) + field(config.value("token").toString(), 129) +
        field(config.value("client_ip").toString("No IP"), 65) +
        field("domain/username", 129) + field("00-00-00-00-00-00", 49) +
        field(config.value("server_ip").toString(), 65);
    validationSent = true;
    socket.sendBinaryMessage(
        packet(ConnectionComplete, 1) + packet(Validate, 1, body) +
        packet(Resume, 0));
}

void MegaRac::handle(quint16 type, quint16 status, QByteArrayView payload)
{
    switch (type) {
        case ConnectionAllowed:
            validate();
            return;
        case Validated:
            if (!validationSent || validated || payload.isEmpty() ||
                payload[0] != 1)
            {
                throw Error("MegaRAC rejected the console session");
            }
            validated = true;
            send(WebToken, 0, field(config.value("session").toString(), 35));
            config = {};
            emit ready(false);
            refresh();
            return;
        case SessionLimit:
            throw Error("MegaRAC console session limit reached");
        case Stop:
            throw Error("MegaRAC stopped the console session");
        case KeepAlive:
            send(KeepAlive);
            return;
        default:
            break;
    }
    if (!validated) {
        return;
    }
    switch (type) {
        case ActiveClients:
            refresh();
            break;
        case NextMaster:
        case Sharing: {
            const auto step = status & 0xffU;
            if (step == 4 || step == 6) {
                setControl((status >> 8U) == 0);
            } else if (step == 2) {
                setControl(false);
            }
            break;
        }
        case MouseMode:
            if (!payload.isEmpty()) {
                mouseMode = quint8(payload[0]);
            }
            break;
        case Video:
            video(payload);
            break;
        case Blank:
            compressed.clear();
            compressedSize = 0;
            emit frameReady({});
            break;
        default:
            break;
    }
}

void MegaRac::video(QByteArrayView payload)
{
    if (payload.size() < 2) {
        throw Error("Truncated MegaRAC video packet");
    }
    payload = payload.sliced(2);
    if (compressedSize == 0) {
        if (payload.size() < VideoHeaderSize) {
            throw Error("Truncated MegaRAC video header");
        }
        const char* header = payload.data();
        frameSize = {
            qFromLittleEndian<quint16>(header + 4),
            qFromLittleEndian<quint16>(header + 6)};
        quality = quint8(header[44]);
        advancedQuality = quint8(header[47]);
        chroma420 = header[55] != 0;
        compressedSize = qFromLittleEndian<quint32>(header + 69);
        if (header[53] != 0) {
            throw Error("Encrypted MegaRAC video is unsupported");
        }
        if (frameSize.isEmpty() || frameSize.width() > 1920 ||
            frameSize.height() > 1200 || quality > 11 || advancedQuality > 11 ||
            compressedSize < 8 || compressedSize > MaxPayload)
        {
            throw Error("Unsupported MegaRAC video frame format");
        }
        payload = payload.sliced(VideoHeaderSize);
    }
    const auto wanted =
        qMin(payload.size(), compressedSize - compressed.size());
    compressed.append(payload.data(), wanted);
    if (compressed.size() == compressedSize) {
        if (decoder.image().size() != frameSize) {
            decoder.resize(frameSize);
        }
        if (decoder
                .decodeAspeed(compressed, chroma420, quality, advancedQuality))
        {
            emit frameReady(decoder.image());
        }
        compressed.clear();
        compressedSize = 0;
    }
}

void MegaRac::refresh()
{
    if (validated && streaming) {
        send(FullScreen, 1);
    }
}

void MegaRac::setStreaming(bool active)
{
    if (streaming == active) {
        return;
    }
    streaming = active;
    if (validated) {
        send(active ? Resume : Pause);
        refresh();
    }
}

void MegaRac::setControl(bool allowed)
{
    if (controlling != allowed) {
        if (!allowed) {
            held.clear();
            mousePressed = false;
            mousePosition.reset();
        }
        controlling = allowed;
        emit ready(controlling);
    }
}

void MegaRac::hid(
    quint8 device,
    quint8 protocol,
    quint8 interfaceNumber,
    QByteArrayView report)
{
    QByteArray data(33, 0);
    std::copy_n("IUSB    ", 8, data.begin());
    data[8] = 1;
    data[10] = 32;
    qToLittleEndian(quint32(1 + report.size()), data.data() + 12);
    data[17] = char(device);
    data[18] = char(protocol);
    data[19] = char(0x80);
    data[20] = 2;
    data[21] = char(interfaceNumber);
    qToLittleEndian(++sequence, data.data() + 24);
    quint8 checksum = 0;
    for (const auto byte: QByteArrayView(data).first(32)) {
        checksum = quint8(checksum + quint8(byte));
    }
    data[11] = char(-checksum);
    data[32] = char(report.size());
    data.append(report.data(), report.size());
    send(Hid, 0, data);
}

void MegaRac::keyboard()
{
    QByteArray report(8, 0);
    qsizetype slot = 2;
    auto keys = held.values();
    std::sort(keys.begin(), keys.end());
    for (const auto usage: keys) {
        if (usage >= 0xe0 && usage <= 0xe7) {
            report[0] = char(quint8(report[0]) | (1U << (usage - 0xe0)));
        } else if (slot < report.size() && usage < 0xe0) {
            report[slot++] = char(usage);
        }
    }
    hid(0x30, 0x10, 0, report);
}

void MegaRac::key(quint32 usage, bool down)
{
    if (!controlling) {
        return;
    }
    if (down) {
        held.insert(usage);
    } else {
        held.remove(usage);
    }
    keyboard();
}

void MegaRac::pointer(int x, int y, unsigned mask)
{
    if (!controlling) {
        return;
    }
    const auto buttons =
        (mask & 1U) | ((mask & 2U) << 1U) | ((mask & 4U) >> 1U);
    const auto wheel = ((mask & 8U) != 0) - ((mask & 16U) != 0);
    const QPoint point(
        qBound(0, x, size().width() - 1),
        qBound(0, y, size().height() - 1));
    QByteArray report(mouseMode == AbsoluteMouse ? 6 : 4, 0);
    report[0] = char(buttons);
    if (mouseMode == AbsoluteMouse) {
        const auto scale = [](int value, int extent)
        {
            return quint16(value * 32767 / qMax(1, extent - 1));
        };
        qToLittleEndian(scale(point.x(), size().width()), report.data() + 1);
        qToLittleEndian(scale(point.y(), size().height()), report.data() + 3);
    } else {
        const auto delta = point - mousePosition.value_or(point);
        report[1] = char(qBound(-127, delta.x(), 127));
        report[2] = char(qBound(-127, delta.y(), 127));
    }
    report.back() = char(wheel);
    mousePosition = point;
    mousePressed = buttons != 0;
    hid(0x31, 0x20, 1, report);
}

void MegaRac::releaseInput()
{
    if (!held.isEmpty()) {
        held.clear();
        keyboard();
    }
    if (mousePressed && mousePosition) {
        pointer(mousePosition->x(), mousePosition->y(), 0);
    }
}

}   // namespace aten
