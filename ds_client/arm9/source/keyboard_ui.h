/*============================================================================
 * ds_client/arm9/source/keyboard_ui.h
 *
 * Thin compatibility shim — the on-screen keyboard is now managed by
 * sub_ui.c.  These functions delegate to sub_ui so that existing callers
 * (input.c, main.c) continue to compile without changes.
 *==========================================================================*/
#ifndef DSRD_KEYBOARD_UI_H
#define DSRD_KEYBOARD_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void    dsrd_keyboard_init(void);
void    dsrd_keyboard_poll(void);
uint8_t dsrd_keyboard_get_key(void);   /* returns ASCII or 0 */

#ifdef __cplusplus
}
#endif
#endif
