// Copyright (C) 2021 Aspeed Technology Inc.
// Derived from the ASPEED reference decoder; licensed under MPL-2.0.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace aspeed {
// Non-owning contiguous storage. Sizes count elements, not bytes.
template <class T>
struct Buffer
{
    T* data;
    std::size_t size;
};
enum class Chroma
{
    Yuv444,
    Yuv420
};

struct CompressedFrame
{
    // Host-order values of the little-endian 32-bit wire words.
    Buffer<const std::uint32_t> words;
    // Excludes any padding in the last word. The codec consumes whole words.
    std::size_t byteCount;
    Chroma chroma = Chroma::Yuv444;
    // Both quality selectors are in the range 0..11.
    std::uint8_t quality = 0;
    std::uint8_t advancedQuality = 0;
};

struct Framebuffer
{
    Buffer<std::uint8_t> bgra;   // Tightly packed BGRA pixels, four bytes each.
    // Nonzero dimensions, at most 1920x1200; there is no row padding.
    unsigned width;
    unsigned height;
};

// Each instance owns its reference frame, tables and bitstream workspace.
// Distinct instances can decode concurrently. A single instance is not
// reentrant.
class Decoder
{
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    // Clears the block map before decoding. One byte per 8x8 (444) or 16x16
    // (420) macroblock. Throws invalid_argument for invalid storage/parameters.
    // Unknown prefixes or out-of-range block positions stop decoding; blocks
    // already written remain. Truncated lookahead is zero-filled.
    bool decode(
        const CompressedFrame& input,
        const Framebuffer& output,
        Buffer<std::uint8_t> changedBlocks);

private:
    struct State;
    std::unique_ptr<State> state;
};
}   // namespace aspeed
