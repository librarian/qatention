#include "scsi.h"

namespace aten {
ScsiCommand::ScsiCommand(const QByteArray& commandBytes)
    : bytes(commandBytes)
{
    if (bytes.size() < 16) {
        throw Error("Truncated SCSI command descriptor");
    }
}

ScsiOpcode ScsiCommand::opcode() const
{
    return ScsiOpcode(quint8(bytes[0]));
}

ScsiReadRequest ScsiCommand::readRequest() const
{
    // READ(10) and READ(12) share a BE32 LBA but have different count fields.
    return {
        be32(bytes, 2),
        opcode() == ScsiOpcode::Read10 ? be16(bytes, 7) : be32(bytes, 6)};
}

ScsiTocRequest ScsiCommand::tocRequest() const
{
    unsigned format = quint8(bytes[2]) & 0x0fU;
    if (!format) {
        format =
            (unsigned(quint8(bytes[9])) >> 6U) & 3U;   // Legacy format bits.
    }
    return {format, quint8(bytes[6]), (quint8(bytes[1]) & 2U) != 0};
}

unsigned ScsiCommand::modePage() const
{
    return quint8(bytes[2]) & 0x3fU;
}

quint16 ScsiCommand::startingFeature() const
{
    return be16(bytes, 2);
}

QByteArray ScsiSense::response() const
{
    // Fixed-format REQUEST SENSE: key at byte 2, ASC at byte 12.
    QByteArray result(18, 0);
    result[0] = 0x70;
    result[2] = char(key);
    result[7] = 10;   // Additional sense length after the 8-byte header.
    result[12] = char(additional);
    return result;
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

ScsiResult CdRom::fail(AdditionalSense code)
{
    sense = {SenseKey::IllegalRequest, code};
    return {{}, 1};
}

ScsiResult CdRom::execute(
    const QByteArray& commandBytes,
    quint32 allocationLength)
{
    if (commandBytes.size() < 16 || allocationLength > 16LL * 1024 * 1024) {
        return fail(AdditionalSense::InvalidField);
    }
    const ScsiCommand command(commandBytes);
    QByteArray response;
    switch (command.opcode()) {
        case ScsiOpcode::TestUnitReady:
        case ScsiOpcode::StartStopUnit:
        case ScsiOpcode::PreventAllowRemoval:
            break;
        case ScsiOpcode::RequestSense:
            response = sense.response();
            sense = {};   // Reading sense consumes the previous error.
            break;
        case ScsiOpcode::Inquiry:
            // Standard INQUIRY: removable CD-ROM, response format 1,
            // followed by the existing vendor/product/revision identity.
            response = QByteArray(8, 0);
            response[0] = 0x05;         // Peripheral type: CD/DVD.
            response[1] = char(0x80);   // Removable media.
            response[3] = 0x21;   // Existing vendor response format flags.
            response[4] =
                31;   // Additional response bytes advertised by the BMC.
            response += "IPMI    Virtual CDROM   3000";
            break;
        case ScsiOpcode::ReadCapacity:
            // READ CAPACITY returns the last LBA (not the count), then block
            // size.
            response = QByteArray(8, 0);
            put32(response, 0, quint32(blocks() - 1));
            put32(response, 4, BlockSize);
            break;
        case ScsiOpcode::Read10:
        case ScsiOpcode::Read12: {
            const auto request = command.readRequest();
            const quint64 logicalBlockAddress = request.logicalBlockAddress;
            const quint64 blockCount = request.blockCount;
            if (!blockCount) {
                break;
            }
            if (logicalBlockAddress + blockCount > blocks()) {
                return fail(AdditionalSense::BlockOutOfRange);
            }
            const auto bytes = qsizetype(
                qMin(blockCount * BlockSize, quint64(allocationLength)));
            response = QByteArray(bytes, 0);
            if (!file.seek(qint64(logicalBlockAddress * BlockSize))) {
                return fail(AdditionalSense::BlockOutOfRange);
            }
            const auto available =
                qMin(qint64(bytes), file.size() - file.pos());
            if (file.read(response.data(), available) != available) {
                sense = {
                    SenseKey::MediumError,
                    AdditionalSense::UnrecoveredRead};
                return {{}, 1};
            }
            break;
        }
        case ScsiOpcode::ModeSense6:
            // MODE SENSE(6): three bytes follow the length byte; no pages.
            response = QByteArray(4, 0);
            response[0] = 3;
            break;
        case ScsiOpcode::ModeSense10:
            // MODE SENSE(10) header + CD/DVD capabilities page (0x2a).
            // Preserve the existing capability/speed bytes for compatibility;
            // page 0x3f requests all supported pages. Other pages get a header
            // only.
            response = QByteArray::fromHex(
                command.modePage() == 0x2a || command.modePage() == 0x3f
                    ? "00240100000000002a1c3f00717729231b90010000c61b9000100000"
                      "00000010000000000000"
                    : "0006010000000000");
            break;
        case ScsiOpcode::ReadToc: {
            const auto request = command.tocRequest();
            const int startTrack = request.startTrack;
            const bool sessionInformationOnly = request.format == 1;
            const bool leadOutOnly = startTrack == 0xaa;
            const bool useMinuteSecondFrame = request.minuteSecondFrame;
            if (!sessionInformationOnly && startTrack > 1 && !leadOutOnly) {
                return fail(AdditionalSense::InvalidField);
            }
            // TOC header (4 bytes), then one or two 8-byte track descriptors.
            // Track 0xaa is lead-out; MSF addresses include the 150-sector
            // lead-in.
            response =
                QByteArray(sessionInformationOnly || leadOutOnly ? 12 : 20, 0);
            put16(response, 0, quint16(response.size() - 2));
            response[2] = response[3] = 1;
            auto appendTrack =
                [&](int offset, int trackNumber, quint64 logicalBlockAddress)
            {
                response[offset + 1] = 0x14;
                response[offset + 2] = char(trackNumber);
                if (useMinuteSecondFrame) {
                    logicalBlockAddress += 150;
                    response[offset + 5] = char(logicalBlockAddress / 4500);
                    response[offset + 6] =
                        char((logicalBlockAddress / 75) % 60);
                    response[offset + 7] = char(logicalBlockAddress % 75);
                } else {
                    put32(response, offset + 4, quint32(logicalBlockAddress));
                }
            };
            if (sessionInformationOnly) {
                appendTrack(4, 1, 0);
            } else {
                if (!leadOutOnly) {
                    appendTrack(4, 1, 0);
                }
                appendTrack(leadOutOnly ? 4 : 12, 0xaa, blocks());
            }
            break;
        }
        case ScsiOpcode::GetConfiguration:
            // Existing MMC feature descriptors, selected by starting feature.
            // Each response begins with a BE32 data length, reserved bytes,
            // and the current CD-ROM profile (0x0008). Descriptor bodies are
            // compatibility constants; do not change them during refactoring.
            response = QByteArray::fromHex(
                command.startingFeature() == 0x28
                    ? "0000004c00000008002a000400010000"
                : command.startingFeature() == 0x20
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
        case ScsiOpcode::GetEventStatus:
            // Media event response: BE16 length, class/notification flags,
            // then the existing media-present event descriptor.
            response = QByteArray::fromHex("0006045e00020000");
            break;
        case ScsiOpcode::ReadDiscInformation:
            // Existing single-session disc descriptor. The leading BE16 is
            // the descriptor length; the ff fields are unknown disc
            // identifiers.
            response = QByteArray::fromHex(
                "00200e01010101200000000000000000ffffffffffffffff00000000000000"
                "000000");
            break;
        case ScsiOpcode::ReadTrackInformation:
            // Track-information response: fixed single-track flags and the
            // image capacity in the BE32 track-size field at byte 24.
            response = QByteArray(28, 0);
            put16(response, 0, 28);
            response[2] = 1;
            response[3] = 1;
            response[5] = 4;
            response[6] = 1;
            put32(response, 24, quint32(blocks()));
            break;
        default:
            return fail(AdditionalSense::InvalidCommand);
    }
    return {
        response.first(qMin(qsizetype(allocationLength), response.size())),
        0};
}

}   // namespace aten
