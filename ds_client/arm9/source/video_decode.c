/*============================================================================
 * ds_client/arm9/source/video_decode.c
 *
 * Decodes incoming 8-bit paletted video (keyframes + deltas) and blits
 * to the top screen using the DS hardware palette LUT.
 *
 * BG2 is configured as a 256-colour bitmap in Mode 5.
 * - VRAM_A  = pixel data  (256×256 stride, 256×192 visible)
 * - BG_PALETTE = 256 × RGB555 entries
 *==========================================================================*/
#include <nds.h>
#include <string.h>

#include "video_decode.h"
#include "../../../common/protocol.h"
#include "../../../common/rle.h"

/*--------------------------------------------------------------------------
 * Static framebuffer mirror in main RAM for delta accumulation.
 * We blit from here to VRAM after each update.
 *------------------------------------------------------------------------*/
static uint8_t s_framebuf[DSRD_FRAME_PIXELS] __attribute__((aligned(4)));
static volatile int s_dirty = 0;

/*--------------------------------------------------------------------------*/
void dsrd_video_init(void)
{
    memset(s_framebuf, 0, sizeof(s_framebuf));
    memset(BG_PALETTE, 0, DSRD_PALETTE_BYTES);
    s_dirty = 0;
}

/*--------------------------------------------------------------------------
 * Blit the 8-bit framebuffer to VRAM BG2 (stride = 256 = screen width).
 * We use DMA channel 3 for speed and flush cache first.
 *------------------------------------------------------------------------*/
void dsrd_video_process(void)
{
    if (!s_dirty) return;
    s_dirty = 0;

    DC_FlushRange(s_framebuf, sizeof(s_framebuf));

    /* BG2 VRAM base = bgGetGfxPtr(bg) = BG_BMP_RAM(0) */
    dmaCopy(s_framebuf, BG_BMP_RAM(0), DSRD_FRAME_PIXELS);
}

/*--------------------------------------------------------------------------
 * Keyframe decoder
 *
 * Payload layout:
 *   [palette: 512 bytes, 256 × RGB555]
 *   [RLE-compressed pixels: remainder]
 *------------------------------------------------------------------------*/
void dsrd_video_decode_keyframe(const uint8_t *payload, uint16_t len)
{
    if (len < DSRD_PALETTE_BYTES + 2) return;

    /* Load palette into HW palette RAM */
    const uint16_t *pal = (const uint16_t *)payload;
    DC_FlushRange((void *)pal, DSRD_PALETTE_BYTES);
    dmaCopy(pal, BG_PALETTE, DSRD_PALETTE_BYTES);

    /* Decode RLE pixels */
    const uint8_t *rle_data = payload + DSRD_PALETTE_BYTES;
    uint16_t rle_len = len - DSRD_PALETTE_BYTES;

    dsrd_rle_decode(rle_data, rle_len, s_framebuf, DSRD_FRAME_PIXELS);
    s_dirty = 1;
}

/*--------------------------------------------------------------------------
 * Delta decoder
 *
 * Payload layout:
 *   uint16_t count;        // number of changed pixels
 *   dsrd_delta_pixel_t deltas[count];
 *------------------------------------------------------------------------*/
void dsrd_video_decode_delta(const uint8_t *payload, uint16_t len)
{
    if (len < 2) return;

    uint16_t count = *(const uint16_t *)payload;
    const dsrd_delta_pixel_t *deltas =
        (const dsrd_delta_pixel_t *)(payload + 2);

    uint16_t max_deltas =
        (len - 2) / sizeof(dsrd_delta_pixel_t);
    if (count > max_deltas) count = max_deltas;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t x = deltas[i].x;
        uint16_t y = deltas[i].y;
        if (x < DSRD_SCREEN_W && y < DSRD_SCREEN_H) {
            s_framebuf[y * DSRD_SCREEN_W + x] = deltas[i].color_idx;
        }
    }

    s_dirty = 1;
}
