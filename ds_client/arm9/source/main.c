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

    /* Bring up the sub-screen UI early so boot status is visible.
       We will re-init it after networking so the normal trackpad layout
       starts cleanly. */
    sub_ui_init();
    iprintf("DS Remote Desktop\n");
    iprintf("Booting...\n\n");

    /* ---- Subsystem init ---------------------------------------------- */
    iprintf("[1/7] config\n");
    dsrd_config_init();

    iprintf("[2/7] video\n");
    dsrd_video_init();

    iprintf("[3/7] input\n");
    dsrd_input_init();

    iprintf("[4/7] keyboard\n");
    dsrd_keyboard_init();   /* compatibility shim — delegates to sub_ui */

    iprintf("[5/7] audio\n");
    dsrd_audio_init();

    iprintf("[6/7] hmac\n");
    dsrd_hmac_init();

    /* ---- NiFi networking --------------------------------------------- */
    iprintf("[7/7] wireless\n");
    iprintf("Starting NiFi...\n");
    dsrd_nifi_init();
    iprintf("NiFi init returned\n");
    iprintf("Waiting for Wii announce...\n");

    /* Re-initialise the normal bottom-screen UI so the runtime layout
       starts from a clean state after boot messages. */
    sub_ui_init();

    /* Re-print runtime connection hint AFTER sub_ui_init(), because
       sub_ui_init() clears/redraws the sub-screen console. */
    iprintf("NiFi ready. Waiting for Wii announce...\n");

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
