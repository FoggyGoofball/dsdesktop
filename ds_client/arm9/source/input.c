/*============================================================================
 * ds_client/arm9/source/input.c
 *
 * Polls DS buttons and touch screen every frame.
 * Touch is routed through sub_ui to determine whether the stylus is on
 * the trackpad, the keyboard, or a UI button.  Only trackpad touches
 * produce mouse telemetry; keyboard touches produce key events.
 *
 * The remap table is owned by sub_ui (editable via the remap overlay)
 * and queried here with sub_ui_get_remap_table().
 *==========================================================================*/
#include <nds.h>
#include <string.h>
#include <stdio.h>

#include "input.h"
#include "sub_ui.h"
#include "keyboard_ui.h"
#include "config.h"
#include "../../../common/protocol.h"

/*--------------------------------------------------------------------------
 * Current telemetry snapshot
 *------------------------------------------------------------------------*/
static dsrd_telemetry_t s_telem;
static uint8_t          s_active_remap_id = 0;

/*--------------------------------------------------------------------------*/
void dsrd_input_init(void)
{
    memset(&s_telem, 0, sizeof(s_telem));
}

/*--------------------------------------------------------------------------
 * Check remap combos from the sub_ui-owned table using bitwise logic.
 * A combo matches when (keys_held & combo_mask) == combo_mask.
 * Longer (more specific) combos are checked first via reverse iteration.
 *------------------------------------------------------------------------*/
static uint8_t evaluate_remaps(uint32_t keys_held)
{
    int count = 0;
    const dsrd_remap_entry_t *table = sub_ui_get_remap_table(&count);

    for (int i = count - 1; i >= 0; i--) {
        uint32_t mask = table[i].ds_mask;
        if ((keys_held & mask) == mask) {
            return table[i].virtual_output;
        }
    }
    return 0;
}

/*--------------------------------------------------------------------------*/
void dsrd_input_poll(void)
{
    scanKeys();

    uint32_t keys_held = keysHeld();
    s_telem.buttons_held = keys_held;

    /* ---- Stylus / trackpad ------------------------------------------ */
    touchPosition touch;
    touchRead(&touch);

    if (keys_held & KEY_TOUCH) {
        /* Route through sub_ui to classify the touch zone */
        sub_touch_zone_t zone =
            sub_ui_process_touch(touch.px, touch.py, 1);

        if (zone == TOUCH_ZONE_TRACKPAD) {
            /* Only trackpad touches generate mouse telemetry */
            s_telem.touch_x    = touch.px;
            s_telem.touch_y    = touch.py;
            s_telem.touch_down = 1;
        } else {
            /* Touch was consumed by keyboard, button, or remap UI */
            s_telem.touch_down = 0;
        }
    } else {
        s_telem.touch_down = 0;
        sub_ui_process_touch(0, 0, 0);
    }

    /* ---- On-screen keyboard ----------------------------------------- */
    s_telem.kbd_scancode = sub_ui_get_key();

    /* ---- Button remap evaluation ------------------------------------ */
    s_telem.remap_id  = evaluate_remaps(keys_held);
    s_active_remap_id = s_telem.remap_id;

    /* ---- Runtime toggle combos -------------------------------------- */
    /* SELECT+L = toggle audio, SELECT+R = toggle HMAC */
    if ((keys_held & KEY_SELECT) && (keys_held & KEY_L)) {
        if (keysDown() & KEY_L) {
            g_cfg.audio_enabled ^= 1;
            iprintf("Audio: %s\n", g_cfg.audio_enabled ? "ON" : "OFF");
        }
    }
    if ((keys_held & KEY_SELECT) && (keys_held & KEY_R)) {
        if (keysDown() & KEY_R) {
            g_cfg.hmac_enabled ^= 1;
            iprintf("HMAC: %s\n", g_cfg.hmac_enabled ? "ON" : "OFF");
        }
    }
}

void dsrd_input_fill_telemetry(dsrd_telemetry_t *tel)
{
    memcpy(tel, &s_telem, sizeof(dsrd_telemetry_t));
}
