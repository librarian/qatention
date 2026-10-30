#pragma once
#include "protocol.h"

#include <QFile>

namespace aten {
// SCSI command descriptor blocks are big-endian, even though their USB wrapper
// uses little-endian lengths and tags. Never overlay a packed C struct on
// bytes.
enum class ScsiOpcode : quint8
{
    TestUnitReady = 0x0,
    StartStopUnit = 0x1b,
    PreventAllowRemoval = 0x1e,
    RequestSense = 0x3,
    Inquiry = 0x12,
    ReadCapacity = 0x25,
    Read10 = 0x28,
    Read12 = 0xa8,
    ModeSense6 = 0x1a,
    ModeSense10 = 0x5a,
    ReadToc = 0x43,
    GetConfiguration = 0x46,
    GetEventStatus = 0x4a,
    ReadDiscInformation = 0x51,
    ReadTrackInformation = 0x52,
};

struct ScsiReadRequest
{
    quint32 logicalBlockAddress;
    quint32 blockCount;
};

struct ScsiTocRequest
{
    unsigned format;
    quint8 startTrack;
    bool minuteSecondFrame;
};

class ScsiCommand
{
public:
    explicit ScsiCommand(const QByteArray& bytes);
    ScsiOpcode opcode() const;
    ScsiReadRequest readRequest() const;
    ScsiTocRequest tocRequest() const;
    unsigned modePage() const;
    quint16 startingFeature() const;

private:
    QByteArray bytes;
};

enum class SenseKey : quint8
{
    None = 0,
    MediumError = 3,
    IllegalRequest = 5
};
enum class AdditionalSense : quint8
{
    None = 0,
    UnrecoveredRead = 0x11,
    InvalidCommand = 0x20,
    BlockOutOfRange = 0x21,
    InvalidField = 0x24
};

struct ScsiSense
{
    SenseKey key = SenseKey::None;
    AdditionalSense additional = AdditionalSense::None;
    QByteArray response() const;
};

struct ScsiResult
{
    QByteArray data;
    quint8 status = 0;
};

class CdRom
{
public:
    static constexpr quint32 BlockSize = 2048;
    explicit CdRom(const QString& path);
    ScsiResult execute(
        const QByteArray& commandBytes,
        quint32 allocationLength);

    quint64 blocks() const
    {
        return (quint64(file.size()) + BlockSize - 1) / BlockSize;
    }

private:
    QFile file;
    ScsiSense sense;
    ScsiResult fail(AdditionalSense code);
};

}   // namespace aten
