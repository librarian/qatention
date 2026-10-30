// Copyright (C) 2021 Aspeed Technology Inc.
// Derived from the ASPEED reference decoder; licensed under MPL-2.0.
#include "decoder.h"

#include "tables.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace aspeed {
namespace {
// Pack the Huffman code length and its ordinal into the lookup-table index.
constexpr Word makeWord(Byte low, Byte high)
{
    return Word(unsigned(low) | (unsigned(high) << 8U));
}

// Match the reference codec's arithmetic right shift for negative values using
// defined C++17 division. Truncating toward zero would change reconstructed
// pixels.
constexpr std::int64_t floorShift(std::int64_t value, unsigned shift)
{
    const auto divisor = std::int64_t(std::uint64_t{1} << shift);
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}

constexpr std::int64_t fixedMultiply(std::int64_t value, int factor)
{
    return floorShift(value * factor, 8);
}

constexpr std::size_t sampleIndex(std::int64_t value)
{
    return std::size_t(std::uint64_t(floorShift(value, 3)) & 1023U);
}
// Four-bit block prefixes. Skip forms add eight-bit X and Y block indices.
enum class BlockType : std::uint32_t
{
    Jpeg = 0x0,
    Refinement = 0x2,
    VqOne = 0x5,
    VqTwo = 0x6,
    VqFour = 0x7,
    JpegAt = 0x8,
    End = 0x9,
    RefinementAt = 0xa,
    VqOneAt = 0xd,
    VqTwoAt = 0xe,
    VqFourAt = 0xf
};
constexpr Byte BlockHeaderBits = 4;
constexpr Byte PositionedBlockHeaderBits = 20;

struct YuvPixel
{
    Byte luma;
    Byte cb;
    Byte cr;
};

struct ColorCache
{
    std::uint32_t color[4];
    Byte index[4];
    Byte indexBits;
};

// The wire stream stores a 64-bit lookahead as two 32-bit words. Truncated
// lookahead is zero-filled, matching the reference decoder's bounded behavior.
struct BitReader
{
    const std::uint32_t* words = nullptr;
    std::size_t nextWordIndex = 0, wordCount = 0;
    unsigned remainingBits = 32;
    std::uint32_t current = 0, next = 0;

    void reset(Buffer<const std::uint32_t> input, std::size_t byteCount)
    {
        words = input.data;
        wordCount = byteCount / sizeof(std::uint32_t);
        current = words[0];
        next = words[1];
        nextWordIndex = 2;
        remainingBits = 32;
    }

    Word peek(unsigned bits)
    {
        return (Word)(current >> (32U - bits));
    }

    void skip(unsigned bits)
    {
        if (remainingBits <= bits) {
            uint32_t data =
                nextWordIndex < wordCount ? words[nextWordIndex] : 0;
            nextWordIndex++;

            current = (current << bits) |
                      ((next | (data >> (remainingBits))) >> (32U - bits));
            next = data << (bits - remainingBits);
            remainingBits = 32 + remainingBits - bits;
        } else {
            current = (current << bits) | (next >> (32U - bits));
            next = next << bits;
            remainingBits -= bits;
        }
    }

    std::int16_t signedCoefficient(unsigned bits)
    {
        static constexpr std::int16_t neg_pow2[17] = {
            0,
            -1,
            -3,
            -7,
            -15,
            -31,
            -63,
            -127,
            -255,
            -511,
            -1023,
            -2047,
            -4095,
            -8191,
            -16383,
            -32767};
        std::int16_t signed_wordvalue;

        signed_wordvalue = peek(bits);
        if (((1U << (bits - 1U)) & unsigned(Word(signed_wordvalue))) == 0) {
            signed_wordvalue += neg_pow2[bits];
        }

        skip(bits);
        return signed_wordvalue;
    }
};
}   // namespace

struct Decoder::State
{
    // All writable tables and workspace belong to this decoder instance.
    int32_t quantization[4][64]{};
    int crToRed[256]{}, cbToBlue[256]{}, crToGreen[256]{}, cbToGreen[256]{},
        luminance[256]{};
    std::array<Byte, 5 * 256 + 128> rangeLimitStorage{};
    Byte* rangeLimit =
        nullptr;   // Points 256 bytes into owned rangeLimitStorage.
    HuffmanTable dcTables[4]{}, acTables[4]{};
    static constexpr Byte LumaTable = 0, ChromaTable = 1;
    std::int16_t previousLuma = 0, previousCb = 0, previousCr = 0;
    std::int16_t coefficients[384]{};
    Byte blockSamples[768]{};
    std::vector<YuvPixel> referencePixels;
    std::vector<Byte> referenceValid;
    unsigned width = 0, height = 0;
    int blockColumns = 0, blockRows = 0;
    bool subsampled = false;
    int quality = -1, advancedQuality = -1;
    BitReader bits;
    Buffer<Byte> changedBlocks{};

    State()
    {
        initializeTables();
    }

    void initializeColorTables()
    {
        // Reference coefficients rounded to 16 fractional bits: 1.597656,
        // 2.015625, -0.8125, -0.390625, and 1.164. Preserve this rounding.
        constexpr int Half = 32768;
        for (unsigned sample = 0; sample < 256; ++sample) {
            const int chroma = int(sample) - 128;
            crToRed[sample] = int(floorShift(104704 * chroma + Half, 16));
            cbToBlue[sample] = int(floorShift(132096 * chroma + Half, 16));
            crToGreen[sample] = int(floorShift(-53248 * chroma + Half, 16));
            cbToGreen[sample] = int(floorShift(-25600 * chroma + Half, 16));
            luminance[sample] =
                int(floorShift(76284 * (int(sample) - 16) + Half, 16));
        }
    }

    /* Allocate and fill in the sample_range_limit table */
    void initializeRangeLimit()
    {
        int j;
        rangeLimit = rangeLimitStorage.data();
        /* First segment of "simple" table: limit[x] = 0 for x < 0 */
        std::fill_n(rangeLimit, 256, 0);
        rangeLimit += 256; /* allow negative subscripts of simple table */
        /* Main part of "simple" table: limit[x] = x */
        for (j = 0; j < 256; j++) {
            rangeLimit[j] = j;
        }
        /* End of simple table, rest of first half of post-IDCT table */
        std::fill_n(rangeLimit + 256, 384, 255);

        /* Second half of post-IDCT table */
        std::fill_n(rangeLimit + 640, 384, 0);
        for (j = 0; j < 128; j++) {
            rangeLimit[j + 1024] = j;
        }
    }

    void initializeHuffmanTable(
        HuffmanTable& table,
        const Byte* counts,
        const Byte* symbols,
        const Word* thresholds)
    {
        unsigned symbolIndex = 0;
        unsigned code = 0;
        for (unsigned length = 1; length <= 16; ++length) {
            table.counts[length] = counts[length];
            for (unsigned ordinal = 0; ordinal < counts[length]; ++ordinal) {
                table.symbols[makeWord(Byte(length), Byte(ordinal))] =
                    symbols[symbolIndex++];
            }
            table.firstCode[length] = Word(code);
            code += counts[length];
            table.lastCode[length] = Word(code - 1);
            code *= 2;
            if (counts[length] == 0) {
                table.firstCode[length] = 0xffff;
                table.lastCode[length] = 0;
            }
        }
        table.codeLengths[0] = 2;
        unsigned threshold = 2;
        for (unsigned lookahead = 1; lookahead < 65536; ++lookahead) {
            // Threshold pairs encode (first code, bit length). 0xffff is also
            // the final sentinel: never advance past that pair.
            if (thresholds[threshold] != 0xffff &&
                lookahead >= thresholds[threshold])
            {
                threshold += 2;
            }
            table.codeLengths[lookahead] = Byte(thresholds[threshold + 1]);
        }
    }

    // setup all table needed
    void initializeTables()
    {
        initializeColorTables();
        initializeRangeLimit();
        initializeHuffmanTable(
            dcTables[0],
            std_dc_luminance_nrcodes,
            std_dc_luminance_values,
            DC_LUMINANCE_HUFFMANCODE);
        initializeHuffmanTable(
            acTables[0],
            std_ac_luminance_nrcodes,
            std_ac_luminance_values,
            AC_LUMINANCE_HUFFMANCODE);
        initializeHuffmanTable(
            dcTables[1],
            std_dc_chrominance_nrcodes,
            std_dc_chrominance_values,
            DC_CHROMINANCE_HUFFMANCODE);
        initializeHuffmanTable(
            acTables[1],
            std_ac_chrominance_nrcodes,
            std_ac_chrominance_values,
            AC_CHROMINANCE_HUFFMANCODE);
    }

    // Preserve the reference decoder's float evaluation order and fixed-point
    // scaling: changing it can change IDCT rounding and therefore output
    // pixels.
    void
    loadQuantization(int32_t* destination, unsigned quality, bool chrominance)
    {
        static constexpr const Byte* luminance[] = {
            Tbl_000Y,
            Tbl_014Y,
            Tbl_029Y,
            Tbl_043Y,
            Tbl_057Y,
            Tbl_071Y,
            Tbl_086Y,
            Tbl_100Y,
            Tbl_08Y,
            Tbl_09Y,
            Tbl_10Y,
            Tbl_11Y};
        static constexpr const Byte* chroma[] = {
            Tbl_000UV,
            Tbl_014UV,
            Tbl_029UV,
            Tbl_043UV,
            Tbl_057UV,
            Tbl_071UV,
            Tbl_086UV,
            Tbl_100UV,
            Tbl_08UV,
            Tbl_09UV,
            Tbl_10UV,
            Tbl_11UV};
        static constexpr float scale[8] = {
            1.0f,
            1.387039845f,
            1.306562965f,
            1.175875602f,
            1.0f,
            0.785694958f,
            0.541196100f,
            0.275899379f};
        const auto* base = chrominance ? chroma[quality] : luminance[quality];
        for (unsigned row = 0; row < 8; ++row) {
            for (unsigned column = 0; column < 8; ++column) {
                const auto index = row * 8 + column;
                // The original scale factor is always 16: (base * 16) / 16.
                const auto coefficient = std::clamp(int(base[index]), 1, 255);
                destination[index] =
                    int32_t((coefficient * scale[row] * scale[column]) * 65536);
            }
        }
    }

    void markBlock(int blockX, int blockY)
    {
        int i;
        if (changedBlocks.data == nullptr) {
            return;
        }
        i = blockY * blockColumns + blockX;
        if (i >= 0 && std::size_t(i) < changedBlocks.size) {
            changedBlocks.data[i] = 1;
        }
    }

    // Unlike the IDCT lookup, RGB conversion can produce values below -256.
    Byte clampColor(int value)
    {
        return value < 0 ? 0 : value > 255 ? 255 : (Byte)value;
    }

    // The output byte order is BGRA, matching QImage::Format_ARGB32 on the
    // supported little-endian platforms. Byte stores avoid overlaying a C
    // struct.
    void writePixel(Byte* output, std::size_t pixelIndex, YuvPixel color)
    {
        const auto offset = pixelIndex * 4;
        output[offset] = clampColor(luminance[color.luma] + cbToBlue[color.cb]);
        output[offset + 1] = clampColor(
            luminance[color.luma] + cbToGreen[color.cb] + crToGreen[color.cr]);
        output[offset + 2] =
            clampColor(luminance[color.luma] + crToRed[color.cr]);
        output[offset + 3] = 255;
    }

    void
    writeBaseBlock(int blockX, int blockY, const Byte* samples, Byte* output)
    {
        const unsigned blockSize = subsampled ? 16 : 8;
        const unsigned left = unsigned(blockX) * blockSize;
        const unsigned top = unsigned(blockY) * blockSize;
        const auto columns = std::min(width - left, blockSize);
        const auto rows = std::min(height - top, blockSize);
        markBlock(blockX, blockY);
        if (!subsampled) {
            referenceValid[blockY * blockColumns + blockX] = 1;
            // Three contiguous 8x8 planes: Y, Cb, Cr. Retain these samples for
            // subsequent refinement blocks, which encode deltas rather than
            // pixels.
            for (unsigned row = 0; row < rows; ++row) {
                for (unsigned column = 0; column < columns; ++column) {
                    const auto sample = row * 8 + column;
                    const auto pixel =
                        std::size_t(top + row) * width + left + column;
                    const YuvPixel color{
                        samples[sample],
                        samples[64 + sample],
                        samples[128 + sample]};
                    referencePixels[pixel] = color;
                    writePixel(output, pixel, color);
                }
            }
        } else {
            // Four luma quadrants (TL, TR, BL, BR), then two 8x8 chroma planes.
            // Preserve the reference codec's sequential reads within each
            // visible luma quadrant, including partial blocks at the right
            // image edge.
            std::array<unsigned, 4> lumaCursor{0, 64, 128, 192};
            for (unsigned row = 0; row < rows; ++row) {
                for (unsigned column = 0; column < columns; ++column) {
                    const auto quadrant = (row / 8) * 2 + column / 8;
                    const auto chromaSample = (row / 2) * 8 + column / 2;
                    const YuvPixel color{
                        samples[lumaCursor[quadrant]++],
                        samples[256 + chromaSample],
                        samples[320 + chromaSample]};
                    const auto pixel =
                        std::size_t(top + row) * width + left + column;
                    writePixel(output, pixel, color);
                }
            }
        }
    }

    void writeRefinementBlock(
        int blockX,
        int blockY,
        const Byte* samples,
        Byte* output)
    {
        if (subsampled || !referenceValid[blockY * blockColumns + blockX]) {
            return;
        }
        markBlock(blockX, blockY);
        const unsigned left = unsigned(blockX) * 8;
        const unsigned top = unsigned(blockY) * 8;
        const auto columns = std::min(width - left, 8u);
        const auto rows = std::min(height - top, 8u);
        for (unsigned row = 0; row < rows; ++row) {
            for (unsigned column = 0; column < columns; ++column) {
                const auto sample = row * 8 + column;
                const auto pixel =
                    std::size_t(top + row) * width + left + column;
                auto& color = referencePixels[pixel];
                // The stream biases each signed delta by 128. Conversion back
                // to Byte intentionally preserves the reference codec's
                // modulo-256 sum.
                color.luma = Byte(color.luma + samples[sample] - 128);
                color.cb = Byte(color.cb + samples[64 + sample] - 128);
                color.cr = Byte(color.cr + samples[128 + sample] - 128);
                writePixel(output, pixel, color);
            }
        }
    }

    // Decode one baseline JPEG data unit: differential DC followed by
    // run-length AC.
    void decodeCoefficients(
        Byte dcTableIndex,
        Byte acTableIndex,
        std::int16_t* previousDc,
        Word coefficientOffset)
    {
        Byte coefficientIndex, codeLength;
        Word code;
        Byte valueBits, zeroRun;
        Word* minimumCodes;
        Byte* values;
        Byte symbol;

        minimumCodes = dcTables[dcTableIndex].firstCode;
        values = dcTables[dcTableIndex].symbols;

        // 1st phase, DC coefficient
        codeLength =
            dcTables[dcTableIndex].codeLengths[(Word)(bits.current >> 16U)];
        code = bits.peek(codeLength);
        bits.skip(codeLength);
        valueBits = values[makeWord(
            codeLength,
            (Byte)(code - minimumCodes[codeLength]))];
        if (valueBits == 0) {
            coefficients[coefficientOffset + 0] = *previousDc;
        } else {
            coefficients[coefficientOffset + 0] =
                (*previousDc += bits.signedCoefficient(valueBits));
        }

        // 2nd phase, AC coefficient
        minimumCodes = acTables[acTableIndex].firstCode;
        values = acTables[acTableIndex].symbols;

        coefficientIndex = 1;   // AC coefficient
        do {
            codeLength =
                acTables[acTableIndex].codeLengths[(Word)(bits.current >> 16U)];
            code = bits.peek(codeLength);
            bits.skip(codeLength);

            symbol = values[makeWord(
                codeLength,
                (Byte)(code - minimumCodes[codeLength]))];
            valueBits = unsigned(symbol) & 0xFU;
            zeroRun = unsigned(symbol) >> 4U;
            if (valueBits == 0) {
                if (zeroRun != 0xF) {
                    break;
                }
                coefficientIndex += 16;
            } else {
                coefficientIndex += zeroRun;   // skip zeroRun zeroes
                coefficients[coefficientOffset + dezigzag[coefficientIndex++]] =
                    bits.signedCoefficient(valueBits);
            }
        } while (coefficientIndex < 64);
    }

    // Reference integer IDCT butterfly. Numbered temporary terms retain the
    // reference equations; 64-bit intermediates protect malformed coefficients.
    void
    inverseDct(short* inputCoefficients, Byte* samples, Byte quantizationIndex)
    {
        constexpr int FIX_1_082392200 = 277;
        constexpr int FIX_1_414213562 = 362;
        constexpr int FIX_1_847759065 = 473;
        constexpr int FIX_2_613125930 = 669;

        constexpr std::size_t DctSize = 8;

        // Wide intermediates are required even when input coefficients fit in
        // std::int16_t.
        int64_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
        int64_t tmp10, tmp11, tmp12, tmp13;
        int64_t z5, z10, z11, z12, z13;
        int64_t workspace[64]; /* buffers samples between passes */

        short* coefficientColumn = inputCoefficients;
        int32_t* quantizationColumn = quantization[quantizationIndex];
        int64_t* workspaceRow = workspace;
        unsigned char* outputRow;
        unsigned char* idctRangeLimit = rangeLimit + 128;
        std::size_t i;

        // Pass 1: process columns from input (coefficientColumn), store into
        // work array(workspaceRow)
        for (i = 8; i > 0; i--) {
            /* Due to quantization, we will usually find that many of the input
             * coefficients are zero, especially the AC terms.  We can exploit
             * this by short-circuiting the IDCT calculation for any column in
             * which all the AC terms are zero.  In that case each output is
             * equal to the DC coefficient (with scale factor as needed). With
             * typical images and quantization tables, half or more of the
             * column DCT calculations can be simplified this way.
             */

            /* check for AC terms all zero */
            if (coefficientColumn[DctSize * 1] == 0 &&
                coefficientColumn[DctSize * 2] == 0 &&
                coefficientColumn[DctSize * 3] == 0 &&
                coefficientColumn[DctSize * 4] == 0 &&
                coefficientColumn[DctSize * 5] == 0 &&
                coefficientColumn[DctSize * 6] == 0 &&
                coefficientColumn[DctSize * 7] == 0)
            {
                workspaceRow[DctSize * 0] = workspaceRow[DctSize * 1] =
                    workspaceRow[DctSize * 2] = workspaceRow[DctSize * 3] =
                        workspaceRow[DctSize * 4] = workspaceRow[DctSize * 5] =
                            workspaceRow[DctSize * 6] =
                                workspaceRow[DctSize * 7] = floorShift(
                                    int64_t(coefficientColumn[DctSize * 0]) *
                                        quantizationColumn[DctSize * 0],
                                    16);

                coefficientColumn++; /* advance pointers to next column */
                quantizationColumn++;
                workspaceRow++;
                continue;
            }

            /* Even part */

            tmp0 = floorShift(
                int64_t(coefficientColumn[DctSize * 0]) *
                    quantizationColumn[DctSize * 0],
                16);
            tmp1 = floorShift(
                int64_t(coefficientColumn[DctSize * 2]) *
                    quantizationColumn[DctSize * 2],
                16);
            tmp2 = floorShift(
                int64_t(coefficientColumn[DctSize * 4]) *
                    quantizationColumn[DctSize * 4],
                16);
            tmp3 = floorShift(
                int64_t(coefficientColumn[DctSize * 6]) *
                    quantizationColumn[DctSize * 6],
                16);

            tmp10 = tmp0 + tmp2; /* phase 3 */
            tmp11 = tmp0 - tmp2;

            tmp13 = tmp1 + tmp3; /* phases 5-3 */
            tmp12 =
                fixedMultiply(tmp1 - tmp3, FIX_1_414213562) - tmp13; /* 2*c4 */

            tmp0 = tmp10 + tmp13; /* phase 2 */
            tmp3 = tmp10 - tmp13;
            tmp1 = tmp11 + tmp12;
            tmp2 = tmp11 - tmp12;

            /* Odd part */

            tmp4 = floorShift(
                int64_t(coefficientColumn[DctSize * 1]) *
                    quantizationColumn[DctSize * 1],
                16);
            tmp5 = floorShift(
                int64_t(coefficientColumn[DctSize * 3]) *
                    quantizationColumn[DctSize * 3],
                16);
            tmp6 = floorShift(
                int64_t(coefficientColumn[DctSize * 5]) *
                    quantizationColumn[DctSize * 5],
                16);
            tmp7 = floorShift(
                int64_t(coefficientColumn[DctSize * 7]) *
                    quantizationColumn[DctSize * 7],
                16);

            z13 = tmp6 + tmp5; /* phase 6 */
            z10 = tmp6 - tmp5;
            z11 = tmp4 + tmp7;
            z12 = tmp4 - tmp7;

            tmp7 = z11 + z13;                                  /* phase 5 */
            tmp11 = fixedMultiply(z11 - z13, FIX_1_414213562); /* 2*c4 */

            z5 = fixedMultiply(z10 + z12, FIX_1_847759065);    /* 2*c2 */
            tmp10 = fixedMultiply(z12, FIX_1_082392200) - z5;  /* 2*(c2-c6) */
            tmp12 = fixedMultiply(z10, -FIX_2_613125930) + z5; /* -2*(c2+c6) */

            tmp6 = tmp12 - tmp7; /* phase 2 */
            tmp5 = tmp11 - tmp6;
            tmp4 = tmp10 + tmp5;

            workspaceRow[DctSize * 0] = (tmp0 + tmp7);
            workspaceRow[DctSize * 7] = (tmp0 - tmp7);
            workspaceRow[DctSize * 1] = (tmp1 + tmp6);
            workspaceRow[DctSize * 6] = (tmp1 - tmp6);
            workspaceRow[DctSize * 2] = (tmp2 + tmp5);
            workspaceRow[DctSize * 5] = (tmp2 - tmp5);
            workspaceRow[DctSize * 4] = (tmp3 + tmp4);
            workspaceRow[DctSize * 3] = (tmp3 - tmp4);

            coefficientColumn++; /* advance pointers to next column */
            quantizationColumn++;
            workspaceRow++;
        }

        /* Pass 2: process rows from work array, store into output array. */
        /* Note that we must descale the results by a factor of 8 == 2**3, */
        /* and also undo the PASS1_BITS scaling. */

        workspaceRow = workspace;
        for (i = 0; i < DctSize; i++) {
            outputRow = samples + (i * 8);

            /* check for AC terms all zero */
            if (workspaceRow[1] == 0 && workspaceRow[2] == 0 &&
                workspaceRow[3] == 0 && workspaceRow[4] == 0 &&
                workspaceRow[5] == 0 && workspaceRow[6] == 0 &&
                workspaceRow[7] == 0)
            {
                /* AC terms all zero */

                outputRow[0] = outputRow[1] = outputRow[2] = outputRow[3] =
                    outputRow[4] = outputRow[5] = outputRow[6] = outputRow[7] =
                        idctRangeLimit[sampleIndex(workspaceRow[0])];

                workspaceRow += DctSize; /* advance pointer to next row */
                continue;
            }
            /* Even part */

            tmp10 = (workspaceRow[0] + workspaceRow[4]);
            tmp11 = (workspaceRow[0] - workspaceRow[4]);

            tmp13 = (workspaceRow[2] + workspaceRow[6]);
            tmp12 = fixedMultiply(
                        workspaceRow[2] - workspaceRow[6],
                        FIX_1_414213562) -
                    tmp13;

            tmp0 = tmp10 + tmp13;
            tmp3 = tmp10 - tmp13;
            tmp1 = tmp11 + tmp12;
            tmp2 = tmp11 - tmp12;

            /* Odd part */

            z13 = workspaceRow[5] + workspaceRow[3];
            z10 = workspaceRow[5] - workspaceRow[3];
            z11 = workspaceRow[1] + workspaceRow[7];
            z12 = workspaceRow[1] - workspaceRow[7];

            tmp7 = z11 + z13;                                  /* phase 5 */
            tmp11 = fixedMultiply(z11 - z13, FIX_1_414213562); /* 2*c4 */

            z5 = fixedMultiply(z10 + z12, FIX_1_847759065);    /* 2*c2 */
            tmp10 = fixedMultiply(z12, FIX_1_082392200) - z5;  /* 2*(c2-c6) */
            tmp12 = fixedMultiply(z10, -FIX_2_613125930) + z5; /* -2*(c2+c6) */

            tmp6 = tmp12 - tmp7; /* phase 2 */
            tmp5 = tmp11 - tmp6;
            tmp4 = tmp10 + tmp5;

            /* Final output stage: scale down by a factor of 8 and range-limit
             */
            outputRow[0] = idctRangeLimit[sampleIndex((tmp0 + tmp7))];
            outputRow[7] = idctRangeLimit[sampleIndex((tmp0 - tmp7))];
            outputRow[1] = idctRangeLimit[sampleIndex((tmp1 + tmp6))];
            outputRow[6] = idctRangeLimit[sampleIndex((tmp1 - tmp6))];
            outputRow[2] = idctRangeLimit[sampleIndex((tmp2 + tmp5))];
            outputRow[5] = idctRangeLimit[sampleIndex((tmp2 - tmp5))];
            outputRow[4] = idctRangeLimit[sampleIndex((tmp3 + tmp4))];
            outputRow[3] = idctRangeLimit[sampleIndex((tmp3 - tmp4))];

            workspaceRow += DctSize; /* advance pointer to next row */
        }
    }

    int decodeBaseBlock(
        int blockX,
        int blockY,
        unsigned char* output,
        Byte quantizationIndex)
    {
        unsigned char* ptr;

        std::fill_n(coefficients, 192 * (1 + subsampled), 0);

        ptr = blockSamples;
        if (subsampled) {
            decodeCoefficients(LumaTable, LumaTable, &previousLuma, 0);
            inverseDct(coefficients, ptr, quantizationIndex);
            ptr += 64;

            decodeCoefficients(LumaTable, LumaTable, &previousLuma, 64);
            inverseDct(coefficients + 64, ptr, quantizationIndex);
            ptr += 64;

            decodeCoefficients(LumaTable, LumaTable, &previousLuma, 128);
            inverseDct(coefficients + 128, ptr, quantizationIndex);
            ptr += 64;

            decodeCoefficients(LumaTable, LumaTable, &previousLuma, 192);
            inverseDct(coefficients + 192, ptr, quantizationIndex);
            ptr += 64;

            decodeCoefficients(ChromaTable, ChromaTable, &previousCb, 256);
            inverseDct(coefficients + 256, ptr, quantizationIndex + 1);
            ptr += 64;

            decodeCoefficients(ChromaTable, ChromaTable, &previousCr, 320);
            inverseDct(coefficients + 320, ptr, quantizationIndex + 1);
        } else {
            decodeCoefficients(LumaTable, LumaTable, &previousLuma, 0);
            inverseDct(coefficients, ptr, quantizationIndex);
            ptr += 64;

            decodeCoefficients(ChromaTable, ChromaTable, &previousCb, 64);
            inverseDct(coefficients + 64, ptr, quantizationIndex + 1);
            ptr += 64;

            decodeCoefficients(ChromaTable, ChromaTable, &previousCr, 128);
            inverseDct(coefficients + 128, ptr, quantizationIndex + 1);
        }

        writeBaseBlock(blockX, blockY, blockSamples, output);

        return 0;
    }

    int decodeRefinementBlock(
        int blockX,
        int blockY,
        unsigned char* output,
        Byte quantizationIndex)
    {
        unsigned char* ptr;

        std::fill_n(coefficients, 192, 0);

        ptr = blockSamples;
        decodeCoefficients(LumaTable, LumaTable, &previousLuma, 0);
        inverseDct(coefficients, ptr, quantizationIndex);
        ptr += 64;

        decodeCoefficients(ChromaTable, ChromaTable, &previousCb, 64);
        inverseDct(coefficients + 64, ptr, quantizationIndex + 1);
        ptr += 64;

        decodeCoefficients(ChromaTable, ChromaTable, &previousCr, 128);
        inverseDct(coefficients + 128, ptr, quantizationIndex + 1);

        writeRefinementBlock(blockX, blockY, blockSamples, output);

        return 0;
    }

    void advanceBlock(int* blockX, int* blockY)
    {
        (*blockX)++;
        if (*blockX >= blockColumns) {
            (*blockY)++;
            if (*blockY >= blockRows) {
                *blockY = 0;
            }
            *blockX = 0;
        }
    }

    // Returns 0 when the block position read from the stream lies outside the
    // frame. The position is 8 bits each way, so without this check a malformed
    // packet writes up to 255 blocks past the output and 2-pass buffers.
    int readBlockPosition(int* blockX, int* blockY)
    {
        *blockX = (bits.current & 0x0FF00000U) >> 20U;
        *blockY = (bits.current & 0x0FF000U) >> 12U;
        return *blockX < blockColumns && *blockY < blockRows;
    }

    void readVqPalette(struct ColorCache* palette, Byte count)
    {
        Byte pixelIndex;
        palette->indexBits = count == 1 ? 0 : (count == 2 ? 1 : 2);
        for (pixelIndex = 0; pixelIndex < count; pixelIndex++) {
            Byte index = (Byte)((bits.current >> 29U) & 0x03U);
            palette->index[pixelIndex] = index;
            if (((bits.current >> 31U) & 0x01U) == 0) {
                bits.skip(3);
            } else {
                palette->color[index] = (bits.current >> 5U) & 0x00ffffffU;
                bits.skip(27);
            }
        }
    }

    void decodeVqBlock(
        int blockX,
        int blockY,
        Byte* output,
        struct ColorCache* palette)
    {
        // VQ encodes an 8x8 4:4:4 block. The 4:2:0 converter expects 384
        // bytes, so an incompatible block must not reach it.
        if (subsampled) {
            return;
        }
        Byte samples[192];
        Byte* sampleCursor = samples;
        int pixelIndex;
        for (pixelIndex = 0; pixelIndex < 64; pixelIndex++) {
            int paletteIndex =
                palette->indexBits == 0 ? 0 : bits.peek(palette->indexBits);
            std::uint32_t color = palette->color[palette->index[paletteIndex]];
            sampleCursor[0] = (Byte)((color >> 16U) & 0xffU);
            sampleCursor[64] = (Byte)((color >> 8U) & 0xffU);
            sampleCursor[128] = (Byte)(color & 0xffU);
            sampleCursor++;
            if (palette->indexBits != 0) {
                bits.skip(palette->indexBits);
            }
        }
        writeBaseBlock(blockX, blockY, samples, output);
    }

    void decodeBlocks(int len, Byte* output)
    {
        int blockX = 0, blockY = 0;
        struct ColorCache vq = {
            {0x008080, 0xff8080, 0x808080, 0xc08080},
            {0, 1, 2, 3},
            0};

        do {
            switch (BlockType(bits.current >> 28U)) {
                case BlockType::Jpeg:
                    bits.skip(BlockHeaderBits);
                    decodeBaseBlock(blockX, blockY, output, 0);
                    advanceBlock(&blockX, &blockY);
                    break;
                case BlockType::End:
                    return;
                case BlockType::JpegAt:
                    if (!readBlockPosition(&blockX, &blockY)) {
                        return;
                    }

                    bits.skip(PositionedBlockHeaderBits);
                    decodeBaseBlock(blockX, blockY, output, 0);
                    advanceBlock(&blockX, &blockY);
                    break;
                case BlockType::Refinement:
                    bits.skip(BlockHeaderBits);
                    decodeRefinementBlock(blockX, blockY, output, 2);
                    advanceBlock(&blockX, &blockY);
                    break;
                case BlockType::RefinementAt:
                    if (!readBlockPosition(&blockX, &blockY)) {
                        return;
                    }

                    bits.skip(PositionedBlockHeaderBits);
                    decodeRefinementBlock(blockX, blockY, output, 2);
                    advanceBlock(&blockX, &blockY);
                    break;
                case BlockType::VqOne:
                case BlockType::VqTwo:
                case BlockType::VqFour: {
                    Byte count = (Byte)(1u
                                        << (((bits.current >> 28U) & 0x0fU) -
                                            unsigned(BlockType::VqOne)));
                    bits.skip(BlockHeaderBits);
                    readVqPalette(&vq, count);
                    decodeVqBlock(blockX, blockY, output, &vq);
                    advanceBlock(&blockX, &blockY);
                    break;
                }
                case BlockType::VqOneAt:
                case BlockType::VqTwoAt:
                case BlockType::VqFourAt: {
                    Byte count = (Byte)(1u
                                        << (((bits.current >> 28U) & 0x0fU) -
                                            unsigned(BlockType::VqOneAt)));
                    if (!readBlockPosition(&blockX, &blockY)) {
                        return;
                    }
                    bits.skip(PositionedBlockHeaderBits);
                    readVqPalette(&vq, count);
                    decodeVqBlock(blockX, blockY, output, &vq);
                    advanceBlock(&blockX, &blockY);
                    break;
                }
                default:
                    /* An unknown block prefix cannot be skipped safely. Stop
                     * instead of spinning forever on the same four bits when a
                     * packet is malformed or misframed. */
                    return;
            }
        } while (bits.nextWordIndex <=
                 (uint32_t)(len / (int)sizeof(std::uint32_t)));
    }
};

Decoder::Decoder()
    : state(std::make_unique<State>())
{}

Decoder::~Decoder() = default;

bool Decoder::decode(
    const CompressedFrame& input,
    const Framebuffer& output,
    Buffer<std::uint8_t> changedBlocks)
{
    if (!output.bgra.data || !output.width || !output.height ||
        output.width > 1920 || output.height > 1200 ||
        output.bgra.size < std::size_t(output.width) * output.height * 4 ||
        !input.words.data || input.byteCount < 8 ||
        input.byteCount / 4 + (input.byteCount % 4 != 0) > input.words.size ||
        input.byteCount > 0x7fffffff || input.quality > 11 ||
        input.advancedQuality > 11 ||
        (input.chroma != Chroma::Yuv444 && input.chroma != Chroma::Yuv420))
    {
        throw std::invalid_argument(
            "Invalid ASPEED frame storage or parameters");
    }
    const bool subsampled = input.chroma == Chroma::Yuv420;
    const unsigned blockSize = subsampled ? 16 : 8;
    const auto columns = (output.width + blockSize - 1) / blockSize;
    const auto rows = (output.height + blockSize - 1) / blockSize;
    const auto blockCount = std::size_t(columns) * rows;
    if (!changedBlocks.data || changedBlocks.size < blockCount) {
        throw std::invalid_argument("ASPEED changed-block map is too small");
    }
    auto& decoder = *state;
    // Reference samples are tied to geometry. Resizing invalidates old deltas.
    if (decoder.width != output.width || decoder.height != output.height) {
        decoder.referencePixels.assign(
            std::size_t(output.width) * output.height,
            {});
        decoder.referenceValid.assign(
            std::size_t((output.width + 7) / 8) * ((output.height + 7) / 8),
            0);
        decoder.width = output.width;
        decoder.height = output.height;
    }
    if (decoder.quality != input.quality ||
        decoder.advancedQuality != input.advancedQuality)
    {
        decoder.loadQuantization(decoder.quantization[0], input.quality, false);
        decoder.loadQuantization(decoder.quantization[1], input.quality, true);
        decoder.loadQuantization(
            decoder.quantization[2],
            input.advancedQuality,
            false);
        decoder.loadQuantization(
            decoder.quantization[3],
            input.advancedQuality,
            true);
        decoder.quality = input.quality;
        decoder.advancedQuality = input.advancedQuality;
    }
    if (subsampled) {
        // Refinements cannot reuse 444 references from before a 420 frame.
        std::fill(
            decoder.referenceValid.begin(),
            decoder.referenceValid.end(),
            0);
    }
    std::fill_n(changedBlocks.data, changedBlocks.size, 0);
    decoder.changedBlocks = changedBlocks;
    decoder.subsampled = subsampled;
    decoder.blockColumns = int(columns);
    decoder.blockRows = int(rows);
    decoder.bits.reset(input.words, input.byteCount);
    decoder.previousLuma = decoder.previousCb = decoder.previousCr = 0;
    decoder.decodeBlocks(int(input.byteCount), output.bgra.data);
    // Do not retain caller-owned storage between calls.
    decoder.changedBlocks = {};
    decoder.bits = {};
    return std::any_of(
        changedBlocks.data,
        changedBlocks.data + blockCount,
        [](Byte changed) { return changed != 0; });
}
}   // namespace aspeed
