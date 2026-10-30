#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// The vendor decoder uses process-global working tables. Initialize once and
// serialize ALL calls, including context destruction (Decoder owns that lock).
// Contexts retain each session's quantization and second-pass reference state.
void aspeed_init(void);
void* aspeed_ctx_new(void);
void aspeed_ctx_free(void* context);
void aspeed_decode_ctx_map(
    void* context,
    uint32_t* words,
    int byte_length,
    unsigned char* bgra,
    int width,
    int height,
    unsigned mode420,
    unsigned quality,
    unsigned advanced_quality,
    unsigned char* changed_blocks,
    int map_length);
#ifdef __cplusplus
}
#endif
