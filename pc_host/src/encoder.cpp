/*============================================================================
 * pc_host/src/encoder.cpp
 *
 * Video encoder — produces DSRD video packets:
 *
 *   - Keyframes:  full palette + RLE-compressed 8-bit pixels
 *   - Deltas:     list of changed (x,y,color_idx) tuples
 *
 * Delta calculation is performed strictly on the post-processed,
 * downscaled 256×192 paletted buffer — NOT on native resolution.
 *
 * Automatic keyframe fallback:
 *   - Every DSRD_KEYFRAME_EVERY frames
 *   - If changed pixel % > DSRD_DELTA_FALLBACK_PCT
 *==========================================================================*/
#include <cstring>
#include <cstdint>

#include "../../common/protocol.h"
#include "../../common/rle.h"
#include "../include/encoder.h"

static uint8_t  s_prev_pixels[DSRD_FRAME_PIXELS];
static uint16_t s_prev_palette[DSRD_PALETTE_SIZE];
static int      s_frame_counter  = 0;
static int      s_force_keyframe = 0;
static uint16_t s_kf_seq  = 0;
static uint16_t s_delta_seq = 0;

void encoder_init(void)
{
    memset(s_prev_pixels, 0, sizeof(s_prev_pixels));
    memset(s_prev_palette, 0, sizeof(s_prev_palette));
    s_frame_counter  = 0;
    s_force_keyframe = 1;  /* first frame is always a keyframe */
}

void encoder_force_keyframe(void)
{
    s_force_keyframe = 1;
}

/*--------------------------------------------------------------------------
 * Build a keyframe packet
 *------------------------------------------------------------------------*/
static int build_keyframe(const uint8_t *pixels, const uint16_t *palette,
                          uint8_t *out, int cap)
{
    /* Header */
    if (cap < (int)(sizeof(dsrd_header_t) + DSRD_PALETTE_BYTES + 2))
        return 0;

    uint8_t *p = out + sizeof(dsrd_header_t);

    /* Palette (512 bytes) */
    memcpy(p, palette, DSRD_PALETTE_BYTES);
    p += DSRD_PALETTE_BYTES;

    /* RLE-compressed pixels */
    size_t rle_len = dsrd_rle_encode(pixels, DSRD_FRAME_PIXELS,
                                     p, cap - (int)(p - out));
    p += rle_len;

    uint16_t payload_len = (uint16_t)(p - out - sizeof(dsrd_header_t));

    dsrd_header_t *hdr = (dsrd_header_t *)out;
    dsrd_header_init(hdr, PKT_VIDEO_KEYFRAME, s_kf_seq++,
                     payload_len, 0 /* PC */, 0);

    return (int)(sizeof(dsrd_header_t) + payload_len);
}

/*--------------------------------------------------------------------------
 * Build a delta packet
 *------------------------------------------------------------------------*/
static int build_delta(const uint8_t *pixels,
                       uint8_t *out, int cap)
{
    /* First pass: count changed pixels */
    int changed = 0;
    for (int i = 0; i < DSRD_FRAME_PIXELS; i++) {
        if (pixels[i] != s_prev_pixels[i])
            changed++;
    }

    /* If too many changed, signal that caller should send keyframe */
    int pct = (changed * 100) / DSRD_FRAME_PIXELS;
    if (pct >= DSRD_DELTA_FALLBACK_PCT)
        return -1;  /* fallback to keyframe */

    if (changed == 0)
        return 0;   /* nothing to send */

    /* Build delta list */
    int offset = sizeof(dsrd_header_t) + 2;  /* header + uint16 count */
    int max_deltas = (cap - offset) / (int)sizeof(dsrd_delta_pixel_t);
    if (changed > max_deltas) changed = max_deltas;

    uint8_t *p = out + sizeof(dsrd_header_t);

    /* Count placeholder */
    uint16_t *count_ptr = (uint16_t *)p;
    p += 2;

    int written = 0;
    for (int i = 0; i < DSRD_FRAME_PIXELS && written < changed; i++) {
        if (pixels[i] != s_prev_pixels[i]) {
            dsrd_delta_pixel_t *dp = (dsrd_delta_pixel_t *)p;
            dp->x = (uint8_t)(i % DSRD_SCREEN_W);
            dp->y = (uint8_t)(i / DSRD_SCREEN_W);
            dp->color_idx = pixels[i];
            p += sizeof(dsrd_delta_pixel_t);
            written++;
        }
    }
    *count_ptr = (uint16_t)written;

    uint16_t payload_len = (uint16_t)(p - out - sizeof(dsrd_header_t));

    dsrd_header_t *hdr = (dsrd_header_t *)out;
    dsrd_header_init(hdr, PKT_VIDEO_DELTA, s_delta_seq++,
                     payload_len, 0, 0);

    return (int)(sizeof(dsrd_header_t) + payload_len);
}

/*--------------------------------------------------------------------------
 * Public encode API
 *------------------------------------------------------------------------*/
int encoder_encode(const uint8_t *pixels, const uint16_t *palette,
                   uint8_t *out_buf, int out_cap,
                   int *out_len, int *is_keyframe)
{
    *out_len     = 0;
    *is_keyframe = 0;

    int send_kf = s_force_keyframe ||
                  (s_frame_counter % DSRD_KEYFRAME_EVERY == 0);

    if (!send_kf) {
        int delta_len = build_delta(pixels, out_buf, out_cap);
        if (delta_len == -1) {
            /* Fallback to keyframe */
            send_kf = 1;
        } else {
            *out_len = delta_len;
        }
    }

    if (send_kf) {
        *out_len = build_keyframe(pixels, palette, out_buf, out_cap);
        *is_keyframe = 1;
        s_force_keyframe = 0;
    }

    /* Save current frame as reference for next delta */
    memcpy(s_prev_pixels, pixels, DSRD_FRAME_PIXELS);
    memcpy(s_prev_palette, palette, DSRD_PALETTE_BYTES);
    s_frame_counter++;

    return (*out_len > 0) ? 1 : 0;
}
