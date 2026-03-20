/*============================================================================
 * pc_host/src/input_inject.cpp
 *
 * Translates DS telemetry into real OS-level inputs.
 *
 * Windows:
 *   - Mouse    → SendInput (MOUSEINPUT, absolute coords)
 *   - Keyboard → SendInput (KEYBDINPUT, virtual key codes)
 *   - Gamepad  → vJoy SDK  (SetAxis / SetBtn)
 *
 * Linux:
 *   - Mouse    → uinput (EV_ABS / EV_REL)
 *   - Keyboard → uinput (EV_KEY)
 *   - Gamepad  → uinput (EV_ABS + EV_KEY)
 *
 * The remap table maps DS button combos (from dsrd_remap_entry_t) to
 * virtual output IDs, which are then translated to platform events.
 *==========================================================================*/
#include <cstdio>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <linux/uinput.h>
#  include <linux/input.h>
#  include <sys/ioctl.h>
#endif

#include "../../common/protocol.h"
#include "../include/input_inject.h"

/*--------------------------------------------------------------------------
 * Remap table — virtual output IDs to platform key/button codes
 *------------------------------------------------------------------------*/
typedef struct {
    uint8_t  virtual_id;      /* matches dsrd_remap_entry_t::virtual_output */
    uint16_t platform_code;   /* VK_xxx on Windows, KEY_xxx on Linux        */
} remap_binding_t;

#define MAX_BINDINGS 32
static remap_binding_t s_bindings[MAX_BINDINGS];
static int s_binding_count = 0;

/* Track previous state for edge detection */
static uint32_t s_prev_buttons = 0;
static uint8_t  s_prev_remap_a = 0;
static uint8_t  s_prev_remap_b = 0;

#ifndef _WIN32
static int s_uinput_fd = -1;
#endif

/*--------------------------------------------------------------------------
 * Platform init
 *------------------------------------------------------------------------*/
int input_inject_init(void)
{
#ifdef _WIN32
    /* Nothing to init for SendInput; vJoy would be loaded here */
    printf("  Input injection: Windows SendInput\n");
#else
    s_uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (s_uinput_fd < 0) {
        perror("open /dev/uinput");
        return -1;
    }

    /* Enable key events */
    ioctl(s_uinput_fd, UI_SET_EVBIT, EV_KEY);
    for (int i = 0; i < 256; i++)
        ioctl(s_uinput_fd, UI_SET_KEYBIT, i);

    /* Enable relative mouse */
    ioctl(s_uinput_fd, UI_SET_EVBIT, EV_REL);
    ioctl(s_uinput_fd, UI_SET_RELBIT, REL_X);
    ioctl(s_uinput_fd, UI_SET_RELBIT, REL_Y);

    /* Enable absolute mouse (for trackpad) */
    ioctl(s_uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(s_uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(s_uinput_fd, UI_SET_ABSBIT, ABS_Y);

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "DS Remote Desktop");
    setup.id.bustype = BUS_USB;
    setup.id.vendor  = 0x1234;
    setup.id.product = 0x5678;

    ioctl(s_uinput_fd, UI_DEV_SETUP, &setup);

    /* Set up absolute axis ranges for touch */
    struct uinput_abs_setup abs_x = {};
    abs_x.code = ABS_X;
    abs_x.absinfo.minimum = 0;
    abs_x.absinfo.maximum = 255;
    ioctl(s_uinput_fd, UI_ABS_SETUP, &abs_x);

    struct uinput_abs_setup abs_y = {};
    abs_y.code = ABS_Y;
    abs_y.absinfo.minimum = 0;
    abs_y.absinfo.maximum = 191;
    ioctl(s_uinput_fd, UI_ABS_SETUP, &abs_y);

    ioctl(s_uinput_fd, UI_DEV_CREATE);

    printf("  Input injection: Linux uinput\n");
#endif
    return 0;
}

void input_inject_shutdown(void)
{
#ifndef _WIN32
    if (s_uinput_fd >= 0) {
        ioctl(s_uinput_fd, UI_DEV_DESTROY);
        close(s_uinput_fd);
        s_uinput_fd = -1;
    }
#endif
}

/*--------------------------------------------------------------------------
 * Load default remap bindings
 *------------------------------------------------------------------------*/
void input_inject_load_remaps(void)
{
    s_binding_count = 0;

#ifdef _WIN32
    s_bindings[s_binding_count++] = { 100, 'A' };
    s_bindings[s_binding_count++] = { 101, 'B' };
    s_bindings[s_binding_count++] = { 102, 'X' };
    s_bindings[s_binding_count++] = { 103, 'Y' };
    s_bindings[s_binding_count++] = { 104, VK_SPACE };
    s_bindings[s_binding_count++] = { 105, VK_RETURN };
    s_bindings[s_binding_count++] = { 106, VK_ESCAPE };
    s_bindings[s_binding_count++] = { 107, VK_TAB };
    s_bindings[s_binding_count++] = { 108, VK_SHIFT };
    s_bindings[s_binding_count++] = { 109, VK_CONTROL };
    s_bindings[s_binding_count++] = { 110, VK_MENU };     /* ALT */
    s_bindings[s_binding_count++] = { 111, VK_BACK };
#else
    s_bindings[s_binding_count++] = { 100, KEY_A };
    s_bindings[s_binding_count++] = { 101, KEY_B };
    s_bindings[s_binding_count++] = { 102, KEY_X };
    s_bindings[s_binding_count++] = { 103, KEY_Y };
    s_bindings[s_binding_count++] = { 104, KEY_SPACE };
    s_bindings[s_binding_count++] = { 105, KEY_ENTER };
    s_bindings[s_binding_count++] = { 106, KEY_ESC };
    s_bindings[s_binding_count++] = { 107, KEY_TAB };
    s_bindings[s_binding_count++] = { 108, KEY_LEFTSHIFT };
    s_bindings[s_binding_count++] = { 109, KEY_LEFTCTRL };
    s_bindings[s_binding_count++] = { 110, KEY_LEFTALT };
    s_bindings[s_binding_count++] = { 111, KEY_BACKSPACE };
#endif
}

/*--------------------------------------------------------------------------
 * Inject a keyboard event
 *------------------------------------------------------------------------*/
static void inject_key(uint16_t code, int pressed)
{
#ifdef _WIN32
    INPUT inp;
    memset(&inp, 0, sizeof(inp));
    inp.type           = INPUT_KEYBOARD;
    inp.ki.wVk         = code;
    inp.ki.dwFlags     = pressed ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &inp, sizeof(INPUT));
#else
    if (s_uinput_fd < 0) return;
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type  = EV_KEY;
    ev.code  = code;
    ev.value = pressed ? 1 : 0;
    write(s_uinput_fd, &ev, sizeof(ev));

    /* Sync */
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
    write(s_uinput_fd, &ev, sizeof(ev));
#endif
}

/*--------------------------------------------------------------------------
 * Inject mouse movement (absolute, normalised to screen)
 *------------------------------------------------------------------------*/
static void inject_mouse(uint16_t x, uint16_t y, int pressed)
{
#ifdef _WIN32
    INPUT inp;
    memset(&inp, 0, sizeof(inp));
    inp.type         = INPUT_MOUSE;
    /* Normalise DS touch (0-255, 0-191) to screen coords (0-65535) */
    inp.mi.dx        = (x * 65535) / 255;
    inp.mi.dy        = (y * 65535) / 191;
    inp.mi.dwFlags   = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    if (pressed)
        inp.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &inp, sizeof(INPUT));
#else
    if (s_uinput_fd < 0) return;
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS; ev.code = ABS_X; ev.value = x;
    write(s_uinput_fd, &ev, sizeof(ev));

    ev.type = EV_ABS; ev.code = ABS_Y; ev.value = y;
    write(s_uinput_fd, &ev, sizeof(ev));

    if (pressed) {
        ev.type = EV_KEY; ev.code = BTN_LEFT; ev.value = 1;
        write(s_uinput_fd, &ev, sizeof(ev));
    }

    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
    write(s_uinput_fd, &ev, sizeof(ev));
#endif
}

/*--------------------------------------------------------------------------
 * remap lookup helpers
 *------------------------------------------------------------------------*/
static int remap_contains(uint8_t a, uint8_t b, uint8_t v)
{
    return (v != 0) && (a == v || b == v);
}

static uint16_t remap_lookup_code(uint8_t vid)
{
    for (int i = 0; i < s_binding_count; i++) {
        if (s_bindings[i].virtual_id == vid)
            return s_bindings[i].platform_code;
    }
    return 0;
}

/*--------------------------------------------------------------------------
 * Process a telemetry packet
 *------------------------------------------------------------------------*/
void input_inject_process(const dsrd_telemetry_t *tel)
{
    /* --- Touch / trackpad -------------------------------------------- */
    if (tel->touch_down) {
        inject_mouse(tel->touch_x, tel->touch_y, 1);
    }

    /* --- On-screen keyboard ------------------------------------------ */
    if (tel->kbd_scancode > 0 && tel->kbd_scancode < 128) {
#ifdef _WIN32
        SHORT vk = VkKeyScan((CHAR)tel->kbd_scancode);
        if (vk != -1) {
            inject_key(vk & 0xFF, 1);
            inject_key(vk & 0xFF, 0);
        }
#else
        /* Simple ASCII → linux keycode mapping for common chars */
        inject_key((uint16_t)tel->kbd_scancode, 1);
        inject_key((uint16_t)tel->kbd_scancode, 0);
#endif
    }

    /* --- Button remaps (up to two simultaneous outputs) --------------- */
    uint8_t cur_a = tel->remap_id;
    uint8_t cur_b = tel->_pad;

    /* Release keys that are no longer active */
    if (s_prev_remap_a && !remap_contains(cur_a, cur_b, s_prev_remap_a)) {
        uint16_t code = remap_lookup_code(s_prev_remap_a);
        if (code) inject_key(code, 0);
    }
    if (s_prev_remap_b && s_prev_remap_b != s_prev_remap_a &&
        !remap_contains(cur_a, cur_b, s_prev_remap_b)) {
        uint16_t code = remap_lookup_code(s_prev_remap_b);
        if (code) inject_key(code, 0);
    }

    /* Press newly active keys */
    if (cur_a && !remap_contains(s_prev_remap_a, s_prev_remap_b, cur_a)) {
        uint16_t code = remap_lookup_code(cur_a);
        if (code) inject_key(code, 1);
    }
    if (cur_b && cur_b != cur_a &&
        !remap_contains(s_prev_remap_a, s_prev_remap_b, cur_b)) {
        uint16_t code = remap_lookup_code(cur_b);
        if (code) inject_key(code, 1);
    }

    s_prev_buttons = tel->buttons_held;
    s_prev_remap_a = cur_a;
    s_prev_remap_b = cur_b;
}
