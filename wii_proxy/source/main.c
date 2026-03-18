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
#include <ogc/lwp_watchdog.h>
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
#include "ip_manager.h"
#include "setup_wizard.h"
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

/* -------------------------------------------------------------------------
 * Runtime prerequisite checks
 * -------------------------------------------------------------------------*/
static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int check_runtime_prereqs(void)
{
    const char *hx_path = "sd:/apps/dsremote/haxxstation.nds";

    if (file_exists(hx_path))
        return 1;

    printf("\n");
    printf("================================================================\n");
    printf("MISSING PREREQUISITE: haxxstation.nds\n");
    printf("----------------------------------------------------------------\n");
    printf("Expected path:\n");
    printf("  %s\n\n", hx_path);

    printf("Why it is missing from this download:\n");
    printf("  This project does NOT redistribute Nintendo DS Download Station\n");
    printf("  derived assets. You must provide your own legally obtained copy.\n\n");

    printf("How to integrate:\n");
    printf("  1) Obtain your own legal DS Download Station Vol.1 source.\n");
    printf("  2) Prepare/patch it in your existing sender workflow.\n");
    printf("  3) Place resulting file as:\n");
    printf("       sd:/apps/dsremote/haxxstation.nds\n");
    printf("  4) Reboot this app, then perform DS payload transfer.\n\n");

    printf("Notes:\n");
    printf("  - If your DS is already modded and you do not use this transfer\n");
    printf("    method, you may continue without this file.\n\n");

    printf("Controls:\n");
    printf("  A = continue anyway\n");
    printf("  B = exit now\n");
    printf("================================================================\n");

    while (1) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        if (pressed & WPAD_BUTTON_A)
            return 1;
        if (pressed & WPAD_BUTTON_B)
            return 0;
        usleep(100000);
    }
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

    if (!check_runtime_prereqs())
        return 1;

    printf("=== DS Remote Desktop — Wii Proxy ===\n\n");

    /* Load or display config UI */
    wii_config_t cfg;
    wii_config_load(&cfg);

    /* Check if this is first boot (no config loaded).
       If so, run setup wizard to configure PC IP and mode. */
    if (cfg.pc_ip[0] == '\0' || strcmp(cfg.pc_ip, "192.168.1.100") == 0) {
        /* Likely first boot (using default IP) — run wizard */
        printf("First boot detected. Starting setup wizard...\n");
        usleep(1000000);
        if (setup_wizard_run(&cfg) < 0) {
            printf("Setup failed. Using defaults and continuing.\n");
            usleep(2000000);
        }
    }

    wii_config_show(&cfg);

    /* Load saved IPs from SD card */
    ip_manager_load();

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
    printf("Press SELECT to manage PC IPs\n");
    printf("Press HOME to exit\n\n");

    /* ---- Connection state machine ------------------------------------- */
    dsrd_conn_state_t conn_state = CONN_ANNOUNCING;
    uint32_t session_token = (uint32_t)gettime();  /* random-ish */
    uint16_t announce_timer = 0;
    uint16_t heartbeat_timer = 0;
    uint16_t heartbeat_watchdog = 0;
    uint16_t hs_seq = 0;

    static uint8_t hs_buf[sizeof(dsrd_header_t) + sizeof(dsrd_handshake_t)];

    printf("Waiting for DS client...\n");

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

        /* Handle SELECT button: show IP manager menu */
        if (pressed & WPAD_BUTTON_MINUS) {
            printf("\n--- IP Manager ---\n");
            if (ip_manager_show_menu(&cfg)) {
                /* User selected an IP, try to reconnect */
                printf("Reconnecting to PC at: %s\n", cfg.pc_ip);
                backhaul_reconnect(&cfg);
            }
            printf("Resuming proxy...\n\n");
        }

        /* ---- Connection state machine -------------------------------- */
        switch (conn_state) {

        case CONN_IDLE:
            conn_state = CONN_ANNOUNCING;
            announce_timer = 0;
            break;

        case CONN_ANNOUNCING: {
            /* Periodically broadcast ANNOUNCE so the DS knows we exist */
            announce_timer++;
            if (announce_timer >= DSRD_ANNOUNCE_INTERVAL) {
                announce_timer = 0;

                dsrd_header_t *hdr = (dsrd_header_t *)hs_buf;
                dsrd_header_init(hdr, PKT_HANDSHAKE, hs_seq++,
                                 sizeof(dsrd_handshake_t), 1 /* Wii */, 0);
                dsrd_handshake_t *hs =
                    (dsrd_handshake_t *)(hs_buf + sizeof(dsrd_header_t));
                hs->hs_type       = HS_ANNOUNCE;
                hs->client_id     = 1;
                hs->proto_version = DSRD_VERSION;
                hs->_pad          = 0;
                hs->session_token = session_token;

                nifi_tx_send(hs_buf, sizeof(hs_buf));
            }
            break;
        }

        case CONN_CONNECTED: {
            /* Send heartbeat periodically */
            heartbeat_timer++;
            if (heartbeat_timer >= DSRD_HEARTBEAT_INTERVAL) {
                heartbeat_timer = 0;

                dsrd_header_t *hdr = (dsrd_header_t *)hs_buf;
                dsrd_header_init(hdr, PKT_HANDSHAKE, hs_seq++,
                                 sizeof(dsrd_handshake_t), 1 /* Wii */, 0);
                dsrd_handshake_t *hs =
                    (dsrd_handshake_t *)(hs_buf + sizeof(dsrd_header_t));
                hs->hs_type       = HS_HEARTBEAT;
                hs->client_id     = 1;
                hs->proto_version = DSRD_VERSION;
                hs->_pad          = 0;
                hs->session_token = session_token;

                nifi_tx_send(hs_buf, sizeof(hs_buf));
            }

            /* Check heartbeat watchdog */
            heartbeat_watchdog++;
            if (heartbeat_watchdog >= DSRD_HEARTBEAT_TIMEOUT) {
                printf("DS heartbeat lost! Returning to ANNOUNCE.\n");
                conn_state = CONN_ANNOUNCING;
                announce_timer = 0;
                heartbeat_watchdog = 0;
            }
            break;
        }

        case CONN_LOST:
            printf("Connection lost. Re-announcing...\n");
            conn_state = CONN_ANNOUNCING;
            announce_timer = 0;
            break;

        default:
            break;
        }

        /* --- Downstream: PC → Wii → DS -------------------------------- */
        /* Only forward video/audio if we have an active DS connection */
        int n = backhaul_recv(rx_buf, sizeof(rx_buf));
        if (n > 0) {
            if (n >= (int)sizeof(dsrd_header_t)) {
                const dsrd_header_t *hdr = (const dsrd_header_t *)rx_buf;
                if (dsrd_header_valid(hdr)) {
                    if (conn_state == CONN_CONNECTED) {
                        /* Normal operation: forward to DS */
                        nifi_tx_send(rx_buf, (uint16_t)n);
                    }
                    /* If not connected, silently drop PC data
                       (DS can't receive it yet) */
                }
            }
        }

        /* --- Upstream: DS → Wii → PC ---------------------------------- */
        int m = nifi_rx_recv(nifi_rx_buf, sizeof(nifi_rx_buf));
        if (m > 0) {
            if (m >= (int)sizeof(dsrd_header_t)) {
                const dsrd_header_t *rx_hdr =
                    (const dsrd_header_t *)nifi_rx_buf;

                if (dsrd_header_valid(rx_hdr) &&
                    rx_hdr->type == PKT_HANDSHAKE &&
                    rx_hdr->payload_len >= sizeof(dsrd_handshake_t)) {

                    const dsrd_handshake_t *hs_in =
                        (const dsrd_handshake_t *)
                            (nifi_rx_buf + sizeof(dsrd_header_t));

                    if (hs_in->hs_type == HS_JOIN) {
                        /* DS wants to connect — send ACCEPT */
                        printf("DS JOIN received (id=%u). Accepting...\n",
                               hs_in->client_id);
                        session_token = hs_in->session_token;

                        dsrd_header_t *ahdr = (dsrd_header_t *)hs_buf;
                        dsrd_header_init(ahdr, PKT_HANDSHAKE, hs_seq++,
                                         sizeof(dsrd_handshake_t), 1, 0);
                        dsrd_handshake_t *acc =
                            (dsrd_handshake_t *)(hs_buf + sizeof(*ahdr));
                        acc->hs_type       = HS_ACCEPT;
                        acc->client_id     = 1;
                        acc->proto_version = DSRD_VERSION;
                        acc->_pad          = 0;
                        acc->session_token = session_token;

                        nifi_tx_send(hs_buf, sizeof(hs_buf));

                        conn_state = CONN_CONNECTED;
                        heartbeat_watchdog = 0;
                        heartbeat_timer = 0;
                        printf("DS connected! Streaming active.\n");

                    } else if (hs_in->hs_type == HS_HEARTBEAT) {
                        /* Reset watchdog */
                        heartbeat_watchdog = 0;

                    } else if (hs_in->hs_type == HS_DISCONNECT) {
                        printf("DS disconnected gracefully.\n");
                        conn_state = CONN_ANNOUNCING;
                        announce_timer = 0;
                    }

                } else if (dsrd_header_valid(rx_hdr)) {
                    /* Normal telemetry/congestion — forward to PC */
                    if (conn_state == CONN_CONNECTED) {
                        backhaul_send(nifi_rx_buf, (uint16_t)m);
                        heartbeat_watchdog = 0;  /* any rx = alive */
                    }
                }
            }
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
