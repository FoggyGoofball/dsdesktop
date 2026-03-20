/*============================================================================
 * pc_host/include/capture.h
 *
 * Desktop screen capture, downscale to 256×192, and palette quantization.
 *==========================================================================*/
#ifndef DSRD_PC_CAPTURE_H
#define DSRD_PC_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the capture subsystem (SDL, GDI, X11, etc.) */
int  capture_init(void);
void capture_shutdown(void);

/* Capture one frame.  Writes 256×192 8-bit paletted pixels into `pixels`
 * and the 256-entry RGB555 palette into `palette`.
 * Returns 0 on success. */
int  capture_frame(uint8_t *pixels, uint16_t *palette);

/* Enable/disable host-side magnifier mode and set zoom/mode/focus.
 * `mode`: 0=cursor follow, 1=stylus pan.
 * `focus_x/focus_y` are DS-space coordinates when `has_focus` is non-zero.
 * Returns 1 if enabled/zoom/mode changed. */
int  capture_set_magnifier(int enabled, int zoom_level,
                           int mode, int focus_x, int focus_y, int has_focus);

#ifdef __cplusplus
}
#endif
#endif
