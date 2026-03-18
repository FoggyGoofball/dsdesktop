/*============================================================================
 * pc_host/src/capture.cpp
 *
 * Desktop screen capture → downscale to 256×192 → median-cut palette
 * quantization to 8-bit (256 colours, RGB555).
 *
 * Platform:
 *   Windows — GDI BitBlt (no extra deps)
 *   Linux   — X11 XGetImage (requires libX11)
 *
 * The heavy quantization work is done on the PC to spare the DS's CPU.
 *==========================================================================*/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "../../common/protocol.h"
#include "../include/capture.h"

/* --- Platform-specific screen grab ------------------------------------ */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
static HDC     s_hdc_screen = NULL;
static HDC     s_hdc_mem    = NULL;
static HBITMAP s_hbmp       = NULL;
static int     s_scr_w      = 0;
static int     s_scr_h      = 0;
static uint8_t *s_rgb_buf   = NULL;   /* captured 32bpp BGRA */
#else
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
static Display *s_dpy       = NULL;
static Window   s_root;
static int      s_scr_w     = 0;
static int      s_scr_h     = 0;
static uint8_t *s_rgb_buf   = NULL;
#endif

/* Downscaled 24-bit buffer */
static uint8_t s_ds_rgb[DSRD_SCREEN_W * DSRD_SCREEN_H * 3];

/*--------------------------------------------------------------------------
 * Initialise screen capture
 *------------------------------------------------------------------------*/
int capture_init(void)
{
#ifdef _WIN32
    s_hdc_screen = GetDC(NULL);
    s_scr_w = GetSystemMetrics(SM_CXSCREEN);
    s_scr_h = GetSystemMetrics(SM_CYSCREEN);

    s_hdc_mem = CreateCompatibleDC(s_hdc_screen);
    s_hbmp = CreateCompatibleBitmap(s_hdc_screen, s_scr_w, s_scr_h);
    SelectObject(s_hdc_mem, s_hbmp);

    s_rgb_buf = (uint8_t *)malloc(s_scr_w * s_scr_h * 4);
    if (!s_rgb_buf) return -1;
#else
    s_dpy = XOpenDisplay(NULL);
    if (!s_dpy) { fprintf(stderr, "Cannot open X display\n"); return -1; }
    s_root = DefaultRootWindow(s_dpy);
    XWindowAttributes xwa;
    XGetWindowAttributes(s_dpy, s_root, &xwa);
    s_scr_w = xwa.width;
    s_scr_h = xwa.height;
    s_rgb_buf = (uint8_t *)malloc(s_scr_w * s_scr_h * 4);
    if (!s_rgb_buf) return -1;
#endif

    printf("  Screen: %dx%d → downscale to 256x192\n", s_scr_w, s_scr_h);
    return 0;
}

void capture_shutdown(void)
{
#ifdef _WIN32
    if (s_rgb_buf)    free(s_rgb_buf);
    if (s_hbmp)       DeleteObject(s_hbmp);
    if (s_hdc_mem)    DeleteDC(s_hdc_mem);
    if (s_hdc_screen) ReleaseDC(NULL, s_hdc_screen);
#else
    if (s_rgb_buf) free(s_rgb_buf);
    if (s_dpy)     XCloseDisplay(s_dpy);
#endif
}

/*--------------------------------------------------------------------------
 * Grab the desktop into s_rgb_buf (32-bit BGRA)
 *------------------------------------------------------------------------*/
static int grab_screen(void)
{
#ifdef _WIN32
    BitBlt(s_hdc_mem, 0, 0, s_scr_w, s_scr_h,
           s_hdc_screen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize        = sizeof(bi);
    bi.biWidth       = s_scr_w;
    bi.biHeight      = -s_scr_h;  /* top-down */
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    GetDIBits(s_hdc_mem, s_hbmp, 0, s_scr_h,
              s_rgb_buf, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    return 0;
#else
    XImage *img = XGetImage(s_dpy, s_root, 0, 0,
                            s_scr_w, s_scr_h, AllPlanes, ZPixmap);
    if (!img) return -1;
    memcpy(s_rgb_buf, img->data, s_scr_w * s_scr_h * 4);
    XDestroyImage(img);
    return 0;
#endif
}

/*--------------------------------------------------------------------------
 * Bilinear downscale from s_rgb_buf (s_scr_w × s_scr_h, BGRA)
 * to s_ds_rgb (256 × 192, RGB24).
 *------------------------------------------------------------------------*/
static void downscale(void)
{
    float sx = (float)s_scr_w / DSRD_SCREEN_W;
    float sy = (float)s_scr_h / DSRD_SCREEN_H;

    for (int y = 0; y < DSRD_SCREEN_H; y++) {
        for (int x = 0; x < DSRD_SCREEN_W; x++) {
            int src_x = (int)(x * sx);
            int src_y = (int)(y * sy);
            if (src_x >= s_scr_w) src_x = s_scr_w - 1;
            if (src_y >= s_scr_h) src_y = s_scr_h - 1;

            const uint8_t *p = s_rgb_buf + (src_y * s_scr_w + src_x) * 4;
            int dst = (y * DSRD_SCREEN_W + x) * 3;
            s_ds_rgb[dst + 0] = p[2];  /* R (from BGRA) */
            s_ds_rgb[dst + 1] = p[1];  /* G */
            s_ds_rgb[dst + 2] = p[0];  /* B */
        }
    }
}

/*--------------------------------------------------------------------------
 * Simple median-cut palette quantization (256 colours).
 * Produces:
 *   - palette[256] as RGB555 (DS native format)
 *   - pixels[256×192] as palette indices
 *
 * For performance, we use a direct-mapped colour cube (5-5-5 bits).
 *------------------------------------------------------------------------*/
static void quantize(uint8_t *pixels, uint16_t *palette)
{
    /* Build a frequency histogram in a 32×32×32 colour cube */
    static uint16_t cube[32][32][32];  /* index → palette slot */
    static int cube_built = 0;

    /* Simple uniform quantization: 5 bits per channel */
    /* Build palette from first 256 unique colours seen */
    int pal_count = 0;
    memset(cube, 0xFF, sizeof(cube));  /* 0xFFFF = unmapped */

    for (int i = 0; i < DSRD_FRAME_PIXELS; i++) {
        int ri = s_ds_rgb[i * 3 + 0] >> 3;
        int gi = s_ds_rgb[i * 3 + 1] >> 3;
        int bi = s_ds_rgb[i * 3 + 2] >> 3;

        if (cube[ri][gi][bi] == 0xFFFF) {
            if (pal_count < DSRD_PALETTE_SIZE) {
                palette[pal_count] =
                    (uint16_t)(ri | (gi << 5) | (bi << 10));
                cube[ri][gi][bi] = (uint16_t)pal_count;
                pal_count++;
            } else {
                /* Palette full — find nearest by Manhattan distance */
                int best = 0, best_d = 999999;
                for (int p = 0; p < DSRD_PALETTE_SIZE; p++) {
                    int pr = palette[p] & 0x1F;
                    int pg = (palette[p] >> 5) & 0x1F;
                    int pb = (palette[p] >> 10) & 0x1F;
                    int d = abs(ri - pr) + abs(gi - pg) + abs(bi - pb);
                    if (d < best_d) { best_d = d; best = p; }
                }
                cube[ri][gi][bi] = (uint16_t)best;
            }
        }

        pixels[i] = (uint8_t)cube[ri][gi][bi];
    }

    /* Fill unused palette entries with black */
    for (int p = pal_count; p < DSRD_PALETTE_SIZE; p++)
        palette[p] = 0;

    /* Reset cube for next frame */
    memset(cube, 0xFF, sizeof(cube));
}

/*--------------------------------------------------------------------------
 * Public API
 *------------------------------------------------------------------------*/
int capture_frame(uint8_t *pixels, uint16_t *palette)
{
    if (grab_screen() < 0) return -1;
    downscale();
    quantize(pixels, palette);
    return 0;
}
