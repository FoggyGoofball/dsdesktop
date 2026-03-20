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
 * Evaluate fixed per-button remaps and emit up to two active outputs.
 *------------------------------------------------------------------------*/
static void remap_add_unique(uint8_t v, uint8_t *o1, uint8_t *o2)
{
    if (v == 0) return;
    if (*o1 == v || *o2 == v) return;
    if (*o1 == 0) { *o1 = v; return; }
    if (*o2 == 0) { *o2 = v; return; }
}

static void evaluate_remaps(uint32_t keys_held, uint8_t *out1, uint8_t *out2)
{
    *out1 = 0;
    *out2 = 0;

    int count = 0;
    const dsrd_remap_entry_t *table = sub_ui_get_remap_table(&count);

    for (int i = 0; i < count; i++) {
        uint32_t mask = table[i].ds_mask;
        if (mask == 0) continue;

        /* Per-button semantics: a row is active when its DS button is held. */
        if ((keys_held & mask) == mask) {
            remap_add_unique(table[i].virtual_output, out1, out2);
            remap_add_unique(sub_ui_get_secondary_output(i), out1, out2);
        }
    }
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
    uint8_t remap_primary = 0;
    uint8_t remap_secondary = 0;
    evaluate_remaps(keys_held, &remap_primary, &remap_secondary);
    s_telem.remap_id  = remap_primary;
    s_telem._pad      = remap_secondary;
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
