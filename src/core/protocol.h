#pragma once
#include <QByteArray>
#include <QSize>
#include <QString>

#include <stdexcept>

namespace aten {
struct Error: std::runtime_error
{
    using std::runtime_error::runtime_error;
};

quint16 be16(const QByteArray& bytes, qsizetype offset);
quint32 be32(const QByteArray& bytes, qsizetype offset);
void put16(QByteArray& bytes, qsizetype offset, quint16 value);
void put32(QByteArray& bytes, qsizetype offset, quint32 value);
// The USB virtual-media wrapper uses little-endian fields; console and SCSI
// fields are big-endian. All helpers check bounds before accessing a field.
quint32 little32(const QByteArray& bytes, qsizetype offset);
void putLittle32(QByteArray& bytes, qsizetype offset, quint32 value);
enum class ServerMessageType : quint8
{
    FramebufferUpdate = 0,
    Cursor = 4,
    Keepalive = 0x16,
    Reserved35 = 0x35,   // Existing 6-byte vendor message; payload is ignored.
    PowerState = 0x37,
    UserNotice = 0x39,
    Reserved3c = 0x3c   // Existing 9-byte vendor message; payload is ignored.
};

struct FramebufferRectangle
{
    static constexpr qsizetype WireSize = 20;
    int x, y, width, height;
    quint32 encoding, payloadLength;
    static FramebufferRectangle parse(
        const QByteArray& bytes,
        qsizetype offset);
};

QByteArray consoleHeartbeat();
QByteArray consoleStartup();
QByteArray displaySettings(int quality, int chromaMode);
QByteArray keyEvent(quint32 usage, bool down);
QByteArray pointerEvent(int x, int y, int buttons);
QByteArray updateRequest(QSize size, bool incremental);
QByteArray credentials(const QString& user, const QString& password);

struct Launch
{
    QString host, user, password;
    quint16 port = 5900, mediaPort = 623;
    bool tls = true;
};

Launch parseJnlp(const QByteArray& xml);
}   // namespace aten
