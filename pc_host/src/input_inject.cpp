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
static uint8_t  s_prev_remap   = 0;

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

    /* Default: virtual_id 100 → Enter key */
#ifdef _WIN32
    s_bindings[s_binding_count++] = { 100, VK_RETURN };
    s_bindings[s_binding_count++] = { 101, VK_SNAPSHOT };
    s_bindings[s_binding_count++] = { 102, VK_ESCAPE };
#else
    s_bindings[s_binding_count++] = { 100, KEY_ENTER };
    s_bindings[s_binding_count++] = { 101, KEY_SYSRQ };
    s_bindings[s_binding_count++] = { 102, KEY_ESC };
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

    /* --- Button remaps ------------------------------------------------ */
    if (tel->remap_id != 0 && tel->remap_id != s_prev_remap) {
        /* New remap activated — find binding */
        for (int i = 0; i < s_binding_count; i++) {
            if (s_bindings[i].virtual_id == tel->remap_id) {
                inject_key(s_bindings[i].platform_code, 1);
                break;
            }
        }
    } else if (tel->remap_id == 0 && s_prev_remap != 0) {
        /* Remap released */
        for (int i = 0; i < s_binding_count; i++) {
            if (s_bindings[i].virtual_id == s_prev_remap) {
                inject_key(s_bindings[i].platform_code, 0);
                break;
            }
        }
    }

    s_prev_buttons = tel->buttons_held;
    s_prev_remap   = tel->remap_id;
}
