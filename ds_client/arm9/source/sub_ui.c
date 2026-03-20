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
#include "config.h"
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

#define REMAP_BTN_X1     224
#define REMAP_BTN_X2     255
#define REMAP_BTN_Y1     0
#define REMAP_BTN_Y2     15

#define MAG_BTN_X1       80
#define MAG_BTN_X2       176
#define MAG_BTN_Y1       0
#define MAG_BTN_Y2       15

#define MAGMODE_BTN_X1   176
#define MAGMODE_BTN_X2   224
#define MAGMODE_BTN_Y1   0
#define MAGMODE_BTN_Y2   15

/* Compact control-panel toggle (top-left) */
#define PAD_TOGGLE_X1    0
#define PAD_TOGGLE_X2    72
#define PAD_TOGGLE_Y1    0
#define PAD_TOGGLE_Y2    15

/* Left control panel area (inspired layout) */
#define PAD_PANEL_W      104
#define PAD_PANEL_X2     (PAD_PANEL_W - 1)

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

/* Whether the compact control-panel is visible on the left side. */
static int s_show_pad_panel = 1;

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
static uint8_t            s_remap_secondary[DSRD_MAX_REMAPS];
static int                s_remap_count = 0;

/* Which remap row is selected for editing (-1 = none) */
static int s_remap_sel = -1;

/* Modal popup state for combo selection */
static int      s_remap_popup_active = 0;
static int      s_remap_popup_target = -1;
static uint32_t s_remap_popup_mask   = 0;

/* Popup geometry (pixel space) */
#define POP_X1  16
#define POP_X2  239
#define POP_Y1  40
#define POP_Y2  167

#define POP_GRID_X 24
#define POP_GRID_Y 64
#define POP_CELL_W 72
#define POP_CELL_H 20
#define POP_COLS   3
#define POP_ROWS   4

#define POP_APPLY_X1   40
#define POP_APPLY_X2   112
#define POP_CANCEL_X1  144
#define POP_CANCEL_X2  216
#define POP_BTN_Y1     144
#define POP_BTN_Y2     164

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

typedef struct { uint8_t vid; const char *label; } key_opt_t;
static const key_opt_t s_key_opts[] = {
    {100, "A"}, {101, "B"}, {102, "X"}, {103, "Y"},
    {104, "SPC"}, {105, "ENT"}, {106, "ESC"}, {107, "TAB"},
    {108, "SHF"}, {109, "CTL"}, {110, "ALT"}, {111, "BSP"},
};
#define NUM_KEY_OPTS (sizeof(s_key_opts) / sizeof(s_key_opts[0]))

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
static int popcount_u32(uint32_t v);
static int popup_button_index_at(uint16_t px, uint16_t py);
static void popup_open_for_row(int row_idx);
static void popup_close(void);

/*--------------------------------------------------------------------------
 * Default remap table
 *------------------------------------------------------------------------*/
static void load_default_remaps(void)
{
    /* One row per DS hardware button (no combo-trigger rows). */
    s_remap_count = (int)NUM_BUTTONS;
    if (s_remap_count > DSRD_MAX_REMAPS)
        s_remap_count = DSRD_MAX_REMAPS;

    for (int i = 0; i < s_remap_count; i++) {
        s_remap_table[i].ds_mask = s_btn_names[i].mask;
        s_remap_table[i].virtual_output = 0;
        s_remap_secondary[i] = 0;
    }

    /* Practical defaults */
    for (int i = 0; i < s_remap_count; i++) {
        if (s_btn_names[i].mask == KEY_A) s_remap_table[i].virtual_output = 100; /* A */
        if (s_btn_names[i].mask == KEY_B) s_remap_table[i].virtual_output = 101; /* B */
        if (s_btn_names[i].mask == KEY_X) s_remap_table[i].virtual_output = 102; /* X */
        if (s_btn_names[i].mask == KEY_Y) s_remap_table[i].virtual_output = 103; /* Y */
        if (s_btn_names[i].mask == KEY_START)  s_remap_table[i].virtual_output = 105; /* Enter */
        if (s_btn_names[i].mask == KEY_SELECT) s_remap_table[i].virtual_output = 106; /* Esc */
    }
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
    s_show_pad_panel = 1;

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
        /* Modal popup takes precedence over base remap list interactions. */
        if (s_remap_popup_active) {
            if (s_touch_debounce != 0)
                return TOUCH_ZONE_REMAP_UI;

            /* Apply / Cancel */
            if (py >= POP_BTN_Y1 && py <= POP_BTN_Y2) {
                if (px >= POP_APPLY_X1 && px <= POP_APPLY_X2) {
                    /* Apply only when 1 or 2 keys are selected */
                    int n = popcount_u32(s_remap_popup_mask);
                    if (n >= 1 && n <= 2 &&
                        s_remap_popup_target >= 0 &&
                        s_remap_popup_target < s_remap_count) {
                        uint8_t out1 = 0, out2 = 0;
                        for (int i = 0; i < (int)NUM_KEY_OPTS; i++) {
                            if (s_remap_popup_mask & (1u << i)) {
                                if (!out1) out1 = s_key_opts[i].vid;
                                else if (!out2) out2 = s_key_opts[i].vid;
                            }
                        }
                        s_remap_table[s_remap_popup_target].virtual_output = out1;
                        s_remap_secondary[s_remap_popup_target] = out2;
                    }
                    popup_close();
                    draw_remap_overlay();
                    return TOUCH_ZONE_REMAP_UI;
                }
                if (px >= POP_CANCEL_X1 && px <= POP_CANCEL_X2) {
                    popup_close();
                    draw_remap_overlay();
                    return TOUCH_ZONE_REMAP_UI;
                }
            }

            /* Grid buttons */
            int bi = popup_button_index_at(px, py);
            if (bi >= 0) {
                uint32_t bit = (1u << bi);
                if (s_remap_popup_mask & bit) {
                    s_remap_popup_mask &= ~bit;
                } else {
                    if (popcount_u32(s_remap_popup_mask) < 2)
                        s_remap_popup_mask |= bit;
                }
                s_touch_debounce = 8;
                draw_remap_overlay();
                return TOUCH_ZONE_REMAP_UI;
            }

            return TOUCH_ZONE_REMAP_UI;
        }

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

        /* Header rows are 0-2, column header row 3,
           remap rows start at row 4 */
        int remap_idx = row - 4;

        /* Remap entry rows */
        if (remap_idx >= 0 && remap_idx < s_remap_count) {
            if (s_touch_debounce == 0) {
                s_remap_sel = remap_idx;
                popup_open_for_row(remap_idx);
                draw_remap_overlay();
            }
            return TOUCH_ZONE_REMAP_UI;
        }

        if (remap_idx == s_remap_count) {
            return TOUCH_ZONE_REMAP_UI;
        }

        /* Fixed per-button rows; no dynamic add/remove in this mode. */
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

    /* ---- Panel toggle button (top-left) ------------------------------- */
    if (px <= PAD_TOGGLE_X2 && py <= PAD_TOGGLE_Y2) {
        if (s_touch_debounce == 0) {
            s_touch_debounce = 15;
            s_show_pad_panel ^= 1;
            clear_screen();
            draw_trackpad_ui();
            if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN)
                draw_keyboard();
        }
        return TOUCH_ZONE_REMAP_UI;
    }

    /* ---- Magnifier toggle button (top strip) -------------------------- */
    if (px >= MAG_BTN_X1 && px <= MAG_BTN_X2 && py <= MAG_BTN_Y2) {
        if (s_touch_debounce == 0) {
            s_touch_debounce = 15;
            if (!g_cfg.magnifier_enabled) {
                g_cfg.magnifier_enabled = 1;
                if (g_cfg.magnifier_zoom < 2)
                    g_cfg.magnifier_zoom = 2;
            } else if (g_cfg.magnifier_zoom == 2) {
                g_cfg.magnifier_zoom = 3;
            } else {
                g_cfg.magnifier_enabled = 0;
                g_cfg.magnifier_zoom = 2;
            }
            clear_screen();
            draw_trackpad_ui();
            if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN)
                draw_keyboard();
        }
        return TOUCH_ZONE_REMAP_UI;
    }

    /* ---- Magnifier mode toggle (cursor/stylus) ------------------------ */
    if (px >= MAGMODE_BTN_X1 && px <= MAGMODE_BTN_X2 && py <= MAGMODE_BTN_Y2) {
        if (s_touch_debounce == 0) {
            s_touch_debounce = 15;
            g_cfg.magnifier_mode ^= 1;
            clear_screen();
            draw_trackpad_ui();
            if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN)
                draw_keyboard();
        }
        return TOUCH_ZONE_REMAP_UI;
    }

    /* If compact control panel is visible, consume touches on it so
       trackpad movement only occurs in the dedicated trackpad region. */
    if (s_show_pad_panel && px <= PAD_PANEL_X2 &&
        py < (uint16_t)((s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN)
            ? s_kbd_top_px : KB_BTN_Y1)) {
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

uint8_t sub_ui_get_secondary_output(int idx)
{
    if (idx < 0 || idx >= s_remap_count)
        return 0;
    return s_remap_secondary[idx];
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

    char mag_label[12];
    const char *mode_label = (g_cfg.magnifier_mode == 0) ? "[Cur]" : "[Pan]";
    if (g_cfg.magnifier_enabled)
        snprintf(mag_label, sizeof(mag_label), "[Mag:%ux]", g_cfg.magnifier_zoom);
    else
        snprintf(mag_label, sizeof(mag_label), "[Mag:OFF]");

    /* Top action strip */
    iprintf("\x1b[0;0H\x1b[36m%s\x1b[39m",
            s_show_pad_panel ? "[Pad:ON ]" : "[Pad:OFF]");
    iprintf("\x1b[0;10H\x1b[35m%s\x1b[39m", mag_label);
    iprintf("\x1b[0;20H\x1b[32m%s\x1b[39m", mode_label);
    iprintf("\x1b[0;27H\x1b[33mBind\x1b[39m");

    /* Determine current usable trackpad rows (exclude keyboard + bottom bar) */
    int track_bottom_px = KB_BTN_Y1;
    if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN)
        track_bottom_px = s_kbd_top_px;
    int track_rows = track_bottom_px / TILE_H;
    if (track_rows < 6) track_rows = 6;

    /* Optional left compact control panel (visual only) */
    if (s_show_pad_panel) {
        iprintf("\x1b[2;0H\x1b[37m+------------+\x1b[39m");
        iprintf("\x1b[3;0H\x1b[37m| A  B  X    |\x1b[39m");
        iprintf("\x1b[4;0H\x1b[37m| Y  L  R    |\x1b[39m");
        iprintf("\x1b[5;0H\x1b[37m|Sel St  Dp  |\x1b[39m");
        iprintf("\x1b[6;0H\x1b[37m+------------+\x1b[39m");
        iprintf("\x1b[8;0H\x1b[90m(touch ignored)\x1b[39m");
    }

    /* Trackpad panel box */
    int track_col = s_show_pad_panel ? 14 : 1;
    int track_w   = s_show_pad_panel ? 17 : 30;
    int top_row   = 2;
    int bottom_row = track_rows - 2;
    if (bottom_row <= top_row + 2) bottom_row = top_row + 3;

    iprintf("\x1b[%d;%dH\x1b[37m+", top_row, track_col);
    for (int i = 0; i < track_w - 2; i++) iprintf("-");
    iprintf("+");

    for (int r = top_row + 1; r < bottom_row; r++) {
        iprintf("\x1b[%d;%dH\x1b[37m|\x1b[39m", r, track_col);
        iprintf("\x1b[%d;%dH\x1b[37m|\x1b[39m", r, track_col + track_w - 1);
    }

    iprintf("\x1b[%d;%dH\x1b[37m+", bottom_row, track_col);
    for (int i = 0; i < track_w - 2; i++) iprintf("-");
    iprintf("+");

    int mid_row = (top_row + bottom_row) / 2;
    int lbl_col = track_col + (track_w - 11) / 2;
    if (lbl_col < track_col + 1) lbl_col = track_col + 1;
    iprintf("\x1b[%d;%dH\x1b[37mTRACKPAD\x1b[39m", mid_row, lbl_col);

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
            s_shift ^= 1;
            clear_screen();
            draw_trackpad_ui();
            draw_keyboard();
            return 0;
        }
        if (px >= 112 && px < 200) {
            return ' ';
        }
        return 0;
    }

    /* Rows 1-4 = key rows */
    int kr = (row_px / TILE_H) - 1;
    if (kr < 0 || kr >= KBD_ROWS) return 0;

    const char **layout = s_shift ? s_kbd_layout_shift : s_kbd_layout;
    const char *keys = layout[kr];
    int len = (int)strlen(keys);

    if (kr == 0 && px >= 224) return 8;

    int ki = ((int)px - 8) / 16;
    if (ki < 0 || ki >= len) return 0;

    char ch = keys[ki];
    if (ch == ' ') return 0;
    if (ch == 'E' && kr == KBD_ROWS - 1 && ki == len - 1)
        return '\n';

    return ch;
}

/*----------
 * Draw the remap overlay
 *------------------------------------------------------------------------*/
static void draw_remap_overlay(void)
{
    consoleSelect(&s_console);
    consoleClear();

    iprintf("\x1b[0;4H\x1b[33m=== BUTTON REMAP ===\x1b[39m");
    iprintf("\x1b[1;0HTap a DS button row to bind keys");
    iprintf("\x1b[2;0HEach DS button supports up to 2 keys");
    iprintf("\x1b[3;0H\x1b[36m DS Button    -> Keys\x1b[39m");

    for (int i = 0; i < s_remap_count && i < 14; i++) {
        int row = 4 + i;
        char buf[33];
        memset(buf, 0, sizeof(buf));

        const char *btn_label = "?";
        for (int b = 0; b < (int)NUM_BUTTONS; b++) {
            if (s_btn_names[b].mask == s_remap_table[i].ds_mask) {
                btn_label = s_btn_names[b].name;
                break;
            }
        }

        const char *k1 = "-";
        const char *k2 = "";
        for (int k = 0; k < (int)NUM_KEY_OPTS; k++) {
            if (s_key_opts[k].vid == s_remap_table[i].virtual_output)
                k1 = s_key_opts[k].label;
            if (s_key_opts[k].vid == s_remap_secondary[i])
                k2 = s_key_opts[k].label;
        }

        char keys[12];
        if (s_remap_secondary[i] != 0)
            snprintf(keys, sizeof(keys), "%s+%s", k1, k2);
        else
            snprintf(keys, sizeof(keys), "%s", k1);

        if (i == s_remap_sel)
            snprintf(buf, sizeof(buf), "> %-10s -> %-8s", btn_label, keys);
        else
            snprintf(buf, sizeof(buf), "  %-10s -> %-8s", btn_label, keys);

        iprintf("\x1b[%d;0H%s", row, buf);
    }

    iprintf("\x1b[22;8H\x1b[36m[ Done / Close ]\x1b[39m");

    if (s_remap_popup_active) {
        int r1 = POP_Y1 / TILE_H;
        int r2 = POP_Y2 / TILE_H;
        int c1 = POP_X1 / 8;
        int c2 = POP_X2 / 8;

        for (int c = c1; c <= c2; c++) {
            iprintf("\x1b[%d;%dH\x1b[37m=\x1b[39m", r1, c);
            iprintf("\x1b[%d;%dH\x1b[37m=\x1b[39m", r2, c);
        }
        for (int r = r1; r <= r2; r++) {
            iprintf("\x1b[%d;%dH\x1b[37m|\x1b[39m", r, c1);
            iprintf("\x1b[%d;%dH\x1b[37m|\x1b[39m", r, c2);
        }

        iprintf("\x1b[%d;%dH\x1b[33mSelect 1 or 2 keyboard keys\x1b[39m", r1 + 1, c1 + 2);
        iprintf("\x1b[%d;%dH\x1b[37mTap to toggle, then Apply\x1b[39m", r1 + 2, c1 + 2);

        for (int i = 0; i < (int)NUM_KEY_OPTS; i++) {
            int prow = i / POP_COLS;
            int pcol = i % POP_COLS;
            int rr = (POP_GRID_Y / TILE_H) + (prow * 3);
            int cc = (POP_GRID_X / 8) + (pcol * 10);
            int on = (s_remap_popup_mask & (1u << i)) ? 1 : 0;
            iprintf("\x1b[%d;%dH%s%-6s", rr, cc,
                    on ? "\x1b[32m[*] " : "\x1b[37m[ ] ",
                    s_key_opts[i].label);
            iprintf("\x1b[39m");
        }

        int nsel = popcount_u32(s_remap_popup_mask);
        iprintf("\x1b[%d;%dH\x1b[36mSelected: %d/2\x1b[39m",
                (POP_BTN_Y1 / TILE_H) - 1, c1 + 2, nsel);
        iprintf("\x1b[%d;%dH\x1b[32m[  APPLY  ]\x1b[39m",
                POP_BTN_Y1 / TILE_H, POP_APPLY_X1 / 8);
        iprintf("\x1b[%d;%dH\x1b[31m[ CANCEL ]\x1b[39m",
                POP_BTN_Y1 / TILE_H, POP_CANCEL_X1 / 8);
    }
}

static int popcount_u32(uint32_t v)
{
    int n = 0;
    while (v) {
        n += (v & 1u) ? 1 : 0;
        v >>= 1;
    }
    return n;
}

static int popup_button_index_at(uint16_t px, uint16_t py)
{
    if (px < POP_GRID_X || py < POP_GRID_Y)
        return -1;

    int rel_x = (int)px - POP_GRID_X;
    int rel_y = (int)py - POP_GRID_Y;
    int stride_x = POP_CELL_W + 4;
    int stride_y = POP_CELL_H + 4;

    int col = rel_x / stride_x;
    int row = rel_y / stride_y;
    if (col < 0 || col >= POP_COLS || row < 0 || row >= POP_ROWS)
        return -1;

    int in_cell_x = rel_x % stride_x;
    int in_cell_y = rel_y % stride_y;
    if (in_cell_x >= POP_CELL_W || in_cell_y >= POP_CELL_H)
        return -1;

    int idx = row * POP_COLS + col;
    if (idx >= (int)NUM_KEY_OPTS)
        return -1;
    return idx;
}

static void popup_open_for_row(int row_idx)
{
    if (row_idx < 0 || row_idx >= s_remap_count)
        return;

    s_remap_popup_active = 1;
    s_remap_popup_target = row_idx;
    s_remap_popup_mask   = 0;

    uint8_t v1 = s_remap_table[row_idx].virtual_output;
    uint8_t v2 = s_remap_secondary[row_idx];
    for (int i = 0; i < (int)NUM_KEY_OPTS; i++) {
        if (s_key_opts[i].vid == v1 || s_key_opts[i].vid == v2)
            s_remap_popup_mask |= (1u << i);
    }

    s_touch_debounce = 10;
}

static void popup_close(void)
{
    s_remap_popup_active = 0;
    s_remap_popup_target = -1;
    s_remap_popup_mask   = 0;
    s_touch_debounce = 8;
}
