#include "protocol.h"

#include <QXmlStreamReader>
#include <QtEndian>

namespace aten {
static void require(const QByteArray& bytes, qsizetype offset, qsizetype length)
{
    if (offset < 0 || offset > bytes.size() || length > bytes.size() - offset) {
        throw Error("Truncated protocol record");
    }
}

quint16 be16(const QByteArray& bytes, qsizetype offset)
{
    require(bytes, offset, 2);
    return qFromBigEndian<quint16>(bytes.constData() + offset);
}

quint32 be32(const QByteArray& bytes, qsizetype offset)
{
    require(bytes, offset, 4);
    return qFromBigEndian<quint32>(bytes.constData() + offset);
}

void put16(QByteArray& bytes, qsizetype offset, quint16 value)
{
    require(bytes, offset, 2);
    qToBigEndian(value, bytes.data() + offset);
}

void put32(QByteArray& bytes, qsizetype offset, quint32 value)
{
    require(bytes, offset, 4);
    qToBigEndian(value, bytes.data() + offset);
}

quint32 little32(const QByteArray& bytes, qsizetype offset)
{
    require(bytes, offset, 4);
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}

void putLittle32(QByteArray& bytes, qsizetype offset, quint32 value)
{
    require(bytes, offset, 4);
    qToLittleEndian(value, bytes.data() + offset);
}

FramebufferRectangle FramebufferRectangle::parse(
    const QByteArray& bytes,
    qsizetype offset)
{
    // ATEN extends the RFB rectangle with a reserved word and payload length.
    require(bytes, offset, WireSize);
    return {
        be16(bytes, offset),
        be16(bytes, offset + 2),
        be16(bytes, offset + 4),
        be16(bytes, offset + 6),
        be32(bytes, offset + 8),
        be32(bytes, offset + 16)};
}

QByteArray consoleHeartbeat()
{
    QByteArray request(9, 0);
    request[0] = 0x15;
    put32(
        request,
        1,
        1);   // Existing ATEN keepalive value; trailing word reserved.
    return request;
}

QByteArray consoleStartup()
{
    // Existing startup sequence: power query, two 0x07 commands, then the
    // vendor 0x80/0x36 setup bytes. Retained as a documented compatibility
    // record.
    return QByteArray::fromHex("37070780360001");
}

QByteArray displaySettings(int quality, int chromaMode)
{
    QByteArray request(5, 0);
    request[0] = 0x32;
    // Byte 1 is reserved; quality is a byte; chroma preference is BE16.
    request[2] = char(quality);
    put16(request, 3, quint16(chromaMode));
    return request;
}

QByteArray keyEvent(quint32 usage, bool down)
{
    // ATEN key record: message type, reserved byte, down flag, two reserved
    // bytes, BE32 USB HID usage, then nine reserved bytes.
    QByteArray bytes(18, 0);
    bytes[0] = 4;
    bytes[2] = down ? 1 : 0;
    put32(bytes, 5, usage);
    return bytes;
}

QByteArray pointerEvent(int x, int y, int buttons)
{
    // Absolute pointer record: button mask at 2, BE16 x/y at 3/5;
    // remaining bytes are reserved. Wheel motion is encoded in the mask.
    QByteArray bytes(18, 0);
    bytes[0] = 5;
    bytes[2] = char(buttons);
    put16(bytes, 3, quint16(x));
    put16(bytes, 5, quint16(y));
    return bytes;
}

QByteArray updateRequest(QSize size, bool incremental)
{
    // RFB update request: incremental flag, x/y origin (zero), BE16
    // width/height.
    QByteArray bytes(10, 0);
    bytes[0] = 3;
    bytes[1] = incremental ? 1 : 0;
    put16(bytes, 6, quint16(size.width()));
    put16(bytes, 8, quint16(size.height()));
    return bytes;
}

QByteArray credentials(const QString& user, const QString& password)
{
    // Two fixed 24-byte ASCII credential fields, padded with zeroes.
    auto field = [](const QString& text)
    {
        if (text.isEmpty() || text.size() > 24) {
            throw Error(
                "KVM credentials must contain 1 to 24 ASCII characters");
        }
        for (auto character: text) {
            if (character.unicode() < 32 || character.unicode() > 126) {
                throw Error("KVM credentials must be ASCII");
            }
        }
        auto bytes = text.toLatin1();
        bytes.append(QByteArray(24 - bytes.size(), 0));
        return bytes;
    };
    return field(user) + field(password);
}

Launch parseJnlp(const QByteArray& xml)
{
    if (xml.size() > 1024LL * 1024) {
        throw Error("JNLP is too large");
    }
    QXmlStreamReader reader(xml);
    QStringList args;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isDTD()) {
            throw Error("JNLP document types are not supported");
        }
        if (reader.isStartElement() && reader.name() == u"argument") {
            args << reader.readElementText().trimmed();
        }
    }
    if (reader.hasError() || args.size() < 10) {
        throw Error("Invalid ATEN launch JNLP");
    }
    if (args[8] != "0" && args[8] != "1") {
        throw Error("Invalid JNLP TLS flag");
    }
    Launch launch;
    launch.host = args[0];
    launch.user = args[1];
    launch.password = args[2];
    launch.tls = args[8] == "1";
    bool ok = false;
    int port = args[launch.tls ? 9 : 4].toInt(&ok);
    if (!ok || port < 1 || port > 65535 || launch.host.isEmpty()) {
        throw Error("Invalid JNLP endpoint");
    }
    launch.port = quint16(port);
    const int mediaIndex = launch.tls ? 10 : 5;
    if (args.size() > mediaIndex) {
        int media = args[mediaIndex].toInt();
        if (media > 0 && media <= 65535) {
            launch.mediaPort = quint16(media);
        }
    }
    credentials(launch.user, launch.password);
    return launch;
}
}   // namespace aten
