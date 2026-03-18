/*============================================================================
 * ds_client/arm9/source/keyboard_ui.c
 *
 * Thin compatibility shim.  All real keyboard / sub-screen UI logic
 * lives in sub_ui.c.  These functions exist so that main.c and input.c
 * can call the same API without modification.
 *==========================================================================*/
#include "keyboard_ui.h"
#include "sub_ui.h"

void dsrd_keyboard_init(void)
{
    /* sub_ui_init() is called directly from main.c now;
       nothing extra needed here. */
}

void dsrd_keyboard_poll(void)
{
    /* Animation / redraw is driven by sub_ui_update() from main.c */
}

uint8_t dsrd_keyboard_get_key(void)
{
    return sub_ui_get_key();
}
