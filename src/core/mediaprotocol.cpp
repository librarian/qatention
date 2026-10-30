#include "mediaprotocol.h"

#include <QMessageAuthenticationCode>

namespace aten {
MediaFrameHeader MediaFrameHeader::parse(const QByteArray& bytes)
{
    // The common size check precedes all direct byte indexing.
    const auto control = be32(bytes, 0);
    const auto length = little32(bytes, 4);
    const bool command = quint8(bytes[0]) == 0x11;
    if (length > MaxPayload || (command && length != UsbCommandBlock::WireSize))
    {
        throw Error("Invalid virtual-media frame");
    }
    return {control, length, quint8(bytes[2]), command};
}

UsbCommandBlock UsbCommandBlock::parse(const QByteArray& bytes)
{
    if (bytes.size() != WireSize || bytes.first(4) != "USBC") {
        throw Error("Invalid USB command block signature or length");
    }
    // USB bulk-only CBW: signature[4], LE32 tag, LE32 transfer length,
    // direction[1], LUN[1], command length[1], padded SCSI descriptor[16].
    const auto commandLength = quint8(bytes[14]) & 31U;
    if (commandLength == 0 || commandLength > 16) {
        throw Error("Invalid SCSI command length");
    }
    return {
        little32(bytes, 4),
        little32(bytes, 8),
        (quint8(bytes[12]) & 0x80U) != 0,
        bytes.mid(15, 16)};
}

QByteArray UsbCommandBlock::status(quint32 transferred, quint8 result) const
{
    if (transferred > transferLength) {
        throw Error("USB transfer exceeds allocation");
    }
    // USB bulk-only CSW: signature, echoed tag, untransferred byte count,
    // then the SCSI completion status (0 = good, 1 = command failed).
    QByteArray response(13, 0);
    response.replace(0, 4, "USBS");
    putLittle32(response, 4, tag);
    putLittle32(response, 8, transferLength - transferred);
    response[12] = char(result);
    return response;
}

QByteArray
mediaDataFrame(quint8 logicalUnit, const QByteArray& payload, bool status)
{
    QByteArray header(MediaFrameHeader::WireSize, 0);
    header[0] = 0x22;   // Device-to-host data envelope.
    header[2] = char(logicalUnit);
    header[3] = status ? char(0xff) : 0;   // CSW versus data payload.
    putLittle32(header, 4, quint32(payload.size()));
    return header + payload;
}

QByteArray mediaDeviceConfiguration()
{
    // Existing vendor device-configuration payload sent after attachment.
    // Keep these opaque capability bytes stable; interpretation is not needed
    // to serve read-only SCSI commands.
    return QByteArray::fromHex("0503011002200330");
}

QByteArray mediaControl(quint32 type, const QByteArray& payload)
{
    // ATEN control envelope: BE32 command followed by LE32 payload length.
    QByteArray packet(MediaFrameHeader::WireSize, 0);
    put32(packet, 0, type);
    putLittle32(packet, 4, quint32(payload.size()));
    return packet + payload;
}

QByteArray
mediaAttach(const QString& user, const QString& password, quint32 timestamp)
{
    for (const auto& token: {user, password}) {
        if (token.isEmpty() || token.size() > 15) {
            throw Error("Media tokens must contain 1 to 15 ASCII characters");
        }
        for (auto character: token) {
            if (character.unicode() < 32 || character.unicode() > 126) {
                throw Error("Media tokens must be ASCII");
            }
        }
    }
    // Vendor attachment record (246 bytes). Reserved fields stay zero.
    // Credentials occupy 16-byte NUL-padded fields; the timestamp is LE32.
    // Bytes 52..225 are the existing USB descriptor block. The final 20 bytes
    // authenticate bytes 0..225 with HMAC-SHA1 using user+password as the key.
    QByteArray packet(246, 0);
    packet[1] = char(0x80);
    packet[3] = 1;
    packet[4] = 0x2c;
    packet.replace(8, user.size(), user.toLatin1());
    packet.replace(24, password.size(), password.toLatin1());
    putLittle32(packet, 44, timestamp);
    packet[48] = char(0x85);
    packet[49] = 3;
    // Opaque vendor USB descriptor sequence retained byte-for-byte from the
    // existing implementation. It includes device/configuration/endpoints and
    // UTF-16LE product/serial strings; it is not a SCSI command response.
    const auto descriptor = QByteArray::fromHex(
        "121201000200000040a00e111100020000000127090227000101008064"
        "090400000308055000070501020002ff070582020002ff0705830302000104"
        "0403090422220346006c0061007300680020004400690073006b0020002000"
        "20002000200020002222033400450038004600300039003200430033004600"
        "4400370046003800460037001a1a0353004e00300030003000500051004900"
        "300030003900200001000a0a060002000000400100");
    packet.replace(52, descriptor.size(), descriptor);
    packet.replace(
        226,
        20,
        QMessageAuthenticationCode::hash(
            packet.first(226),
            (user + password).toLatin1(),
            QCryptographicHash::Sha1));
    return packet;
}

}   // namespace aten
