/*============================================================================
 * ds_client/arm9/source/sub_ui.c
 *
 * Bottom-screen master UI controller.
 *
 * The sub screen is a bitmap-rendered touch UI with slide-out overlays:
 *
 *   - DEFAULT state : trackpad surface with a left-side control cluster,
 *     a top-right remap button, and a keyboard toggle pill.
 *
 *   - KEYBOARD state: on-screen keyboard slides up from the bottom while
 *     the keyboard toggle pill floats just above it.
 *
 *   - REMAP state: full-screen overlay listing each DS hardware button and
 *     its current virtual-output mapping. The popup key chooser is paged so
 *     a larger keyboard key set can be browsed.
 *
 * Rendering uses a 16-bit bitmap sub background with a RAM backbuffer so
 * animations and press feedback can be composed and presented in one copy.
 *
 * No dynamic memory allocation — all buffers are static.
 *==========================================================================*/
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "sub_ui.h"
#include "config.h"
#include "../../../common/protocol.h"

#define SUB_W 256
#define SUB_H 192
#define SLIDE_SPEED 8
#define KBD_HEIGHT 96
#define KBD_MIN_TRACK_PX 58

#define KB_BTN_X1 96
#define KB_BTN_X2 160
#define KB_BTN_Y1 176
#define KB_BTN_Y2 191
#define KB_BTN_GAP 4

#define PAD_TOGGLE_X1 2
#define PAD_TOGGLE_X2 42
#define PAD_TOGGLE_Y1 2
#define PAD_TOGGLE_Y2 18
#define MAG_BTN_X1 164
#define MAG_BTN_X2 206
#define MAG_BTN_Y1 2
#define MAG_BTN_Y2 18
#define MODE_BTN_X1 208
#define MODE_BTN_X2 232
#define MODE_BTN_Y1 2
#define MODE_BTN_Y2 18
#define REMAP_BTN_X1 234
#define REMAP_BTN_X2 254
#define REMAP_BTN_Y1 2
#define REMAP_BTN_Y2 18

#define POP_X1 16
#define POP_X2 239
#define POP_Y1 28
#define POP_Y2 170
#define POP_GRID_X 24
#define POP_GRID_Y 56
#define POP_CELL_W 64
#define POP_CELL_H 20
#define POP_COLS 3
#define POP_ROWS 4
#define POP_KEYS_PER_PAGE (POP_COLS * POP_ROWS)
#define POP_PAGE_PREV_X1 24
#define POP_PAGE_PREV_X2 74
#define POP_PAGE_NEXT_X1 182
#define POP_PAGE_NEXT_X2 232
#define POP_PAGE_BTN_Y1 34
#define POP_PAGE_BTN_Y2 48
#define POP_APPLY_X1 44
#define POP_APPLY_X2 108
#define POP_CANCEL_X1 148
#define POP_CANCEL_X2 212
#define POP_BTN_Y1 144
#define POP_BTN_Y2 164

typedef enum {
    UI_STATE_TRACKPAD = 0,
    UI_STATE_KBD_SLIDING_IN,
    UI_STATE_KBD_OPEN,
    UI_STATE_KBD_SLIDING_OUT,
    UI_STATE_REMAP,
} ui_state_t;

typedef struct { uint32_t mask; const char *name; } btn_name_t;
typedef struct { uint8_t vid; const char *label; } key_opt_t;
typedef struct {
    int x, y, w, h;
    const char *label;
    char normal;
    char shifted;
    uint8_t action;
} ui_key_t;

enum {
    UIKEY_CHAR = 0,
    UIKEY_SHIFT,
    UIKEY_SPACE,
    UIKEY_ENTER,
    UIKEY_BACKSPACE,
};

static ui_state_t s_state = UI_STATE_TRACKPAD;
static int s_kbd_top_px = SUB_H;
static int s_show_pad_panel = 1;
static int s_shift = 0;
static volatile uint8_t s_last_key = 0;
static int s_touch_debounce = 0;
static int s_ui_inited = 0;
static uint16_t *s_fb = NULL;
static uint16_t s_backbuf[SUB_W * SUB_H];
static int s_pressed_active = 0;
static int s_pressed_x = 0;
static int s_pressed_y = 0;
static int s_pressed_w = 0;
static int s_pressed_h = 0;

static dsrd_remap_entry_t s_remap_table[DSRD_MAX_REMAPS];
static uint8_t s_remap_secondary[DSRD_MAX_REMAPS];
static int s_remap_count = 0;
static int s_remap_sel = -1;
static int s_remap_popup_active = 0;
static int s_remap_popup_target = -1;
static uint32_t s_remap_popup_mask = 0;
static int s_key_opt_page = 0;

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
#define NUM_BUTTONS ((int)(sizeof(s_btn_names) / sizeof(s_btn_names[0])))

static const key_opt_t s_key_opts[] = {
    {100, "A"}, {101, "B"}, {112, "C"}, {113, "D"},
    {114, "E"}, {115, "F"}, {116, "G"}, {117, "H"},
    {118, "I"}, {119, "J"}, {120, "K"}, {121, "L"},
    {122, "M"}, {123, "N"}, {124, "O"}, {125, "P"},
    {126, "Q"}, {127, "R"}, {128, "S"}, {129, "T"},
    {130, "U"}, {131, "V"}, {132, "W"}, {102, "X"},
    {103, "Y"}, {133, "Z"},
    {134, "0"}, {135, "1"}, {136, "2"}, {137, "3"},
    {138, "4"}, {139, "5"}, {140, "6"}, {141, "7"},
    {142, "8"}, {143, "9"},
    {104, "SPC"}, {105, "ENT"}, {106, "ESC"}, {107, "TAB"},
    {108, "SHF"}, {109, "CTL"}, {110, "ALT"}, {111, "BSP"},
    {144, "UP"}, {145, "DN"}, {146, "LT"}, {147, "RT"},
};
#define NUM_KEY_OPTS ((int)(sizeof(s_key_opts) / sizeof(s_key_opts[0])))

static const ui_key_t s_row0[] = {
    {  6, 0, 16, 14, "1", '1', '!', UIKEY_CHAR }, { 24, 0, 16, 14, "2", '2', '@', UIKEY_CHAR },
    { 42, 0, 16, 14, "3", '3', '#', UIKEY_CHAR }, { 60, 0, 16, 14, "4", '4', '$', UIKEY_CHAR },
    { 78, 0, 16, 14, "5", '5', '%', UIKEY_CHAR }, { 96, 0, 16, 14, "6", '6', '^', UIKEY_CHAR },
    {114, 0, 16, 14, "7", '7', '&', UIKEY_CHAR }, {132, 0, 16, 14, "8", '8', '*', UIKEY_CHAR },
    {150, 0, 16, 14, "9", '9', '(', UIKEY_CHAR }, {168, 0, 16, 14, "0", '0', ')', UIKEY_CHAR },
    {186, 0, 16, 14, "-", '-', '_', UIKEY_CHAR }, {204, 0, 16, 14, "=", '=', '+', UIKEY_CHAR },
    {222, 0, 26, 14, "BK", 8, 8, UIKEY_BACKSPACE },
};
static const ui_key_t s_row1[] = {
    { 12, 0, 16, 14, "Q", 'q', 'Q', UIKEY_CHAR }, { 30, 0, 16, 14, "W", 'w', 'W', UIKEY_CHAR },
    { 48, 0, 16, 14, "E", 'e', 'E', UIKEY_CHAR }, { 66, 0, 16, 14, "R", 'r', 'R', UIKEY_CHAR },
    { 84, 0, 16, 14, "T", 't', 'T', UIKEY_CHAR }, {102, 0, 16, 14, "Y", 'y', 'Y', UIKEY_CHAR },
    {120, 0, 16, 14, "U", 'u', 'U', UIKEY_CHAR }, {138, 0, 16, 14, "I", 'i', 'I', UIKEY_CHAR },
    {156, 0, 16, 14, "O", 'o', 'O', UIKEY_CHAR }, {174, 0, 16, 14, "P", 'p', 'P', UIKEY_CHAR },
    {192, 0, 16, 14, "[", '[', '{', UIKEY_CHAR }, {210, 0, 16, 14, "]", ']', '}', UIKEY_CHAR },
};
static const ui_key_t s_row2[] = {
    { 18, 0, 16, 14, "A", 'a', 'A', UIKEY_CHAR }, { 36, 0, 16, 14, "S", 's', 'S', UIKEY_CHAR },
    { 54, 0, 16, 14, "D", 'd', 'D', UIKEY_CHAR }, { 72, 0, 16, 14, "F", 'f', 'F', UIKEY_CHAR },
    { 90, 0, 16, 14, "G", 'g', 'G', UIKEY_CHAR }, {108, 0, 16, 14, "H", 'h', 'H', UIKEY_CHAR },
    {126, 0, 16, 14, "J", 'j', 'J', UIKEY_CHAR }, {144, 0, 16, 14, "K", 'k', 'K', UIKEY_CHAR },
    {162, 0, 16, 14, "L", 'l', 'L', UIKEY_CHAR }, {180, 0, 16, 14, ";", ';', ':', UIKEY_CHAR },
    {198, 0, 16, 14, "'", '\'', '"', UIKEY_CHAR }, {216, 0, 32, 14, "ENT", '\n', '\n', UIKEY_ENTER },
};
static const ui_key_t s_row3[] = {
    {  6, 0, 28, 14, "SHF", 0, 0, UIKEY_SHIFT }, { 38, 0, 16, 14, "Z", 'z', 'Z', UIKEY_CHAR },
    { 56, 0, 16, 14, "X", 'x', 'X', UIKEY_CHAR }, { 74, 0, 16, 14, "C", 'c', 'C', UIKEY_CHAR },
    { 92, 0, 16, 14, "V", 'v', 'V', UIKEY_CHAR }, {110, 0, 16, 14, "B", 'b', 'B', UIKEY_CHAR },
    {128, 0, 16, 14, "N", 'n', 'N', UIKEY_CHAR }, {146, 0, 16, 14, "M", 'm', 'M', UIKEY_CHAR },
    {164, 0, 16, 14, ",", ',', '<', UIKEY_CHAR }, {182, 0, 16, 14, ".", '.', '>', UIKEY_CHAR },
    {200, 0, 16, 14, "/" , '/' , '?', UIKEY_CHAR },
};
static const ui_key_t s_row4[] = {
    { 50, 0, 156, 16, "SPACE", ' ', ' ', UIKEY_SPACE },
};

static uint16_t rgb15(int r, int g, int b) { return RGB15(r, g, b) | BIT(15); }
#define COL_BG         rgb15(29,30,31)
#define COL_PANEL      rgb15(30,30,31)
#define COL_PANEL2     rgb15(27,28,30)
#define COL_BORDER     rgb15(18,20,22)
#define COL_WHITE      rgb15(31,31,31)
#define COL_TEXT       rgb15(10,12,14)
#define COL_SUBTEXT    rgb15(14,16,18)
#define COL_ACCENT     rgb15(17,25,30)
#define COL_ACCENT2    rgb15(8,18,24)
#define COL_KEY        rgb15(28,30,31)
#define COL_SELECTED   rgb15(20,28,31)
#define COL_PRESSED    rgb15(21,23,24)
#define COL_PILL_PRESS rgb15(14,18,22)

static const uint8_t *glyph_for(char c);
static void put_px(int x, int y, uint16_t c);
static void fill_rect(int x, int y, int w, int h, uint16_t c);
static void fill_round_rect(int x, int y, int w, int h, int r, uint16_t c);
static void draw_round_rect(int x, int y, int w, int h, int r, uint16_t c);
static void fill_circle(int cx, int cy, int r, uint16_t c);
static void draw_soft_shadow(int x, int y, int w, int h, int r);
static int text_width(const char *s, int scale);
static void draw_glyph(int x, int y, char ch, uint16_t c, int scale);
static void draw_text(int x, int y, const char *s, uint16_t c, int scale);
static void draw_center_text(int x, int y, int w, int h, const char *s, uint16_t c, int scale);
static int rect_is_pressed(int x, int y, int w, int h);
static int set_pressed_rect(int x, int y, int w, int h);
static int clear_pressed_rect(void);
static void draw_button(int x, int y, int w, int h, const char *label, int selected);
static void draw_pill(int x, int y, int w, int h, const char *label, uint16_t fill, uint16_t text);
static void draw_icon_circle(int cx, int cy, int r, const char *label);
static void draw_icon_dpad(int cx, int cy);
static const char *button_name_for_mask(uint32_t mask);
static const char *key_label_for_vid(uint8_t vid);
static void load_default_remaps(void);
static void clear_screen(void);
static void present_ui(void);
static int kb_toggle_y1(void);
static int kb_toggle_y2(void);
static void draw_control_card(int x, int y, int w, int h, const char *icon_label, const char *caption, int icon_style);
static void draw_control_cluster(void);
static void draw_trackpad_ui(void);
static void draw_keyboard_row(const ui_key_t *row, int count, int base_y);
static void draw_keyboard(void);
static void draw_remap_overlay(void);
static void redraw_current_ui(void);
static char key_from_button(const ui_key_t *k);
static char hit_row(const ui_key_t *row, int count, int base_y, uint16_t px, uint16_t py);
static char kbd_hit_test(uint16_t px, uint16_t py);
static int popcount_u32(uint32_t v);
static int popup_button_index_at(uint16_t px, uint16_t py);
static void popup_open_for_row(int row_idx);
static void popup_close(void);

static const uint8_t *glyph_for(char c)
{
    static const uint8_t sp[5]={0,0,0,0,0}, A[5]={0x1E,0x05,0x05,0x1E,0x00}, B[5]={0x1F,0x15,0x15,0x0A,0x00},
    C[5]={0x0E,0x11,0x11,0x0A,0x00}, D[5]={0x1F,0x11,0x11,0x0E,0x00}, E[5]={0x1F,0x15,0x15,0x11,0x00},
    F[5]={0x1F,0x05,0x05,0x01,0x00}, G[5]={0x0E,0x11,0x15,0x1D,0x00}, H[5]={0x1F,0x04,0x04,0x1F,0x00},
    I[5]={0x11,0x1F,0x11,0x00,0x00}, J[5]={0x08,0x10,0x10,0x0F,0x00}, K[5]={0x1F,0x04,0x0A,0x11,0x00},
    L[5]={0x1F,0x10,0x10,0x10,0x00}, M[5]={0x1F,0x02,0x04,0x02,0x1F}, N[5]={0x1F,0x02,0x04,0x1F,0x00},
    O[5]={0x0E,0x11,0x11,0x0E,0x00}, P[5]={0x1F,0x05,0x05,0x02,0x00}, Q[5]={0x0E,0x11,0x19,0x1E,0x00},
    R[5]={0x1F,0x05,0x0D,0x12,0x00}, S[5]={0x12,0x15,0x15,0x09,0x00}, T[5]={0x01,0x1F,0x01,0x00,0x00},
    U[5]={0x0F,0x10,0x10,0x0F,0x00}, V[5]={0x07,0x08,0x10,0x08,0x07}, W[5]={0x1F,0x08,0x04,0x08,0x1F},
    X[5]={0x1B,0x04,0x04,0x1B,0x00}, Y[5]={0x03,0x04,0x18,0x04,0x03}, Z[5]={0x19,0x15,0x13,0x00,0x00},
    n0[5]={0x0E,0x19,0x15,0x13,0x0E}, n1[5]={0x12,0x1F,0x10,0x00,0x00}, n2[5]={0x19,0x15,0x15,0x12,0x00},
    n3[5]={0x11,0x15,0x15,0x0A,0x00}, n4[5]={0x07,0x04,0x04,0x1F,0x00}, n5[5]={0x17,0x15,0x15,0x09,0x00},
    n6[5]={0x0E,0x15,0x15,0x08,0x00}, n7[5]={0x01,0x01,0x1D,0x03,0x00}, n8[5]={0x0A,0x15,0x15,0x0A,0x00},
    n9[5]={0x02,0x15,0x15,0x0E,0x00}, dash[5]={0x04,0x04,0x04,0x04,0x00}, eq[5]={0x0A,0x0A,0x0A,0x0A,0x00},
    lbr[5]={0x1F,0x11,0x11,0x00,0x00}, rbr[5]={0x11,0x11,0x1F,0x00,0x00}, semi[5]={0x10,0x0A,0x00,0x00,0x00},
    apos[5]={0x03,0x00,0x00,0x00,0x00}, comma[5]={0x10,0x08,0x00,0x00,0x00}, dot[5]={0x10,0x00,0x00,0x00,0x00},
    slash[5]={0x18,0x04,0x03,0x00,0x00}, plus[5]={0x04,0x0E,0x04,0x00,0x00}, colon[5]={0x0A,0x00,0x00,0x00,0x00},
    gt[5]={0x00,0x11,0x0A,0x04,0x00};
    if (c >= 'a' && c <= 'z') c -= 32;
    switch (c) {
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D; case 'E': return E; case 'F': return F;
    case 'G': return G; case 'H': return H; case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
    case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P; case 'Q': return Q; case 'R': return R;
    case 'S': return S; case 'T': return T; case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
    case 'Y': return Y; case 'Z': return Z; case '0': return n0; case '1': return n1; case '2': return n2; case '3': return n3;
    case '4': return n4; case '5': return n5; case '6': return n6; case '7': return n7; case '8': return n8; case '9': return n9;
    case '-': return dash; case '=': return eq; case '[': return lbr; case ']': return rbr; case ';': return semi; case '\'': return apos;
    case ',': return comma; case '.': return dot; case '/': return slash; case '+': return plus; case ':': return colon; case '>': return gt;
    default: return sp;
    }
}

static void put_px(int x, int y, uint16_t c)
{
    if (!s_fb) return;
    if ((unsigned)x >= SUB_W || (unsigned)y >= SUB_H) return;
    s_backbuf[y * SUB_W + x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint16_t c)
{
    int xx, yy;
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            put_px(xx, yy, c);
}

static void fill_round_rect(int x, int y, int w, int h, int r, uint16_t c)
{
    int xx, yy, dx, dy;
    fill_rect(x + r, y, w - 2 * r, h, c);
    fill_rect(x, y + r, r, h - 2 * r, c);
    fill_rect(x + w - r, y + r, r, h - 2 * r, c);
    for (yy = 0; yy < r; yy++) {
        for (xx = 0; xx < r; xx++) {
            dx = r - 1 - xx;
            dy = r - 1 - yy;
            if (dx * dx + dy * dy <= r * r) {
                put_px(x + xx, y + yy, c);
                put_px(x + w - 1 - xx, y + yy, c);
                put_px(x + xx, y + h - 1 - yy, c);
                put_px(x + w - 1 - xx, y + h - 1 - yy, c);
            }
        }
    }
}

static void draw_round_rect(int x, int y, int w, int h, int r, uint16_t c)
{
    int i, xx, yy, dx, dy;
    int inner = (r > 1) ? (r - 1) : 0;
    int outer2 = r * r;
    int inner2 = inner * inner;

    for (i = x + r; i < x + w - r; i++) {
        put_px(i, y, c);
        put_px(i, y + h - 1, c);
    }
    for (i = y + r; i < y + h - r; i++) {
        put_px(x, i, c);
        put_px(x + w - 1, i, c);
    }

    for (yy = 0; yy < r; yy++) {
        for (xx = 0; xx < r; xx++) {
            dx = r - 1 - xx;
            dy = r - 1 - yy;
            if (dx * dx + dy * dy <= outer2 &&
                (inner == 0 || dx * dx + dy * dy >= inner2)) {
                put_px(x + xx, y + yy, c);
                put_px(x + w - 1 - xx, y + yy, c);
                put_px(x + xx, y + h - 1 - yy, c);
                put_px(x + w - 1 - xx, y + h - 1 - yy, c);
            }
        }
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                put_px(cx + x, cy + y, c);
}

static void draw_soft_shadow(int x, int y, int w, int h, int r)
{
    fill_round_rect(x + 2, y + 2, w, h, r, rgb15(18, 20, 22));
}

static int text_width(const char *s, int scale)
{
    int n = 0;
    while (*s) { n += 6 * scale; s++; }
    return n ? n - scale : 0;
}

static void draw_glyph(int x, int y, char ch, uint16_t c, int scale)
{
    const uint8_t *g = glyph_for(ch);
    int col, row, sx, sy;
    for (col = 0; col < 5; col++)
        for (row = 0; row < 7; row++)
            if (g[col] & (1u << row))
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++)
                        put_px(x + col * scale + sx, y + row * scale + sy, c);
}

static void draw_text(int x, int y, const char *s, uint16_t c, int scale)
{
    while (*s) {
        draw_glyph(x, y, *s, c, scale);
        x += 6 * scale;
        s++;
    }
}

static void draw_center_text(int x, int y, int w, int h, const char *s, uint16_t c, int scale)
{
    int tw = text_width(s, scale);
    int th = 7 * scale;
    draw_text(x + (w - tw) / 2, y + (h - th) / 2, s, c, scale);
}

static int rect_is_pressed(int x, int y, int w, int h)
{
    return s_pressed_active && s_pressed_x == x && s_pressed_y == y && s_pressed_w == w && s_pressed_h == h;
}

static int set_pressed_rect(int x, int y, int w, int h)
{
    if (rect_is_pressed(x, y, w, h)) return 0;
    s_pressed_active = 1;
    s_pressed_x = x;
    s_pressed_y = y;
    s_pressed_w = w;
    s_pressed_h = h;
    return 1;
}

static int clear_pressed_rect(void)
{
    if (!s_pressed_active) return 0;
    s_pressed_active = 0;
    return 1;
}

static void draw_button(int x, int y, int w, int h, const char *label, int selected)
{
    draw_soft_shadow(x, y, w, h, 6);
    fill_round_rect(x, y, w, h, 6, selected ? COL_SELECTED : (rect_is_pressed(x, y, w, h) ? COL_PRESSED : COL_KEY));
    draw_round_rect(x, y, w, h, 6, COL_BORDER);
    draw_center_text(x, y, w, h, label, COL_TEXT, (h >= 16) ? 2 : 1);
}

static void draw_pill(int x, int y, int w, int h, const char *label, uint16_t fill, uint16_t text)
{
    uint16_t actual = rect_is_pressed(x, y, w, h) ? ((fill == COL_ACCENT) ? COL_ACCENT2 : COL_PILL_PRESS) : fill;
    fill_round_rect(x, y, w, h, h / 2, actual);
    draw_round_rect(x, y, w, h, h / 2, COL_BORDER);
    draw_center_text(x, y, w, h, label, text, 1);
}

static void draw_icon_circle(int cx, int cy, int r, const char *label)
{
    fill_circle(cx, cy, r, rgb15(16, 18, 20));
    fill_circle(cx, cy - 1, r - 2, rgb15(21, 23, 25));
    draw_center_text(cx - r, cy - r, r * 2, r * 2, label, COL_WHITE, 2);
}

static void draw_icon_dpad(int cx, int cy)
{
    fill_round_rect(cx - 6, cy - 16, 12, 32, 3, rgb15(17, 19, 21));
    fill_round_rect(cx - 16, cy - 6, 32, 12, 3, rgb15(17, 19, 21));
    fill_round_rect(cx - 5, cy - 15, 10, 14, 2, rgb15(21, 23, 25));
    fill_round_rect(cx - 15, cy - 5, 14, 10, 2, rgb15(21, 23, 25));
    fill_round_rect(cx + 1, cy - 5, 14, 10, 2, rgb15(21, 23, 25));
    fill_round_rect(cx - 5, cy + 1, 10, 14, 2, rgb15(21, 23, 25));
    draw_round_rect(cx - 16, cy - 16, 32, 32, 4, COL_BORDER);
}

static const char *button_name_for_mask(uint32_t mask)
{
    int i;
    for (i = 0; i < NUM_BUTTONS; i++) if (s_btn_names[i].mask == mask) return s_btn_names[i].name;
    return "?";
}

static const char *key_label_for_vid(uint8_t vid)
{
    int i;
    for (i = 0; i < NUM_KEY_OPTS; i++) if (s_key_opts[i].vid == vid) return s_key_opts[i].label;
    return "-";
}

static void load_default_remaps(void)
{
    int i;
    s_remap_count = NUM_BUTTONS;
    if (s_remap_count > DSRD_MAX_REMAPS) s_remap_count = DSRD_MAX_REMAPS;
    for (i = 0; i < s_remap_count; i++) {
        s_remap_table[i].ds_mask = s_btn_names[i].mask;
        s_remap_table[i].virtual_output = 0;
        s_remap_secondary[i] = 0;
    }
    for (i = 0; i < s_remap_count; i++) {
        if (s_btn_names[i].mask == KEY_A) s_remap_table[i].virtual_output = 100;
        if (s_btn_names[i].mask == KEY_B) s_remap_table[i].virtual_output = 101;
        if (s_btn_names[i].mask == KEY_X) s_remap_table[i].virtual_output = 102;
        if (s_btn_names[i].mask == KEY_Y) s_remap_table[i].virtual_output = 103;
        if (s_btn_names[i].mask == KEY_START) s_remap_table[i].virtual_output = 105;
        if (s_btn_names[i].mask == KEY_SELECT) s_remap_table[i].virtual_output = 106;
    }
}

static void clear_screen(void)
{
    fill_rect(0, 0, SUB_W, SUB_H, COL_BG);
}

static int kb_toggle_y1(void)
{
    int btn_h = KB_BTN_Y2 - KB_BTN_Y1 + 1;
    if (s_state == UI_STATE_KBD_OPEN ||
        s_state == UI_STATE_KBD_SLIDING_IN ||
        s_state == UI_STATE_KBD_SLIDING_OUT)
        return s_kbd_top_px - btn_h - KB_BTN_GAP;
    return KB_BTN_Y1;
}

static int kb_toggle_y2(void)
{
    return kb_toggle_y1() + (KB_BTN_Y2 - KB_BTN_Y1);
}

static void present_ui(void)
{
    if (!s_fb) return;
    dmaCopyHalfWords(0, s_backbuf, s_fb, sizeof(s_backbuf));
}

static void draw_control_card(int x, int y, int w, int h, const char *icon_label, const char *caption, int icon_style)
{
    draw_soft_shadow(x, y, w, h, 7);
    fill_round_rect(x, y, w, h, 7, COL_KEY);
    draw_round_rect(x, y, w, h, 7, COL_BORDER);
    if (icon_style == 0) draw_icon_circle(x + w / 2, y + 12, 8, icon_label ? icon_label : " ");
    else if (icon_style == 1) { if (icon_label) draw_center_text(x, y + 2, w, 18, icon_label, COL_SUBTEXT, 2); }
    else draw_icon_dpad(x + w / 2, y + 12);
    if (caption) draw_center_text(x, y + h - 12, w, 10, caption, COL_SUBTEXT, 1);
}

static void draw_control_cluster(void)
{
    draw_control_card(8, 26, 34, 28, "A", NULL, 0);
    draw_control_card(46, 26, 34, 28, "B", NULL, 0);
    draw_control_card(84, 26, 34, 28, "X", NULL, 0);
    draw_control_card(8, 58, 34, 28, "Y", NULL, 0);
    draw_control_card(46, 58, 34, 28, "L", NULL, 1);
    draw_control_card(84, 58, 34, 28, "R", NULL, 1);
    draw_control_card(8, 90, 34, 28, "-", "SELECT", 1);
    draw_control_card(46, 90, 34, 28, ">", "START", 1);
    draw_control_card(84, 90, 34, 28, NULL, NULL, 2);
}

static void draw_trackpad_ui(void)
{
    int track_x, track_y = 24, track_h, track_w;
    int kb_btn_y1 = kb_toggle_y1();
    int kb_btn_y2 = kb_toggle_y2();
    char mag[12];
    clear_screen();
    draw_pill(PAD_TOGGLE_X1, PAD_TOGGLE_Y1, 40, 16, s_show_pad_panel ? "PAD" : ">>", COL_PANEL2, COL_WHITE);
    if (g_cfg.magnifier_enabled) snprintf(mag, sizeof(mag), "M%ux", g_cfg.magnifier_zoom); else snprintf(mag, sizeof(mag), "MAG");
    draw_pill(MAG_BTN_X1, MAG_BTN_Y1, 42, 16, mag, COL_ACCENT, COL_WHITE);
    draw_pill(MODE_BTN_X1, MODE_BTN_Y1, 24, 16, g_cfg.magnifier_mode ? "PAN" : "CUR", COL_PANEL2, COL_WHITE);
    draw_pill(REMAP_BTN_X1, REMAP_BTN_Y1, 20, 16, "MAP", COL_PANEL2, COL_WHITE);
    if (s_show_pad_panel) {
        draw_soft_shadow(4, 20, 120, 102, 10);
        fill_round_rect(4, 20, 120, 102, 10, COL_PANEL);
        draw_round_rect(4, 20, 120, 102, 10, COL_BORDER);
        draw_control_cluster();
        track_x = 132;
        track_w = 120;
    } else { track_x = 8; track_w = 240; }
    if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN || s_state == UI_STATE_KBD_SLIDING_OUT)
        track_h = (kb_btn_y1 - KB_BTN_GAP) - track_y;
    else
        track_h = 172 - track_y;
    if (track_h < KBD_MIN_TRACK_PX) track_h = KBD_MIN_TRACK_PX;
    draw_soft_shadow(track_x, track_y, track_w, track_h, 12);
    fill_round_rect(track_x, track_y, track_w, track_h, 12, COL_PANEL);
    draw_round_rect(track_x, track_y, track_w, track_h, 12, COL_BORDER);
    draw_center_text(track_x, track_y, track_w, track_h, "TRACKPAD", COL_ACCENT2, 2);
    draw_pill(KB_BTN_X1, kb_btn_y1, KB_BTN_X2 - KB_BTN_X1, kb_btn_y2 - kb_btn_y1 + 1,
              (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN || s_state == UI_STATE_KBD_SLIDING_OUT) ? "HIDE KB" : "SHOW KB", COL_ACCENT, COL_WHITE);
}

static void draw_keyboard_row(const ui_key_t *row, int count, int base_y)
{
    int i;
    for (i = 0; i < count; i++) draw_button(row[i].x, base_y + row[i].y, row[i].w, row[i].h, row[i].label, row[i].action == UIKEY_SHIFT && s_shift);
}

static void draw_keyboard(void)
{
    int start_y = s_kbd_top_px;
    fill_round_rect(0, start_y, SUB_W, SUB_H - start_y, 10, COL_PANEL2);
    draw_round_rect(0, start_y, SUB_W, SUB_H - start_y, 10, COL_BORDER);
    draw_keyboard_row(s_row0, (int)(sizeof(s_row0) / sizeof(s_row0[0])), start_y + 4);
    draw_keyboard_row(s_row1, (int)(sizeof(s_row1) / sizeof(s_row1[0])), start_y + 22);
    draw_keyboard_row(s_row2, (int)(sizeof(s_row2) / sizeof(s_row2[0])), start_y + 40);
    draw_keyboard_row(s_row3, (int)(sizeof(s_row3) / sizeof(s_row3[0])), start_y + 58);
    draw_keyboard_row(s_row4, (int)(sizeof(s_row4) / sizeof(s_row4[0])), start_y + 76);
}

static void draw_remap_overlay(void)
{
    int i;
    int page_count = (NUM_KEY_OPTS + POP_KEYS_PER_PAGE - 1) / POP_KEYS_PER_PAGE;
    int page_start = s_key_opt_page * POP_KEYS_PER_PAGE;
    clear_screen();
    draw_soft_shadow(6, 6, 244, 180, 10);
    fill_round_rect(6, 6, 244, 180, 10, COL_PANEL);
    draw_round_rect(6, 6, 244, 180, 10, COL_BORDER);
    draw_text(16, 14, "BUTTON MAP", COL_TEXT, 2);
    draw_text(16, 32, "TAP A ROW TO EDIT", COL_SUBTEXT, 1);
    for (i = 0; i < s_remap_count && i < 12; i++) {
        int y = 44 + i * 10;
        char keys[20];
        const char *k1 = key_label_for_vid(s_remap_table[i].virtual_output);
        const char *k2 = key_label_for_vid(s_remap_secondary[i]);
        if (s_remap_secondary[i]) snprintf(keys, sizeof(keys), "%s+%s", k1, k2); else snprintf(keys, sizeof(keys), "%s", k1);
        draw_soft_shadow(16, y, 208, 8, 4);
        fill_round_rect(16, y, 208, 8, 4, (i == s_remap_sel) ? COL_SELECTED : COL_KEY);
        draw_round_rect(16, y, 208, 8, 4, COL_BORDER);
        draw_text(22, y + 1, button_name_for_mask(s_remap_table[i].ds_mask), COL_TEXT, 1);
        draw_text(96, y + 1, keys, COL_TEXT, 1);
    }
    draw_pill(80, 172, 96, 14, "DONE", COL_ACCENT, COL_WHITE);
    if (s_remap_popup_active) {
        char page_label[16];
        draw_soft_shadow(POP_X1, POP_Y1, POP_X2 - POP_X1 + 1, POP_Y2 - POP_Y1 + 1, 8);
        fill_round_rect(POP_X1, POP_Y1, POP_X2 - POP_X1 + 1, POP_Y2 - POP_Y1 + 1, 8, COL_PANEL2);
        draw_round_rect(POP_X1, POP_Y1, POP_X2 - POP_X1 + 1, POP_Y2 - POP_Y1 + 1, 8, COL_BORDER);
        draw_text(POP_X1 + 12, POP_Y1 + 10, "SELECT 1 OR 2 KEYS", COL_TEXT, 1);
        draw_pill(POP_PAGE_PREV_X1, POP_PAGE_BTN_Y1, POP_PAGE_PREV_X2 - POP_PAGE_PREV_X1, POP_PAGE_BTN_Y2 - POP_PAGE_BTN_Y1, "PREV", COL_PANEL, COL_WHITE);
        draw_pill(POP_PAGE_NEXT_X1, POP_PAGE_BTN_Y1, POP_PAGE_NEXT_X2 - POP_PAGE_NEXT_X1, POP_PAGE_BTN_Y2 - POP_PAGE_BTN_Y1, "NEXT", COL_PANEL, COL_WHITE);
        snprintf(page_label, sizeof(page_label), "%d/%d", s_key_opt_page + 1, page_count);
        draw_center_text(98, POP_PAGE_BTN_Y1, 60, POP_PAGE_BTN_Y2 - POP_PAGE_BTN_Y1, page_label, COL_WHITE, 1);
        for (i = 0; i < POP_KEYS_PER_PAGE; i++) {
            int idx = page_start + i;
            int row, col, x, y;
            if (idx >= NUM_KEY_OPTS) break;
            row = i / POP_COLS;
            col = i % POP_COLS;
            x = POP_GRID_X + col * (POP_CELL_W + 6);
            y = POP_GRID_Y + row * (POP_CELL_H + 6);
            draw_button(x, y, POP_CELL_W, POP_CELL_H, s_key_opts[idx].label, (s_remap_popup_mask & (1u << idx)) ? 1 : 0);
        }
        draw_pill(POP_APPLY_X1, POP_BTN_Y1, POP_APPLY_X2 - POP_APPLY_X1, POP_BTN_Y2 - POP_BTN_Y1, "APPLY", COL_ACCENT, COL_WHITE);
        draw_pill(POP_CANCEL_X1, POP_BTN_Y1, POP_CANCEL_X2 - POP_CANCEL_X1, POP_BTN_Y2 - POP_BTN_Y1, "CANCEL", COL_PANEL, COL_WHITE);
    }
}

static void redraw_current_ui(void)
{
    if (s_state == UI_STATE_REMAP) draw_remap_overlay();
    else {
        draw_trackpad_ui();
        if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN || s_state == UI_STATE_KBD_SLIDING_OUT) draw_keyboard();
    }
    present_ui();
}

static char key_from_button(const ui_key_t *k)
{
    switch (k->action) {
    case UIKEY_SHIFT: s_shift ^= 1; redraw_current_ui(); return 0;
    case UIKEY_SPACE: return ' ';
    case UIKEY_ENTER: return '\n';
    case UIKEY_BACKSPACE: return 8;
    default: return s_shift ? k->shifted : k->normal;
    }
}

static char hit_row(const ui_key_t *row, int count, int base_y, uint16_t px, uint16_t py)
{
    int i;
    for (i = 0; i < count; i++) {
        if (px >= row[i].x && px < row[i].x + row[i].w && py >= base_y + row[i].y && py < base_y + row[i].y + row[i].h) {
            if (set_pressed_rect(row[i].x, base_y + row[i].y, row[i].w, row[i].h)) redraw_current_ui();
            return key_from_button(&row[i]);
        }
    }
    return 0;
}

static char kbd_hit_test(uint16_t px, uint16_t py)
{
    int start_y = s_kbd_top_px;
    char ch;
    ch = hit_row(s_row0, (int)(sizeof(s_row0) / sizeof(s_row0[0])), start_y + 4, px, py); if (ch) return ch;
    ch = hit_row(s_row1, (int)(sizeof(s_row1) / sizeof(s_row1[0])), start_y + 22, px, py); if (ch) return ch;
    ch = hit_row(s_row2, (int)(sizeof(s_row2) / sizeof(s_row2[0])), start_y + 40, px, py); if (ch) return ch;
    ch = hit_row(s_row3, (int)(sizeof(s_row3) / sizeof(s_row3[0])), start_y + 58, px, py); if (ch) return ch;
    ch = hit_row(s_row4, (int)(sizeof(s_row4) / sizeof(s_row4[0])), start_y + 76, px, py); if (ch) return ch;
    return 0;
}

static int popcount_u32(uint32_t v)
{
    int n = 0;
    while (v) { n += (v & 1u) ? 1 : 0; v >>= 1; }
    return n;
}

static int popup_button_index_at(uint16_t px, uint16_t py)
{
    int page_start = s_key_opt_page * POP_KEYS_PER_PAGE;
    if (px < POP_GRID_X || py < POP_GRID_Y) return -1;
    {
        int rel_x = (int)px - POP_GRID_X, rel_y = (int)py - POP_GRID_Y;
        int stride_x = POP_CELL_W + 6, stride_y = POP_CELL_H + 6;
        int col = rel_x / stride_x, row = rel_y / stride_y, idx;
        if (col < 0 || col >= POP_COLS || row < 0 || row >= POP_ROWS) return -1;
        if ((rel_x % stride_x) >= POP_CELL_W || (rel_y % stride_y) >= POP_CELL_H) return -1;
        idx = page_start + row * POP_COLS + col;
        if (idx >= NUM_KEY_OPTS) return -1;
        return idx;
    }
}

static void popup_open_for_row(int row_idx)
{
    int i;
    if (row_idx < 0 || row_idx >= s_remap_count) return;
    s_remap_popup_active = 1;
    s_remap_popup_target = row_idx;
    s_remap_popup_mask = 0;
    s_key_opt_page = 0;
    for (i = 0; i < NUM_KEY_OPTS; i++) {
        if (s_key_opts[i].vid == s_remap_table[row_idx].virtual_output || s_key_opts[i].vid == s_remap_secondary[row_idx]) {
            s_remap_popup_mask |= (1u << i);
            s_key_opt_page = i / POP_KEYS_PER_PAGE;
        }
    }
    s_touch_debounce = 10;
}

static void popup_close(void)
{
    s_remap_popup_active = 0;
    s_remap_popup_target = -1;
    s_remap_popup_mask = 0;
    s_touch_debounce = 8;
}

void sub_ui_init(void)
{
    int bg;
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    bg = bgInitSub(2, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    s_fb = (uint16_t *)bgGetGfxPtr(bg);
    s_state = UI_STATE_TRACKPAD;
    s_kbd_top_px = SUB_H;
    s_show_pad_panel = 1;
    s_shift = 0;
    s_last_key = 0;
    s_touch_debounce = 0;
    s_remap_sel = -1;
    s_remap_popup_active = 0;
    s_remap_popup_target = -1;
    s_remap_popup_mask = 0;
    s_pressed_active = 0;
    s_key_opt_page = 0;
    s_ui_inited = 1;
    load_default_remaps();
    redraw_current_ui();
}

sub_touch_zone_t sub_ui_process_touch(uint16_t px, uint16_t py, int stylus_down)
{
    int kb_btn_y1 = kb_toggle_y1();
    int kb_btn_y2 = kb_toggle_y2();

    if (!stylus_down) {
        if (clear_pressed_rect()) redraw_current_ui();
        return TOUCH_ZONE_NONE;
    }

    if (s_state == UI_STATE_REMAP) {
        int remap_y0 = 44, row_h = 10;
        if (s_remap_popup_active) {
            int page_count = (NUM_KEY_OPTS + POP_KEYS_PER_PAGE - 1) / POP_KEYS_PER_PAGE;
            if (py >= POP_PAGE_BTN_Y1 && py <= POP_PAGE_BTN_Y2) {
                if (px >= POP_PAGE_PREV_X1 && px <= POP_PAGE_PREV_X2 && set_pressed_rect(POP_PAGE_PREV_X1, POP_PAGE_BTN_Y1, POP_PAGE_PREV_X2 - POP_PAGE_PREV_X1, POP_PAGE_BTN_Y2 - POP_PAGE_BTN_Y1)) redraw_current_ui();
                if (px >= POP_PAGE_NEXT_X1 && px <= POP_PAGE_NEXT_X2 && set_pressed_rect(POP_PAGE_NEXT_X1, POP_PAGE_BTN_Y1, POP_PAGE_NEXT_X2 - POP_PAGE_NEXT_X1, POP_PAGE_BTN_Y2 - POP_PAGE_BTN_Y1)) redraw_current_ui();
            }
            if (py >= POP_BTN_Y1 && py <= POP_BTN_Y2) {
                if (px >= POP_APPLY_X1 && px <= POP_APPLY_X2 && set_pressed_rect(POP_APPLY_X1, POP_BTN_Y1, POP_APPLY_X2 - POP_APPLY_X1, POP_BTN_Y2 - POP_BTN_Y1)) redraw_current_ui();
                if (px >= POP_CANCEL_X1 && px <= POP_CANCEL_X2 && set_pressed_rect(POP_CANCEL_X1, POP_BTN_Y1, POP_CANCEL_X2 - POP_CANCEL_X1, POP_BTN_Y2 - POP_BTN_Y1)) redraw_current_ui();
            }
            {
                int bi = popup_button_index_at(px, py);
                if (bi >= 0) {
                    int local = bi - (s_key_opt_page * POP_KEYS_PER_PAGE);
                    int row = local / POP_COLS, col = local % POP_COLS;
                    int x = POP_GRID_X + col * (POP_CELL_W + 6), y = POP_GRID_Y + row * (POP_CELL_H + 6);
                    if (set_pressed_rect(x, y, POP_CELL_W, POP_CELL_H)) redraw_current_ui();
                }
            }
            if (s_touch_debounce != 0) return TOUCH_ZONE_REMAP_UI;
            if (py >= POP_PAGE_BTN_Y1 && py <= POP_PAGE_BTN_Y2) {
                if (px >= POP_PAGE_PREV_X1 && px <= POP_PAGE_PREV_X2) { if (s_key_opt_page > 0) s_key_opt_page--; s_touch_debounce = 8; redraw_current_ui(); return TOUCH_ZONE_REMAP_UI; }
                if (px >= POP_PAGE_NEXT_X1 && px <= POP_PAGE_NEXT_X2) { if (s_key_opt_page + 1 < page_count) s_key_opt_page++; s_touch_debounce = 8; redraw_current_ui(); return TOUCH_ZONE_REMAP_UI; }
            }
            if (py >= POP_BTN_Y1 && py <= POP_BTN_Y2) {
                if (px >= POP_APPLY_X1 && px <= POP_APPLY_X2) {
                    int n = popcount_u32(s_remap_popup_mask);
                    if (n >= 1 && n <= 2 && s_remap_popup_target >= 0 && s_remap_popup_target < s_remap_count) {
                        uint8_t out1 = 0, out2 = 0; int i;
                        for (i = 0; i < NUM_KEY_OPTS; i++) if (s_remap_popup_mask & (1u << i)) { if (!out1) out1 = s_key_opts[i].vid; else if (!out2) out2 = s_key_opts[i].vid; }
                        s_remap_table[s_remap_popup_target].virtual_output = out1;
                        s_remap_secondary[s_remap_popup_target] = out2;
                    }
                    popup_close(); redraw_current_ui(); return TOUCH_ZONE_REMAP_UI;
                }
                if (px >= POP_CANCEL_X1 && px <= POP_CANCEL_X2) { popup_close(); redraw_current_ui(); return TOUCH_ZONE_REMAP_UI; }
            }
            {
                int bi = popup_button_index_at(px, py);
                if (bi >= 0) {
                    uint32_t bit = (1u << bi);
                    if (s_remap_popup_mask & bit) s_remap_popup_mask &= ~bit; else if (popcount_u32(s_remap_popup_mask) < 2) s_remap_popup_mask |= bit;
                    s_touch_debounce = 8; redraw_current_ui(); return TOUCH_ZONE_REMAP_UI;
                }
            }
            return TOUCH_ZONE_REMAP_UI;
        }

        if (py >= 172) {
            if (set_pressed_rect(80, 172, 96, 14)) redraw_current_ui();
            s_touch_debounce = 12; s_state = UI_STATE_TRACKPAD; s_remap_sel = -1; redraw_current_ui(); return TOUCH_ZONE_REMAP_UI;
        }
        if (py >= remap_y0 && py < remap_y0 + s_remap_count * row_h) {
            int idx = (py - remap_y0) / row_h;
            if (idx >= 0 && idx < s_remap_count && s_touch_debounce == 0) { s_remap_sel = idx; popup_open_for_row(idx); redraw_current_ui(); }
            return TOUCH_ZONE_REMAP_UI;
        }
        return TOUCH_ZONE_REMAP_UI;
    }

    if (px >= KB_BTN_X1 && px <= KB_BTN_X2 && py >= kb_btn_y1 && py <= kb_btn_y2) {
        if (set_pressed_rect(KB_BTN_X1, kb_btn_y1, KB_BTN_X2 - KB_BTN_X1, kb_btn_y2 - kb_btn_y1 + 1)) redraw_current_ui();
        if (s_touch_debounce == 0) { s_touch_debounce = 15; if (s_state == UI_STATE_TRACKPAD) s_state = UI_STATE_KBD_SLIDING_IN; else if (s_state == UI_STATE_KBD_OPEN) s_state = UI_STATE_KBD_SLIDING_OUT; }
        return TOUCH_ZONE_KB_BTN;
    }
    if (px >= PAD_TOGGLE_X1 && px <= PAD_TOGGLE_X2 && py >= PAD_TOGGLE_Y1 && py <= PAD_TOGGLE_Y2) {
        if (set_pressed_rect(PAD_TOGGLE_X1, PAD_TOGGLE_Y1, 40, 16)) redraw_current_ui();
        if (s_touch_debounce == 0) { s_touch_debounce = 15; s_show_pad_panel ^= 1; redraw_current_ui(); }
        return TOUCH_ZONE_REMAP_UI;
    }
    if (px >= MAG_BTN_X1 && px <= MAG_BTN_X2 && py >= MAG_BTN_Y1 && py <= MAG_BTN_Y2) {
        if (set_pressed_rect(MAG_BTN_X1, MAG_BTN_Y1, 42, 16)) redraw_current_ui();
        if (s_touch_debounce == 0) {
            s_touch_debounce = 15;
            if (!g_cfg.magnifier_enabled) { g_cfg.magnifier_enabled = 1; if (g_cfg.magnifier_zoom < 2) g_cfg.magnifier_zoom = 2; }
            else if (g_cfg.magnifier_zoom == 2) g_cfg.magnifier_zoom = 3;
            else { g_cfg.magnifier_enabled = 0; g_cfg.magnifier_zoom = 2; }
            redraw_current_ui();
        }
        return TOUCH_ZONE_REMAP_UI;
    }
    if (px >= MODE_BTN_X1 && px <= MODE_BTN_X2 && py >= MODE_BTN_Y1 && py <= MODE_BTN_Y2) {
        if (set_pressed_rect(MODE_BTN_X1, MODE_BTN_Y1, 24, 16)) redraw_current_ui();
        if (s_touch_debounce == 0) { s_touch_debounce = 15; g_cfg.magnifier_mode ^= 1; redraw_current_ui(); }
        return TOUCH_ZONE_REMAP_UI;
    }
    if (px >= REMAP_BTN_X1 && px <= REMAP_BTN_X2 && py >= REMAP_BTN_Y1 && py <= REMAP_BTN_Y2) {
        if (set_pressed_rect(REMAP_BTN_X1, REMAP_BTN_Y1, 20, 16)) redraw_current_ui();
        if (s_touch_debounce == 0) { s_touch_debounce = 15; if (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN) s_kbd_top_px = SUB_H; s_state = UI_STATE_REMAP; s_remap_sel = -1; redraw_current_ui(); }
        return TOUCH_ZONE_REMAP_BTN;
    }
    if (s_show_pad_panel && px < 124 && py < ((s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN || s_state == UI_STATE_KBD_SLIDING_OUT) ? kb_btn_y1 : KB_BTN_Y1)) return TOUCH_ZONE_REMAP_UI;
    if ((s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN || s_state == UI_STATE_KBD_SLIDING_OUT) && py >= (uint16_t)s_kbd_top_px) {
        char ch = kbd_hit_test(px, py); if (ch) s_last_key = (uint8_t)ch; return TOUCH_ZONE_KEYBOARD;
    }
    return TOUCH_ZONE_TRACKPAD;
}

void sub_ui_update(void)
{
    if (!s_ui_inited) return;
    if (s_touch_debounce > 0) s_touch_debounce--;
    switch (s_state) {
    case UI_STATE_KBD_SLIDING_IN:
        s_kbd_top_px -= SLIDE_SPEED;
        if (s_kbd_top_px <= SUB_H - KBD_HEIGHT) { s_kbd_top_px = SUB_H - KBD_HEIGHT; if (s_kbd_top_px < KBD_MIN_TRACK_PX) s_kbd_top_px = KBD_MIN_TRACK_PX; s_state = UI_STATE_KBD_OPEN; }
        redraw_current_ui();
        break;
    case UI_STATE_KBD_SLIDING_OUT:
        s_kbd_top_px += SLIDE_SPEED;
        if (s_kbd_top_px >= SUB_H) { s_kbd_top_px = SUB_H; s_state = UI_STATE_TRACKPAD; }
        redraw_current_ui();
        break;
    case UI_STATE_REMAP:
    default:
        break;
    }
}

uint8_t sub_ui_get_key(void) { uint8_t k = s_last_key; s_last_key = 0; return k; }
int sub_ui_kbd_visible(void) { return (s_state == UI_STATE_KBD_OPEN || s_state == UI_STATE_KBD_SLIDING_IN || s_state == UI_STATE_KBD_SLIDING_OUT); }
int sub_ui_remap_active(void) { return (s_state == UI_STATE_REMAP); }
const dsrd_remap_entry_t *sub_ui_get_remap_table(int *count) { if (count) *count = s_remap_count; return s_remap_table; }
uint8_t sub_ui_get_secondary_output(int idx) { if (idx < 0 || idx >= s_remap_count) return 0; return s_remap_secondary[idx]; }
