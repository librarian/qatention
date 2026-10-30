#pragma once
#include "protocol.h"

namespace aten {
enum class MediaControl : quint32
{
    AttachResult = 2,
    Disconnected = 4,
    Detach = 5,
    DetachResult = 6,
    ConfigureDevice = 7,
    HealthRequest = 8,
    HealthResult = 9,
    Initialize = 10,
    InitializeResult = 11
};

// ATEN envelope fields have mixed endianness. These are decoded values, not
// packed structs: direct casts would depend on host alignment and byte order.
struct MediaFrameHeader
{
    static constexpr qsizetype WireSize = 8;
    static constexpr quint32 MaxPayload = 1024 * 1024;
    quint32 controlType;
    quint32 payloadLength;
    quint8 logicalUnit;
    bool scsiCommand;
    static MediaFrameHeader parse(const QByteArray& bytes);
};

struct UsbCommandBlock
{
    static constexpr qsizetype WireSize = 31;
    quint32 tag;
    quint32 transferLength;
    bool dataIn;
    QByteArray commandDescriptor;
    static UsbCommandBlock parse(const QByteArray& bytes);
    QByteArray status(quint32 transferred, quint8 result) const;
};

QByteArray mediaControl(quint32 type, const QByteArray& payload = {});

inline QByteArray mediaControl(
    MediaControl type,
    const QByteArray& payload = {})
{
    return mediaControl(quint32(type), payload);
}

QByteArray
mediaAttach(const QString& user, const QString& password, quint32 timestamp);
QByteArray
mediaDataFrame(quint8 logicalUnit, const QByteArray& payload, bool status);
QByteArray mediaDeviceConfiguration();
}   // namespace aten
