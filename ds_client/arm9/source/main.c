/*============================================================================
 * ds_client/arm9/source/main.c
 *
 * DS Remote Desktop — ARM9 entry point.
 *
 * Screen layout:
 *   Top    (main engine): 256×192 8-bit paletted video from PC stream
 *   Bottom (sub engine) : Exclusive trackpad with slide-out keyboard
 *                         and remap overlay, managed by sub_ui.
 *==========================================================================*/
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "nifi_net.h"
#include "video_decode.h"
#include "input.h"
#include "keyboard_ui.h"
#include "sub_ui.h"
#include "audio_stream.h"
#include "hmac_auth.h"
#include "config.h"

/*--------------------------------------------------------------------------*/
static volatile int s_vblank = 0;
static void vblank_handler(void) { s_vblank = 1; }

/*--------------------------------------------------------------------------*/
int main(void)
{
    /* ---- Hardware init ------------------------------------------------ */
    powerOn(POWER_ALL_2D);
    defaultExceptionHandler();

    irqSet(IRQ_VBLANK, vblank_handler);
    irqEnable(IRQ_VBLANK);

    /* Top screen: Mode 5, BG2 = 8-bit paletted bitmap 256x192 */
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    int bg = bgInit(2, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    (void)bg;

    /* Bottom screen: initialised by sub_ui_init() (text console) */

    /* ---- Subsystem init ---------------------------------------------- */
    dsrd_config_init();
    dsrd_video_init();
    sub_ui_init();          /* bottom screen: trackpad + slide-out KB */
    dsrd_input_init();
    dsrd_keyboard_init();   /* compatibility shim — delegates to sub_ui */
    dsrd_audio_init();
    dsrd_hmac_init();

    /* ---- NiFi networking --------------------------------------------- */
    dsrd_nifi_init();

    /* ---- Main loop --------------------------------------------------- */
    for (;;) {
        swiWaitForVBlank();
        s_vblank = 0;

        /* Poll NiFi — drives rx callbacks */
        dsrd_nifi_poll();

        /* Process any received video frame → top screen */
        dsrd_video_process();

        /* Animate sub-screen UI (keyboard slide, remap overlay) */
        sub_ui_update();

        /* Read input & build telemetry (touch routed through sub_ui) */
        dsrd_input_poll();

        /* Transmit upstream telemetry */
        dsrd_nifi_send_telemetry();

        /* Periodically send congestion report */
        dsrd_nifi_send_congestion();
    }

    return 0;
}
