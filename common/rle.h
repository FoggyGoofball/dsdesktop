/*============================================================================
 * common/rle.h
 *
 * Minimal Run-Length Encoding for 8-bit paletted 256×192 framebuffers.
 * Used by both the PC host (encoder) and DS client (decoder).
 *
 * Format:
 *   [uint8_t run_length] [uint8_t pixel_value]   — run_length 1..128
 *   If high bit set:  literal run of (val & 0x7F)+1 raw bytes follows.
 *==========================================================================*/
#ifndef DSRD_RLE_H
#define DSRD_RLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  Encode `src` (exactly DSRD_FRAME_PIXELS bytes) into `dst`.
 *  Returns number of bytes written into dst.
 *  dst must be at least DSRD_FRAME_PIXELS*2 bytes (worst case). */
static inline size_t dsrd_rle_encode(const uint8_t *src, size_t src_len,
                                     uint8_t *dst, size_t dst_cap)
{
    size_t si = 0, di = 0;
    while (si < src_len && di + 2 <= dst_cap) {
        uint8_t val = src[si];
        size_t  run = 1;
        while (si + run < src_len && run < 128 && src[si + run] == val)
            run++;
        dst[di++] = (uint8_t)run;
        dst[di++] = val;
        si += run;
    }
    return di;
}

/*  Decode into `dst` (must be at least DSRD_FRAME_PIXELS bytes).
 *  Returns number of bytes written into dst. */
static inline size_t dsrd_rle_decode(const uint8_t *src, size_t src_len,
                                     uint8_t *dst, size_t dst_cap)
{
    size_t si = 0, di = 0;
    while (si + 1 < src_len && di < dst_cap) {
        uint8_t run = src[si++];
        uint8_t val = src[si++];
        while (run-- && di < dst_cap)
            dst[di++] = val;
    }
    return di;
}

#ifdef __cplusplus
}
#endif
#endif /* DSRD_RLE_H */
