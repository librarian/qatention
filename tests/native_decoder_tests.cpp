// Qt-independent tests: no prebuilt Qt code crosses the sanitizer boundary.
#include "aspeed/decoder.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(                                                      \
                stderr,                                                        \
                "%s:%d: %s\n",                                                 \
                __FILE__,                                                      \
                __LINE__,                                                      \
                #condition);                                                   \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

namespace {
using aspeed::Chroma;
constexpr std::array<std::uint32_t, 2> WhiteBlock{0x53200000, 0};
constexpr std::array<std::uint32_t, 2> BlackBlock{0x51200000, 0};
constexpr std::array<std::uint32_t, 2> DarkBlueBlock{0x58000001, 0x20000000};
constexpr std::array<std::uint32_t, 2> RefinementOnly{0x20000000, 0};

struct Frame
{
    std::array<std::uint8_t, std::size_t{64} * 64 * 4> pixels{};
    std::array<std::uint8_t, 64> changed{};

    aspeed::Framebuffer output(unsigned width = 64, unsigned height = 64)
    {
        return {{pixels.data(), pixels.size()}, width, height};
    }

    aspeed::Buffer<std::uint8_t> blockMap()
    {
        return {changed.data(), changed.size()};
    }
};

template <std::size_t Size>
aspeed::CompressedFrame packet(
    const std::array<std::uint32_t, Size>& words,
    Chroma chroma = Chroma::Yuv444)
{
    return {{words.data(), words.size()}, sizeof(words), chroma, 0, 0};
}

void regressions()
{
    aspeed::Decoder decoder;
    Frame frame;
    frame.pixels.fill(0x31);
    CHECK(!decoder.decode(
        packet(RefinementOnly),
        frame.output(16, 16),
        frame.blockMap()));
    CHECK(frame.pixels[0] == 0x31 && frame.changed[0] == 0);
    // VQ is an 8x8 444 block, incompatible with the 384-sample 420 converter.
    CHECK(!decoder.decode(
        packet(WhiteBlock, Chroma::Yuv420),
        frame.output(16, 16),
        frame.blockMap()));
    CHECK(frame.pixels[0] == 0x31);
    CHECK(decoder.decode(
        packet(DarkBlueBlock),
        frame.output(16, 16),
        frame.blockMap()));
    CHECK(
        frame.pixels[0] ==
        0);   // Y=0, Cb=0 must clamp blue, not index below a table.
    CHECK(decoder.decode(
        packet(WhiteBlock),
        frame.output(16, 16),
        frame.blockMap()));
    CHECK(
        frame.pixels[0] == 255 && frame.pixels[1] == 255 &&
        frame.pixels[2] == 255);
    // A new geometry cannot reuse old second-pass samples.
    frame.pixels.fill(0x31);
    CHECK(!decoder.decode(
        packet(RefinementOnly),
        frame.output(8, 8),
        frame.blockMap()));
    CHECK(frame.pixels[0] == 0x31);
    CHECK(decoder.decode(
        packet(WhiteBlock),
        frame.output(8, 8),
        frame.blockMap()));
    CHECK(!decoder.decode(
        packet(WhiteBlock, Chroma::Yuv420),
        frame.output(8, 8),
        frame.blockMap()));
    frame.pixels.fill(0x31);
    CHECK(!decoder.decode(
        packet(RefinementOnly),
        frame.output(8, 8),
        frame.blockMap()));
    CHECK(frame.pixels[0] == 0x31);
    // A clipped edge block writes only the supplied geometry.
    CHECK(decoder.decode(
        packet(WhiteBlock),
        frame.output(3, 5),
        frame.blockMap()));
    CHECK(frame.pixels[std::size_t{3} * 5 * 4 - 1] == 255);
    CHECK(frame.pixels[std::size_t{3} * 5 * 4] == 0x31);
}

void independentReferences()
{
    aspeed::Decoder first;
    aspeed::Decoder second;
    Frame firstFrame, secondFrame;
    CHECK(first.decode(
        packet(WhiteBlock),
        firstFrame.output(8, 8),
        firstFrame.blockMap()));
    CHECK(second.decode(
        packet(BlackBlock),
        secondFrame.output(8, 8),
        secondFrame.blockMap()));
    CHECK(first.decode(
        packet(RefinementOnly),
        firstFrame.output(8, 8),
        firstFrame.blockMap()));
    // Recorded from the former C decoder: the refinement uses this session's
    // white base samples even after another decoder reconstructs a black block.
    CHECK(
        firstFrame.pixels[0] == 129 && firstFrame.pixels[1] == 129 &&
        firstFrame.pixels[2] == 129);
    CHECK(secondFrame.pixels[0] == 0);
}

template <class Function>
void rejects(Function function)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

void bounds()
{
    aspeed::Decoder decoder;
    Frame frame;
    auto input = packet(WhiteBlock);
    auto output = frame.output();
    rejects([&] { decoder.decode(input, output, {frame.changed.data(), 63}); });
    rejects(
        [&]
        {
            decoder.decode(
                input,
                {{frame.pixels.data(), 3}, 1, 1},
                frame.blockMap());
        });
    rejects(
        [&]
        {
            decoder.decode(
                input,
                {{frame.pixels.data(), frame.pixels.size()}, 0, 64},
                frame.blockMap());
        });
    input.byteCount = 9;
    rejects([&] { decoder.decode(input, output, frame.blockMap()); });
    input.byteCount = 7;
    rejects([&] { decoder.decode(input, output, frame.blockMap()); });
    input = packet(WhiteBlock);
    input.quality = 12;
    rejects([&] { decoder.decode(input, output, frame.blockMap()); });
    input = packet(WhiteBlock);
    input.advancedQuality = 12;
    rejects([&] { decoder.decode(input, output, frame.blockMap()); });
    input = packet(WhiteBlock);
    input.words.data = nullptr;
    rejects([&] { decoder.decode(input, output, frame.blockMap()); });
}

std::uint32_t randomWord(std::uint32_t& state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

std::uint64_t corpus()
{
    aspeed::Decoder decoder;
    Frame frame;
    std::array<std::uint32_t, 64> words{};
    std::uint32_t randomState = 1;
    std::uint64_t fingerprint = 14695981039346656037ULL;
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        for (auto& word: words) {
            word = randomWord(randomState);
        }
        frame.pixels.fill(0);
        // Includes quality changes, malformed coefficients and partial words.
        decoder.decode(
            {{words.data(), words.size()},
             8 + iteration % 249,
             iteration % 2 ? Chroma::Yuv420 : Chroma::Yuv444,
             std::uint8_t(iteration % 12),
             std::uint8_t((iteration / 12) % 12)},
            frame.output(),
            frame.blockMap());
        for (auto byte: frame.pixels) {
            fingerprint = (fingerprint ^ byte) * 1099511628211ULL;
        }
        for (auto byte: frame.changed) {
            fingerprint = (fingerprint ^ byte) * 1099511628211ULL;
        }
    }
    return fingerprint;
}

void concurrentSessions()
{
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(4);
    for (unsigned worker = 0; worker < 4; ++worker) {
        workers.emplace_back(
            [&, worker]
            {
                aspeed::Decoder decoder;
                Frame frame;
                ++ready;
                while (!start.load()) {
                    std::this_thread::yield();
                }
                for (unsigned iteration = 0; iteration < 200; ++iteration) {
                    const bool white = (iteration + worker) % 2;
                    auto input = packet(white ? WhiteBlock : BlackBlock);
                    input.quality = std::uint8_t((iteration + worker) % 12);
                    input.advancedQuality =
                        std::uint8_t((iteration * 3 + worker) % 12);
                    CHECK(decoder.decode(
                        input,
                        frame.output(8, 8),
                        frame.blockMap()));
                    CHECK(frame.pixels[0] == (white ? 255 : 0));
                }
            });
    }
    while (ready.load() < 4) {
        std::this_thread::yield();
    }
    start = true;
    for (auto& worker: workers) {
        worker.join();
    }
}
}   // namespace

int main()
{
    regressions();
    bounds();
    independentReferences();
    const auto fingerprint = corpus();
    // Recorded from the C implementation at 67bbd4a before the redesign.
    // Covers every output byte and block-map byte for the 10,000-packet
    // sequence.
    CHECK(fingerprint == 0x06395262332fd2baULL);
    concurrentSessions();
    std::puts(
        "Decoder regressions, C output compatibility and concurrent sessions "
        "passed");
}
