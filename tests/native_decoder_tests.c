// No Qt or C++ runtime: the entire decoder/test boundary can be MSan-instrumented.
#include "core/aspeed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)

static uint32_t random_word(uint32_t* state)
{
    // Fixed portable PRNG, so failures reproduce with any libc/platform.
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

int main(void)
{
    aspeed_init();
    void* context = aspeed_ctx_new();
    CHECK(context != NULL);
    unsigned char pixels[64 * 64 * 4];
    unsigned char changed[64];
    // Little-endian words after the four-byte Qt packet header.
    uint32_t white[] = {0x53200000, 0}; // 4:4:4 VQ cached white, then frame end.
    uint32_t dark_blue[] = {0x58000001, 0x20000000}; // Y=0/Cb=0: negative RGB conversion.
    uint32_t second_pass[] = {0x20000000, 0}; // No preceding base block.
    memset(pixels, 0x31, sizeof(pixels));
    memset(changed, 0, sizeof(changed));
    aspeed_decode_ctx_map(context, second_pass, 8, pixels, 16, 16, 0, 0, 0, changed, 4);
    CHECK(pixels[0] == 0x31 && changed[0] == 0); // Reject an uninitialized reference.
    aspeed_decode_ctx_map(context, white, 8, pixels, 16, 16, 1, 0, 0, changed, 1);
    CHECK(pixels[0] == 0x31 && changed[0] == 0); // VQ cannot use a 4:2:0 buffer.
    aspeed_decode_ctx_map(context, dark_blue, 8, pixels, 16, 16, 0, 0, 0, changed, 4);
    CHECK(pixels[0] == 0 && changed[0] == 1); // Blue must clamp to zero.
    aspeed_decode_ctx_map(context, white, 8, pixels, 16, 16, 0, 0, 0, changed, 4);
    CHECK(pixels[0] == 255 && pixels[1] == 255 && pixels[2] == 255);
    aspeed_ctx_free(context);

    context = aspeed_ctx_new();
    CHECK(context != NULL);
    uint32_t state = 1;
    uint32_t packet[64];
    // Exercise malformed coefficients, truncated streams, modes and qualities.
    // Sanitizers must report failures rather than silently accepting UB.
    for (int iteration = 0; iteration < 10000; ++iteration) {
        for (int word = 0; word < 64; ++word) packet[word] = random_word(&state);
        memset(pixels, 0, sizeof(pixels));
        memset(changed, 0, sizeof(changed));
        aspeed_decode_ctx_map(context, packet, 8 + (iteration % 249), pixels,
            64, 64, iteration % 2, iteration % 12, (iteration / 12) % 12,
            changed, sizeof(changed));
    }
    aspeed_ctx_free(context);
    puts("Native decoder regressions and deterministic malformed-packet corpus passed");
    return 0;
}
