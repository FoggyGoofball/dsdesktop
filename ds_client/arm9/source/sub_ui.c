/*============================================================================
 * ds_client/arm9/source/sub_ui.c
 *
 * Bottom-screen master UI controller.
 *
 * The sub screen is exclusively a touchpad with slide-out overlays:
 *
 *   - DEFAULT state : full-screen trackpad.
 *     Bottom bar (y 176-191) has a centred "KB" button (toggle keyboard)
 *     and a top-right "Remap" button (y 0-15, x 232-255).
 *
 *   - KEYBOARD state: on-screen keyboard slides up from the bottom.
 *     Max keyboard height = 134 px (70 % of 192).
 *     Trackpad remains above it (≥ 58 px = 30 %).
 *     Tapping "KB" button again hides the keyboard.
 *
 *   - REMAP state: full-screen overlay listing every DS button with its
 *     current virtual-output mapping.  Tap a row to change its binding.
 *     Tap the "Done" button to dismiss.
 *
 * Rendering uses BG0 (text console via libnds consoleDemoInit on sub)
 * because the DS has limited VRAM for a second bitmap layer.  All UI
 * elements are drawn with iprintf / consoleClear.
 *
 * No dynamic memory allocation — all buffers are static.
 *==========================================================================*/
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "sub_ui.h"
#include "../../../common/protocol.h"

/*--------------------------------------------------------------------------
 * Screen geometry constants
 *------------------------------------------------------------------------*/
#define SUB_W            256
#define SUB_H            192
#define TILE_H           8        /* one text row = 8 px            */
#define CONSOLE_ROWS     24       /* 192 / 8                        */
#define CONSOLE_COLS     32       /* 256 / 8                        */

/* Keyboard slide parameters */
#define KBD_MAX_ROWS     16       /* max rows the keyboard occupies */
#define KBD_MAX_HEIGHT   (KBD_MAX_ROWS * TILE_H)  /* 128 px ≤ 134 limit */
#define KBD_MIN_TRACK_PX 58       /* 30 % of 192 = 57.6 → 58       */
#define KBD_TOP_ROW      (CONSOLE_ROWS - KBD_MAX_ROWS)  /* row 8    */

/* Button hit zones (pixel coords) */
#define KB_BTN_X1        96
#define KB_BTN_X2        160
#define KB_BTN_Y1        176
#define KB_BTN_Y2        191

#define REMAP_BTN_X1     232
#define REMAP_BTN_X2     255
#define REMAP_BTN_Y1     0
#define REMAP_BTN_Y2     15

/* Slide animation speed (pixels per frame) */
#define SLIDE_SPEED      8

/*--------------------------------------------------------------------------
 * UI state machine
 *------------------------------------------------------------------------*/
typedef enum {
    UI_STATE_TRACKPAD = 0,
    UI_STATE_KBD_SLIDING_IN,
    UI_STATE_KBD_OPEN,
    UI_STATE_KBD_SLIDING_OUT,
    UI_STATE_REMAP,
} ui_state_t;

static ui_state_t s_state = UI_STATE_TRACKPAD;

/* Current keyboard top-edge in pixels (starts at SUB_H = off-screen) */
static int s_kbd_top_px = SUB_H;

/*--------------------------------------------------------------------------
 * On-screen keyboard data
 *------------------------------------------------------------------------*/
/* We draw the keyboard manually into the console text rows because
   keyboardDemoInit() takes over the entire sub screen.  Instead we
   implement a simple 4-row QWERTY layout rendered with iprintf. */

#define KBD_ROWS  4
#define KBD_KEY_W 2     /* columns per key cell */

static const char *s_kbd_layout[KBD_ROWS] = {
    "1234567890-= ",
    " qwertyuiop[]",
    " asdfghjkl;' ",
    " zxcvbnm,./ E",   /* E = Enter */
};

/* Shift variant */
static const char *s_kbd_layout_shift[KBD_ROWS] = {
    "!@#$%^&*()_+ ",
    " QWERTYUIOP{}",
    " ASDFGHJKL:\" ",
    " ZXCVBNM<>? E",
};

static int s_shift = 0;
static volatile uint8_t s_last_key = 0;

/*--------------------------------------------------------------------------
 * Remap overlay data
 *------------------------------------------------------------------------*/
/* Statically allocated remap table — same format as protocol.h */
static dsrd_remap_entry_t s_remap_table[DSRD_MAX_REMAPS];
static int                s_remap_count = 0;

/* Which remap row is selected for editing (-1 = none) */
static int s_remap_sel = -1;

/* Button names for display (indexed by bit position in keysHeld) */
typedef struct { uint32_t mask; const char *name; } btn_name_t;

static const btn_name_t s_btn_names[] = {
    { KEY_A,      "A"      },
    { KEY_B,      "B"      },
    { KEY_SELECT, "SELECT" },
    { KEY_START,  "START"  },
    { KEY_RIGHT,  "RIGHT"  },
    { KEY_LEFT,   "LEFT"   },
    { KEY_UP,     "UP"     },
    { KEY_DOWN,   "DOWN"   },
    { KEY_R,      "R"      },
    { KEY_L,      "L"      },
    { KEY_X,      "X"      },
    { KEY_Y,      "Y"      },
};
#define NUM_BUTTONS (sizeof(s_btn_names) / sizeof(s_btn_names[0]))

/* Debounce tracking for button presses */
static int s_touch_debounce = 0;

/*--------------------------------------------------------------------------
 * PrintConsole pointer for sub screen
 *------------------------------------------------------------------------*/
static PrintConsole s_console;
static int s_console_inited = 0;

/*--------------------------------------------------------------------------
 * Forward declarations
 *------------------------------------------------------------------------*/
static void draw_trackpad_ui(void);
static void draw_keyboard(void);
static void draw_remap_overlay(void);
static void clear_screen(void);
static char kbd_hit_test(uint16_t px, uint16_t py);

/*--------------------------------------------------------------------------
 * Default remap table
 *------------------------------------------------------------------------*/
static void load_default_remaps(void)
{
    s_remap_table[0].ds_mask        = KEY_B | KEY_UP;
    s_remap_table[0].virtual_output = 100;

    s_remap_table[1].ds_mask        = KEY_L | KEY_R;
    s_remap_table[1].virtual_output = 101;

    s_remap_table[2].ds_mask        = KEY_A | KEY_DOWN;
    s_remap_table[2].virtual_output = 102;

    s_remap_count = 3;
}

/*==========================================================================
 * PUBLIC API
 *========================================================================*/

void sub_ui_init(void)
{
    /* Sub screen: text console on BG0 */
    videoSetModeSub(MODE_0_2D);
    vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&s_console, 0, BgType_Text4bpp, BgSize_T_256x256,
                31, 0, false, true);
    consoleSelect(&s_console);
    s_console_inited = 1;

    s_state      = UI_STATE_TRACKPAD;
    s_kbd_top_px = SUB_H;
    s_shift      = 0;
    s_last_key   = 0;
    s_remap_sel  = -1;

    load_default_remaps();
    draw_trackpad_ui();
}

/*--------------------------------------------------------------------------
 * Per-frame update — animate keyboard slide
 *------------------------------------------------------------------------*/
void sub_ui_update(void)
{
    if (!s_console_inited) return;
    consoleSelect(&s_console);

    /* Debounce cooldown */
    if (s_touch_debounce > 0)
        s_touch_debounce--;

    switch (s_state) {
    case UI_STATE_KBD_SLIDING_IN:
        s_kbd_top_px -= SLIDE_SPEED;
        if (s_kbd_top_px <= (int)(KBD_MIN_TRACK_PX)) {
            s_kbd_top_px = KBD_MIN_TRACK_PX;
            s_state = UI_STATE_KBD_OPEN;
        }
        clear_screen();
        draw_trackpad_ui();
        draw_keyboard();
        break;

    case UI_STATE_KBD_SLIDING_OUT:
        s_kbd_top_px += SLIDE_SPEED;
        if (s_kbd_top_px >= SUB_H) {
            s_kbd_top_px = SUB_H;
            s_state = UI_STATE_TRACKPAD;
            clear_screen();
            draw_trackpad_ui();
        } else {
            clear_screen();
            draw_trackpad_ui();
            draw_keyboard();
        }
        break;

    case UI_STATE_KBD_OPEN:
        /* static — no animation needed, but redraw if dirty */
        break;

    case UI_STATE_REMAP:
        /* Continuously redraw so "Holding:" display stays live */
        draw_remap_overlay();
        break;

    case UI_STATE_TRACKPAD:
    default:
        break;
    }
}

/*--------------------------------------------------------------------------
 * Process a touch event and return which zone was hit
 *------------------------------------------------------------------------*/
sub_touch_zone_t sub_ui_process_touch(uint16_t px, uint16_t py,
                                      int stylus_down)
{
    if (!stylus_down)
        return TOUCH_ZONE_NONE;

    /* ---- Remap overlay mode ----------------------------------------- */
    if (s_state == UI_STATE_REMAP) {
        int row = py / TILE_H;

        /* "Done / Close" button — row 22 */
        if (row >= 22) {
            if (s_touch_debounce == 0) {
                s_touch_debounce = 15;
                s_state = UI_STATE_TRACKPAD;
                s_remap_sel = -1;
                clear_screen();
                draw_trackpad_ui();
            }
            return TOUCH_ZONE_REMAP_UI;
        }

        /* "SET COMBO" button — when a row is selected, check the
           SET zone rendered at info_row+2.  Capture held DS buttons. */
        if (s_remap_sel >= 0 && s_remap_sel < s_remap_count) {
            int info_row = 4 + s_remap_count + 2;
            if (info_row > 20) info_row = 20;
            int set_row = info_row + 2;
            if (row == set_row && px >= 8 && px < 112) {
                if (s_touch_debounce == 0) {
                    scanKeys();
                    uint32_t held = keysHeld() & ~KEY_TOUCH;
                    if (held != 0) {
                        s_remap_table[s_remap_sel].ds_mask = held;
                    }
                    s_touch_debounce = 15;
                    draw_remap_overlay();
                }
                return TOUCH_ZONE_REMAP_UI;
            }
        }

        /* Header rows are 0-2, column header row 3,
           remap rows start at row 4 */
        int remap_idx = row - 4;

        /* Remap entry rows */
        if (remap_idx >= 0 && remap_idx < s_remap_count) {
            if (s_remap_sel == remap_idx) {
                /* Already selected — cycle virtual output +1 */
                if (s_touch_debounce == 0) {
                    s_remap_table[remap_idx].virtual_output++;
                    if (s_remap_table[remap_idx].virtual_output > 120)
                        s_remap_table[remap_idx].virtual_output = 100;
                    s_touch_debounce = 10;
                    draw_remap_overlay();
                }
            } else {
                if (s_touch_debounce == 0) {
                    s_remap_sel = remap_idx;
                    s_touch_debounce = 10;
                    draw_remap_overlay();
                }
            }
            return TOUCH_ZONE_REMAP_UI;
        }

        /* "+Add new" row (immediately after the last remap entry) */
        if (remap_idx == s_remap_count && s_remap_count < DSRD_MAX_REMAPS) {
            if (s_touch_debounce == 0) {
                s_remap_table[s_remap_count].ds_mask = KEY_A;
                s_remap_table[s_remap_count].virtual_output = 100;
                s_remap_count++;
                s_touch_debounce = 10;
                draw_remap_overlay();
            }
            return TOUCH_ZONE_REMAP_UI;
        }

        return TOUCH_ZONE_REMAP_UI;
    }

    /* ---- Remap button (top-right corner) ----------------------------- */
    if (px >= REMAP_BTN_X1 && px <= REMAP_BTN_X2 &&
        py <= REMAP_BTN_Y2) {
        if (s_touch_debounce == 0) {
            s_touch_debounce = 15;
            /* If keyboard is open, close it first */
            if (s_state == UI_STATE_KBD_OPEN ||
                s_state == UI_STATE_KBD_SLIDING_IN) {
                s_kbd_top_px = SUB_H;
            }
            s_state = UI_STATE_REMAP;
            s_remap_sel = -1;
            clear_screen();
            draw_remap_overlay();
        }
        return TOUCH_ZONE_REMAP_BTN;
    }

    /* ---- KB toggle button (bottom strip) ----------------------------- */
    if (px >= KB_BTN_X1 && px <= KB_BTN_X2 &&
        py >= KB_BTN_Y1 && py <= KB_BTN_Y2) {
        if (s_touch_debounce == 0) {
            s_touch_debounce = 15;
            if (s_state == UI_STATE_TRACKPAD) {
                s_state = UI_STATE_KBD_SLIDING_IN;
            } else if (s_state == UI_STATE_KBD_OPEN) {
                s_state = UI_STATE_KBD_SLIDING_OUT;
            }
            /* if already sliding, let it finish */
        }
        return TOUCH_ZONE_KB_BTN;
    }

    /* ---- Keyboard zone (when visible) -------------------------------- */
    if ((s_state == UI_STATE_KBD_OPEN ||
         s_state == UI_STATE_KBD_SLIDING_IN) &&
        py >= (uint16_t)s_kbd_top_px && py < KB_BTN_Y1) {
        /* Hit-test against keyboard grid */
        char ch = kbd_hit_test(px, py);
        if (ch) {
            s_last_key = (uint8_t)ch;
        }
        return TOUCH_ZONE_KEYBOARD;
    }

    /* ---- Everything else is trackpad --------------------------------- */
    return TOUCH_ZONE_TRACKPAD;
}

uint8_t sub_ui_get_key(void)
{
    uint8_t k = s_last_key;
    s_last_key = 0;
    return k;
}

int sub_ui_kbd_visible(void)
{
    return (s_state == UI_STATE_KBD_OPEN ||
            s_state == UI_STATE_KBD_SLIDING_IN ||
            s_state == UI_STATE_KBD_SLIDING_OUT);
}

int sub_ui_remap_active(void)
{
    return (s_state == UI_STATE_REMAP);
}

const dsrd_remap_entry_t *sub_ui_get_remap_table(int *count)
{
    if (count) *count = s_remap_count;
    return s_remap_table;
}

/*==========================================================================
 * PRIVATE — Drawing helpers
 *========================================================================*/

static void clear_screen(void)
{
    consoleSelect(&s_console);
    consoleClear();
}

/*--------------------------------------------------------------------------
 * Draw the trackpad-mode UI elements
 *   - "Remap" button label at top-right
 *   - "[ KB ]" button label at bottom-centre
 *   - Faint "Trackpad" label at centre
 *------------------------------------------------------------------------*/
static void draw_trackpad_ui(void)
{
    consoleSelect(&s_console);

    /* Top-right: remap button  (row 0, col 28..31) */
    iprintf("\x1b[0;28H\x1b[33m" "Bind" "\x1b[39m");

    /* Centre: trackpad indicator */
    int mid_row = 11;
    if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN)
        mid_row = (s_kbd_top_px / TILE_H) / 2;
    if (mid_row < 2) mid_row = 2;
    iprintf("\x1b[%d;10H\x1b[37m" "-- Trackpad --" "\x1b[39m", mid_row);

    /* Bottom: KB toggle button (row 22-23, centred) */
    const char *kb_label;
    if (s_state == UI_STATE_KBD_OPEN ||
        s_state == UI_STATE_KBD_SLIDING_IN)
        kb_label = "[ Hide KB ]";
    else
        kb_label = "[ Show KB ]";

    int label_col = (CONSOLE_COLS - (int)strlen(kb_label)) / 2;
    iprintf("\x1b[22;%dH\x1b[36m%s\x1b[39m", label_col, kb_label);
}

/*--------------------------------------------------------------------------
 * Draw the on-screen keyboard at its current slide position
 *------------------------------------------------------------------------*/
static void draw_keyboard(void)
{
    consoleSelect(&s_console);

    int start_row = s_kbd_top_px / TILE_H;
    if (start_row < (int)(KBD_MIN_TRACK_PX / TILE_H))
        start_row = KBD_MIN_TRACK_PX / TILE_H;

    /* Separator line */
    iprintf("\x1b[%d;0H", start_row);
    for (int c = 0; c < CONSOLE_COLS; c++)
        iprintf("-");

    /* "Shift" toggle label */
    iprintf("\x1b[%d;0H\x1b[32m%s\x1b[39m",
            start_row + 1,
            s_shift ? "[SHIFT ON ]" : "[shift off]");

    /* Space bar label */
    iprintf("\x1b[%d;14H\x1b[32m" "[  SPACE  ]" "\x1b[39m",
            start_row + 1);

    /* Keyboard rows */
    const char **layout = s_shift ? s_kbd_layout_shift : s_kbd_layout;
    for (int r = 0; r < KBD_ROWS; r++) {
        int row = start_row + 2 + r;
        if (row >= CONSOLE_ROWS - 2) break;

        iprintf("\x1b[%d;1H", row);
        const char *keys = layout[r];
        int len = (int)strlen(keys);
        for (int k = 0; k < len; k++) {
            char ch = keys[k];
            if (ch == ' ')
                iprintf("  ");
            else if (ch == 'E' && r == KBD_ROWS - 1 && k == len - 1)
                iprintf("\x1b[32m" "En" "\x1b[39m");
            else
                iprintf("%c ", ch);
        }
    }

    /* Backspace label */
    int bs_row = start_row + 2;
    iprintf("\x1b[%d;28H\x1b[31m" "Del" "\x1b[39m", bs_row);
}

/*--------------------------------------------------------------------------
 * Hit-test the keyboard grid — return the ASCII character at (px, py)
 *------------------------------------------------------------------------*/
static char kbd_hit_test(uint16_t px, uint16_t py)
{
    int start_row = s_kbd_top_px / TILE_H;
    if (start_row < (int)(KBD_MIN_TRACK_PX / TILE_H))
        start_row = KBD_MIN_TRACK_PX / TILE_H;

    int row_px = (int)py - (start_row + 1) * TILE_H;

    /* Row 0 = Shift / Space bar row */
    if (row_px >= 0 && row_px < TILE_H) {
        if (px < 88) {
            /* Shift toggle */
            s_shift ^= 1;
            /* Redraw keyboard on next frame */
            clear_screen();
            draw_trackpad_ui();
            draw_keyboard();
            return 0;
        }
        if (px >= 112 && px < 200) {
            return ' ';  /* space bar */
        }
        return 0;
    }

    /* Rows 1-4 = key rows */
    int kr = (row_px / TILE_H) - 1;
    if (kr < 0 || kr >= KBD_ROWS) return 0;

    const char **layout = s_shift ? s_kbd_layout_shift : s_kbd_layout;
    const char *keys = layout[kr];
    int len = (int)strlen(keys);

    /* Backspace zone (right edge, first key row) */
    if (kr == 0 && px >= 224) return 8;  /* ASCII backspace */

    /* Map px to key index: each key occupies 2 console columns = 16 px */
    int ki = ((int)px - 8) / 16;
    if (ki < 0 || ki >= len) return 0;

    char ch = keys[ki];
    if (ch == ' ') return 0;

    /* 'E' at end of last row = Enter */
    if (ch == 'E' && kr == KBD_ROWS - 1 && ki == len - 1)
        return '\n';

    return ch;
}

/*--------------------------------------------------------------------------
 * Draw the remap overlay
 *------------------------------------------------------------------------*/
static void draw_remap_overlay(void)
{
    consoleSelect(&s_console);
    consoleClear();

    /* Title */
    iprintf("\x1b[0;4H\x1b[33m=== BUTTON REMAP ===\x1b[39m");
    iprintf("\x1b[1;0H" "Tap row to select, tap again to");
    iprintf("\x1b[2;0H" "cycle output. Tap +Add for new.");

    /* Column headers */
    iprintf("\x1b[3;0H\x1b[36m" " # Buttons        -> Out" "\x1b[39m");

    /* Remap rows */
    for (int i = 0; i < s_remap_count && i < 14; i++) {
        int row = 4 + i;
        char buf[33];
        memset(buf, 0, sizeof(buf));

        /* Build button name string from mask */
        char names[24];
        memset(names, 0, sizeof(names));
        int first = 1;
        for (int b = 0; b < (int)NUM_BUTTONS; b++) {
            if (s_remap_table[i].ds_mask & s_btn_names[b].mask) {
                if (!first) strncat(names, "+", sizeof(names) - strlen(names) - 1);
                strncat(names, s_btn_names[b].name, sizeof(names) - strlen(names) - 1);
                first = 0;
            }
        }

        if (i == s_remap_sel)
            snprintf(buf, sizeof(buf), ">[%d] %-14s -> %3d",
                     i, names, s_remap_table[i].virtual_output);
        else
            snprintf(buf, sizeof(buf), " [%d] %-14s -> %3d",
                     i, names, s_remap_table[i].virtual_output);

        iprintf("\x1b[%d;0H%s", row, buf);
    }

    /* +Add row */
    if (s_remap_count < DSRD_MAX_REMAPS) {
        iprintf("\x1b[%d;0H\x1b[32m" " [+] Add new remap..." "\x1b[39m",
                4 + s_remap_count);
    }

    /* Editing instructions for selected row */
    if (s_remap_sel >= 0 && s_remap_sel < s_remap_count) {
        int info_row = 4 + s_remap_count + 2;
        if (info_row > 20) info_row = 20;
        iprintf("\x1b[%d;0H\x1b[33m" "Hold DS buttons + tap SET" "\x1b[39m",
                info_row);
        iprintf("\x1b[%d;0H\x1b[33m" "to assign combo to #%d" "\x1b[39m",
                info_row + 1, s_remap_sel);

        /* SET button */
        iprintf("\x1b[%d;1H\x1b[32m" "[ SET COMBO ]" "\x1b[39m",
                info_row + 2);

        /* Check if the user is pressing DS buttons right now */
        scanKeys();
        uint32_t held = keysHeld();
        /* Filter out touch bit */
        held &= ~KEY_TOUCH;
        if (held != 0) {
            /* Show what they're holding */
            char held_str[24];
            memset(held_str, 0, sizeof(held_str));
            int hfirst = 1;
            for (int b = 0; b < (int)NUM_BUTTONS; b++) {
                if (held & s_btn_names[b].mask) {
                    if (!hfirst) strncat(held_str, "+",
                                         sizeof(held_str) - strlen(held_str) - 1);
                    strncat(held_str, s_btn_names[b].name,
                            sizeof(held_str) - strlen(held_str) - 1);
                    hfirst = 0;
                }
            }
            iprintf("\x1b[%d;1H" "Holding: %-20s", info_row + 3, held_str);
        }

        /* If they tap "SET COMBO" zone, capture the held buttons */
        /* (handled in process_touch via a special coordinate check) */
    }

    /* Done button — bottom row */
    iprintf("\x1b[22;8H\x1b[36m" "[ Done / Close ]" "\x1b[39m");
}
