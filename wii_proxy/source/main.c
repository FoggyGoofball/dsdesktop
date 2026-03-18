/*============================================================================
 * wii_proxy/source/main.c
 *
 * Wii Protocol Proxy — Entry point.
 *
 * The Wii sits between the PC (via TCP/IP over Wi-Fi or USB Ethernet)
 * and the DS (via NiFi raw 802.11b ad-hoc).
 *
 * Flow:
 *   PC  --[UDP/IP]--> Wii --[NiFi 802.11b]--> DS   (video + audio)
 *   DS  --[NiFi]----> Wii --[UDP/IP]--------> PC   (telemetry)
 *
 * The Wii strips IP headers and re-wraps payloads into NiFi frames,
 * while spoofing the local CMD/ACK handshakes the DS expects every 4 ms.
 *==========================================================================*/
#include <gccore.h>
#include <network.h>
#include <wiiuse/wpad.h>
#include <ogc/lwp.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "nifi_tx.h"
#include "nifi_rx.h"
#include "ack_spoof.h"
#include "backhaul.h"
#include "config_ui.h"
#include "channel_calib.h"
#include "../../common/protocol.h"

/* ---------- Globals ---------------------------------------------------- */
static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

/*--------------------------------------------------------------------------
 * Video / console init
 *------------------------------------------------------------------------*/
static void video_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20,
                 rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();
}

/*--------------------------------------------------------------------------
 * MAIN
 *------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    video_init();
    WPAD_Init();
    fatInitDefault();

    printf("=== DS Remote Desktop — Wii Proxy ===\n\n");

    /* Load or display config UI */
    wii_config_t cfg;
    wii_config_load(&cfg);
    wii_config_show(&cfg);

    /* Initialise backhaul (Wi-Fi or USB Ethernet) */
    printf("Initialising backhaul (%s)...\n",
           cfg.use_usb_ethernet ? "USB Ethernet" : "Wi-Fi");
    if (backhaul_init(&cfg) < 0) {
        printf("FATAL: backhaul init failed.\n");
        goto hang;
    }
    printf("Backhaul ready. IP = %s\n", backhaul_ip_str());

    /* Initialise NiFi radio (promiscuous / ad-hoc) */
    printf("Initialising NiFi radio...\n");
    nifi_tx_init(&cfg);
    nifi_rx_init(&cfg);

    /* Start ACK spoofing immediately so DS 4ms timing is preserved
       even during channel-calibration probe traffic. */
    ack_spoof_start();

    /* Optional: benchmark candidate channels and lock lowest-latency one */
    if (cfg.auto_channel) {
        uint8_t best = channel_calib_run(&cfg);
        if (best >= 1 && best <= 11) {
            cfg.wifi_channel = best;
            printf("Using calibrated channel: %u\n", cfg.wifi_channel);
        }
    }

    printf("\nProxy active. Listening on UDP %d...\n", DSRD_PORT);

    /* ---- Main loop ---------------------------------------------------- */
    /* Two threads:
     *   - Main thread: poll backhaul socket → NiFi TX
     *   - LWP thread:  NiFi RX → backhaul socket (upstream telemetry)
     *
     * We keep it single-threaded here using non-blocking I/O for
     * simplicity and determinism on the Wii's single-core Broadway CPU.
     */
    static uint8_t rx_buf[DSRD_MTU + 64];
    static uint8_t nifi_rx_buf[DSRD_MTU + 64];

    for (;;) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        if (pressed & WPAD_BUTTON_HOME) break;  /* exit */

        /* --- Downstream: PC → Wii → DS -------------------------------- */
        int n = backhaul_recv(rx_buf, sizeof(rx_buf));
        if (n > 0) {
            /* Validate DSRD header */
            if (n >= (int)sizeof(dsrd_header_t)) {
                const dsrd_header_t *hdr = (const dsrd_header_t *)rx_buf;
                if (dsrd_header_valid(hdr)) {
                    nifi_tx_send(rx_buf, (uint16_t)n);
                }
            }
        }

        /* --- Upstream: DS → Wii → PC ---------------------------------- */
        int m = nifi_rx_recv(nifi_rx_buf, sizeof(nifi_rx_buf));
        if (m > 0) {
            backhaul_send(nifi_rx_buf, (uint16_t)m);
        }

        /* Yield briefly */
        usleep(200);
    }

hang:
    printf("Shutting down...\n");
    ack_spoof_stop();
    backhaul_shutdown();
    return 0;
}
