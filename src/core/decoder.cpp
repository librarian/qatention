#include "decoder.h"

#include "aspeed/decoder.h"
#include "protocol.h"

#include <QtEndian>

#include <cstring>
#include <vector>

namespace aten {

Decoder::Decoder()
{
    resize({640, 480});
}

Decoder::~Decoder() = default;

void Decoder::resize(QSize size)
{
    if (size.width() < 1 || size.height() < 1 || size.width() > 4096 ||
        size.height() > 4096)
    {
        throw Error("Unsupported framebuffer dimensions");
    }
    aspeedDecoder.reset();
    frame = QImage(size, QImage::Format_ARGB32);
    if (frame.isNull()) {
        throw Error("Cannot allocate framebuffer");
    }
    frame.fill(Qt::black);
}

static QRgb rgb555(const char* bytes)
{
    const auto pixel =
        quint32(quint8(bytes[0])) | (quint32(quint8(bytes[1])) << 8U);
    return qRgb(
        ((pixel >> 10U) & 31U) << 3U,
        ((pixel >> 5U) & 31U) << 3U,
        (pixel & 31U) << 3U);
}

bool Decoder::decode(const QByteArray& packet, quint32 encoding)
{
    if (encoding == 0x57) {
        // ASPEED packet: quality[1], advanced quality[1], BE16 chroma marker,
        // followed by the native word-oriented compressed stream.
        if (packet.size() < 12 || quint8(packet[0]) > 11 ||
            quint8(packet[1]) > 11)
        {
            throw Error("Invalid ASPEED header");
        }
        auto marker = be16(packet, 2);
        if (marker != 0x1a6 && marker != 0x1bc) {
            throw Error("Unknown ASPEED chroma mode");
        }
        if (frame.width() > 1920 || frame.height() > 1200) {
            throw Error("ASPEED supports at most 1920x1200");
        }
        // Native input is uint32_t*: provide aligned storage, including zero
        // padding for partial words.
        std::vector<uint32_t> words(size_t((packet.size() - 4 + 3) / 4), 0);
        std::memcpy(
            words.data(),
            packet.constData() + 4,
            size_t(packet.size() - 4));
        const int blockSize = marker == 0x1a6 ? 16 : 8;
        QByteArray changedBlocks(
            ((qsizetype(frame.width()) + blockSize - 1) / blockSize) *
                ((frame.height() + blockSize - 1) / blockSize),
            0);
        if (!aspeedDecoder) {
            aspeedDecoder = std::make_unique<aspeed::Decoder>();
        }
        return aspeedDecoder->decode(
            {{words.data(), words.size()},
             std::size_t(packet.size() - 4),
             marker == 0x1a6 ? aspeed::Chroma::Yuv420 : aspeed::Chroma::Yuv444,
             quint8(packet[0]),
             quint8(packet[1])},
            {{frame.bits(), std::size_t(frame.sizeInBytes())},
             unsigned(frame.width()),
             unsigned(frame.height())},
            {reinterpret_cast<std::uint8_t*>(changedBlocks.data()),
             std::size_t(changedBlocks.size())});
    }
    if (packet.size() < 10) {
        throw Error("Truncated ATEN header");
    }
    // Legacy ATEN header (10 bytes): frame kind, encoding type, then
    // type-specific fields. Type zero is Hermon RGB555; others use planes.
    if (packet[1] != 0) {
        return decodePlanes(packet);
    }
    if (packet[0] == 1) {
        if (packet.size() - 10 < qsizetype(frame.width()) * frame.height() * 2)
        {
            throw Error("Truncated Hermon keyframe");
        }
        for (int y = 0; y < frame.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(frame.scanLine(y));
            for (int x = 0; x < frame.width(); ++x) {
                row[x] = rgb555(
                    packet.constData() + 10 + 2LL * (y * frame.width() + x));
            }
        }
        return true;
    }
    // Hermon delta: BE32 tile count in the header. Each 518-byte tile has
    // four reserved bytes, row/column in 16-pixel units, then 256 LE RGB555
    // pixels.
    const auto tileCount = be32(packet, 2);
    if (tileCount > quint32((packet.size() - 10) / 518)) {
        throw Error("Truncated Hermon tile update");
    }
    for (quint32 tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
        auto tileOffset = 10 + qsizetype(tileIndex) * 518;
        int top = quint8(packet[tileOffset + 4]) * 16,
            left = quint8(packet[tileOffset + 5]) * 16;
        for (int y = 0; y < 16 && y + top < frame.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(frame.scanLine(top + y));
            for (int x = 0; x < 16 && x + left < frame.width(); ++x) {
                row[left + x] = rgb555(
                    packet.constData() + tileOffset + 6 + 2LL * (y * 16 + x));
            }
        }
    }
    return tileCount != 0;
}
}   // namespace aten

namespace aten {
static QByteArray expandRle(const QByteArray& input, int size)
{
    QByteArray output(size, 0);
    qsizetype inputOffset = 0, outputOffset = 0;
    auto write = [&](char value, int count)
    {
        if (count > output.size() - outputOffset) {
            throw Error("RLE output exceeds frame");
        }
        std::memset(output.data() + outputOffset, quint8(value), size_t(count));
        outputOffset += count;
    };
    // Chunks begin on four-byte boundaries with a LE32 encoded byte count.
    // 0x55 introduces an escape/run; 0xaa repeats the next byte three times.
    while (inputOffset < input.size()) {
        inputOffset = ((inputOffset + 3) / 4) * 4;
        if (inputOffset == input.size()) {
            break;
        }
        if (input.size() - inputOffset < 4) {
            throw Error("Truncated RLE chunk header");
        }
        const auto chunkLength =
            qFromLittleEndian<quint32>(input.constData() + inputOffset);
        inputOffset += 4;
        if (chunkLength > input.size() - inputOffset) {
            throw Error("Truncated RLE chunk");
        }
        const qsizetype chunkEnd = inputOffset + chunkLength;
        while (inputOffset < chunkEnd) {
            const auto lead = quint8(input[inputOffset++]);
            if (inputOffset == chunkEnd) {
                write(char(lead), 1);
                continue;
            }
            if (lead == 0x55) {
                const auto count = quint8(input[inputOffset++]);
                if (count <= 1) {
                    write(count == 0 ? char(0x55) : char(0xaa), 1);
                } else {
                    if (inputOffset == chunkEnd) {
                        throw Error("Truncated RLE run");
                    }
                    write(input[inputOffset++], count + 1);
                }
            } else if (lead == 0xaa) {
                write(input[inputOffset++], 3);
            } else {
                write(char(lead), 1);
            }
        }
    }
    // Zero the unwritten tail rather than reusing pixels from an earlier frame.
    return output;
}

static QRgb planePixel(
    const QByteArray& planes,
    int base,
    int pixels,
    int pixelIndex,
    bool type4)
{
    const int stride = (pixels + 7) / 8;
    auto assemble = [&](int group)
    {
        quint32 channelValue = 0;
        for (int bit = 0; bit < 8; ++bit) {
            const int index =
                base + (group + 7 - bit) * stride + pixelIndex / 8;
            const quint32 packed = quint8(planes[index]);
            channelValue |= ((packed >> unsigned(pixelIndex % 8)) & 1U)
                            << unsigned(bit);
        }
        return channelValue;
    };
    if (type4) {
        return qRgb(assemble(24), assemble(0), assemble(8));
    }
    const auto low = assemble(8), high = assemble(0);
    // Type 2 uses the vendor's packed conversion, distinct from raw Hermon
    // RGB555.
    return qRgb(
        high & 0xf8U,
        ((low >> 3U & 0x1cU) | (high << 5U)) & 255U,
        (low << 3U) & 255U);
}

bool Decoder::decodePlanes(const QByteArray& packet)
{
    // Plane header: frame kind[1], type[1], subtype[1], reserved[3],
    // BE32 total record length. Selected types carry a 256-entry BGRA palette.
    const int type = quint8(packet[1]), subtype = quint8(packet[2]);
    const bool hasPalette = (type == 1 && subtype == 0) || type == 5;
    const int offset = hasPalette ? 1034 : 10;
    const quint32 total = be32(packet, 6);
    if (total < quint32(offset) || total > quint32(packet.size())) {
        throw Error("Invalid video payload length");
    }
    if (hasPalette) {
        palette = packet.mid(10, 1024);
    }
    const auto payload = packet.mid(offset, total - offset);
    const int pixels = frame.width() * frame.height();
    if (packet[0] == 1) {
        if (subtype == 2 || (subtype == 0 && type == 1)) {
            const bool nibble = subtype == 2;
            if (payload.size() < (pixels + (nibble ? 1 : 0)) / (nibble ? 2 : 1))
            {
                throw Error("Truncated palette frame");
            }
            for (int pixelIndex = 0; pixelIndex < pixels; ++pixelIndex) {
                const auto packedIndex =
                    quint32(quint8(payload[pixelIndex / (nibble ? 2 : 1)]));
                const int index =
                    nibble ? ((packedIndex >> unsigned((pixelIndex % 2) * 4)) &
                              15U)
                           : packedIndex;
                reinterpret_cast<QRgb*>(frame.scanLine(
                    pixelIndex / frame.width()))[pixelIndex % frame.width()] =
                    qRgb(
                        quint8(palette[index * 4 + 2]),
                        quint8(palette[index * 4 + 1]),
                        quint8(palette[index * 4LL]));
            }
            return true;
        }
        if (subtype != 0 || (type != 2 && type != 4)) {
            throw Error("Unsupported ATEN full-frame encoding");
        }
        const auto planes =
            expandRle(payload, (type == 4 ? 32 : 16) * ((pixels + 7) / 8));
        for (int pixelIndex = 0; pixelIndex < pixels; ++pixelIndex) {
            reinterpret_cast<QRgb*>(frame.scanLine(
                pixelIndex / frame.width()))[pixelIndex % frame.width()] =
                planePixel(planes, 0, pixels, pixelIndex, type == 4);
        }
        return true;
    }
    if (type != 2 && type != 4) {
        throw Error("Unsupported ATEN incremental encoding");
    }
    if (payload.size() < 4) {
        throw Error("Truncated tile table");
    }
    // Delta payload: LE16 tile count, {y,x,width,height} byte descriptors,
    // two padding bytes, then the RLE-compressed planes for all tiles.
    const int count = qFromLittleEndian<quint16>(payload.constData()),
              start = count * 4 + 4;
    if (start > payload.size()) {
        throw Error("Truncated tile table");
    }
    int totalPlanes = 0;
    const int planeSlots = type == 4 ? 32 : 16;
    for (int tileIndex = 0; tileIndex < count; ++tileIndex) {
        const int descriptorOffset = 2 + tileIndex * 4,
                  width = quint8(payload[descriptorOffset + 2]),
                  height = quint8(payload[descriptorOffset + 3]);
        const int bytes = planeSlots * ((width * height + 7) / 8);
        if (totalPlanes > 64LL * 1024 * 1024 - bytes) {
            throw Error("Tile expansion limit exceeded");
        }
        totalPlanes += bytes;
    }
    const auto planes = expandRle(payload.mid(start), totalPlanes);
    int planeOffset = 0;
    for (int tileIndex = 0; tileIndex < count; ++tileIndex) {
        const int descriptorOffset = 2 + tileIndex * 4,
                  y = quint8(payload[descriptorOffset]),
                  x = quint8(payload[descriptorOffset + 1]),
                  width = quint8(payload[descriptorOffset + 2]),
                  height = quint8(payload[descriptorOffset + 3]);
        for (int row = 0; row < height && y + row < frame.height(); ++row) {
            auto* line = reinterpret_cast<QRgb*>(frame.scanLine(y + row));
            for (int col = 0; col < width && x + col < frame.width(); ++col) {
                line[x + col] = planePixel(
                    planes,
                    planeOffset,
                    width * height,
                    row * width + col,
                    type == 4);
            }
        }
        planeOffset += planeSlots * ((width * height + 7) / 8);
    }
    return count > 0;
}
}   // namespace aten
