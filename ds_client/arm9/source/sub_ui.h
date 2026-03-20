/*============================================================================
 * ds_client/arm9/source/sub_ui.h
 *
 * Bottom (sub) screen UI controller.
 *
 * Layout:
 *   ┌──────────────────────────────────┐  y=0
 *   │          [⚙ Remap]  (top-right)  │
 *   │                                  │
 *   │         TRACKPAD ZONE            │
 *   │     (always ≥ 30% of screen)     │
 *   │                                  │
 *   ├──────────────────────────────────┤  y = kbd_top (slides)
 *   │         ON-SCREEN KEYBOARD       │
 *   │        (≤ 70% of screen)         │
 *   ├──────────────────────────────────┤  y=176
 *   │         [ ⌨ Toggle KB ]          │
 *   └──────────────────────────────────┘  y=192
 *
 *   When the remap overlay is active it covers the entire sub screen.
 *==========================================================================*/
#ifndef DSRD_SUB_UI_H
#define DSRD_SUB_UI_H

#include <stdint.h>
#include "../../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------
 * Touch result — tells the input layer what the touch event means
 *------------------------------------------------------------------------*/
typedef enum {
    TOUCH_ZONE_NONE     = 0,
    TOUCH_ZONE_TRACKPAD = 1,
    TOUCH_ZONE_KEYBOARD = 2,
    TOUCH_ZONE_KB_BTN   = 3,
    TOUCH_ZONE_REMAP_BTN= 4,
    TOUCH_ZONE_REMAP_UI = 5,
} sub_touch_zone_t;

/*--------------------------------------------------------------------------
 * Public API
 *------------------------------------------------------------------------*/

/* Initialise sub-screen hardware (BG layers, palettes, console) */
void sub_ui_init(void);

/* Per-frame update — animate slide, redraw dirty regions */
void sub_ui_update(void);

/* Classify a touch coordinate into a UI zone.
 * If the touch falls on the keyboard, the key is consumed internally
 * and can be retrieved via sub_ui_get_key(). */
sub_touch_zone_t sub_ui_process_touch(uint16_t px, uint16_t py,
                                      int stylus_down);

/* Get the last key pressed on the on-screen keyboard (ASCII, or 0) */
uint8_t sub_ui_get_key(void);

/* Is the keyboard currently visible (even partially)? */
int sub_ui_kbd_visible(void);

/* Is the remap overlay currently active? */
int sub_ui_remap_active(void);

/* Access the live remap table (for input.c to evaluate) */
const dsrd_remap_entry_t *sub_ui_get_remap_table(int *count);

/* Get secondary virtual output id for remap row i (0 if none) */
uint8_t sub_ui_get_secondary_output(int idx);

#ifdef __cplusplus
}
#endif
#endif /* DSRD_SUB_UI_H */
