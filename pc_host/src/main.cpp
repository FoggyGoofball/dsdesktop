/*============================================================================
 * pc_host/src/main.cpp
 *
 * DS Remote Desktop — PC Host Server entry point.
 *
 * Usage:
 *   dsrd_host --wii <ip> [--audio] [--hmac] [--fps <30|15>]
 *
 * This process:
 *   1. Captures the desktop screen continuously
 *   2. Downscales to 256×192, quantizes to 8-bit palette
 *   3. Calculates deltas (or sends keyframes) and streams via UDP
 *   4. Optionally interleaves compressed audio
 *   5. Receives DS telemetry and injects virtual OS inputs
 *   6. Dynamically throttles FPS based on DS congestion reports
 *==========================================================================*/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>

#ifdef _WIN32
#  include <winsock2.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <unistd.h>
#endif

#include "../../common/protocol.h"
#include "../include/capture.h"
#include "../include/encoder.h"
#include "../include/net_host.h"
#include "../include/throttle.h"
#include "../include/input_inject.h"
#include "../include/audio_enc.h"
#include "../include/hmac_host.h"
#include "../include/net_utils.h"

/*--------------------------------------------------------------------------
 * Command-line configuration
 *------------------------------------------------------------------------*/
struct HostConfig {
    char     wii_ip[64];
    uint16_t port;
    int      audio_enabled;
    int      hmac_enabled;
    int      initial_fps;
};

static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s --wii <ip> [--audio] [--hmac] [--fps <30|15>]\n",
        argv0);
}

static int parse_args(int argc, char **argv, HostConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->port        = DSRD_PORT;
    cfg->initial_fps = 30;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wii") == 0 && i + 1 < argc) {
            strncpy(cfg->wii_ip, argv[++i], sizeof(cfg->wii_ip) - 1);
        } else if (strcmp(argv[i], "--audio") == 0) {
            cfg->audio_enabled = 1;
        } else if (strcmp(argv[i], "--hmac") == 0) {
            cfg->hmac_enabled = 1;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            cfg->initial_fps = atoi(argv[++i]);
        } else {
            print_usage(argv[0]);
            return -1;
        }
    }

    if (cfg->wii_ip[0] == '\0') {
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}

/*--------------------------------------------------------------------------
 * MAIN
 *------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
    HostConfig cfg;
    if (parse_args(argc, argv, &cfg) < 0)
        return 1;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    /* Discover and display local IP */
    char local_ip[16] = "unknown";
    net_utils_get_local_ip(local_ip, sizeof(local_ip));

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║ DS Remote Desktop — PC Host Server                       ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("┌─ PC Configuration ─────────────────────────────────────┐\n");
    printf("│ This PC IP : %s                                        │\n", local_ip);
    printf("│ Wii IP     : %s:%d                                   │\n", cfg.wii_ip, cfg.port);
    printf("│ Audio      : %s                                        │\n", cfg.audio_enabled ? "ON " : "OFF");
    printf("│ HMAC Auth  : %s                                        │\n", cfg.hmac_enabled  ? "ON " : "OFF");
    printf("│ Initial FPS: %d                                         │\n", cfg.initial_fps);
    printf("└────────────────────────────────────────────────────────┘\n");
    printf("\n");
    printf("→ Tell Wii: pc_ip=%s in proxy.cfg\n", local_ip);
    printf("→ Or use Wii menu (SELECT) to auto-add this IP\n");
    printf("\n");

    /* ---- Init subsystems --------------------------------------------- */
    if (capture_init() < 0)      { fprintf(stderr, "capture init failed\n");  return 1; }
    if (net_host_init(cfg.wii_ip, cfg.port) < 0)
                                  { fprintf(stderr, "net init failed\n");     return 1; }
    if (input_inject_init() < 0) { fprintf(stderr, "input init failed\n");    return 1; }
    if (cfg.audio_enabled && audio_enc_init() < 0)
                                  { fprintf(stderr, "audio init failed\n");   return 1; }

    encoder_init();
    throttle_init(cfg.initial_fps);
    hmac_host_init();
    input_inject_load_remaps();

    /* ---- Buffers (statically sized) ---------------------------------- */
    static uint8_t  frame_pixels[DSRD_FRAME_PIXELS];
    static uint16_t frame_palette[DSRD_PALETTE_SIZE];
    static uint8_t  enc_buf[DSRD_FRAME_PIXELS * 2];  /* worst-case RLE */
    static uint8_t  audio_buf[DSRD_MTU];
    static uint8_t  rx_buf[DSRD_MTU + 128];

    uint16_t tx_seq = 0;

    printf("Streaming started.\n");

    /* ---- Main loop --------------------------------------------------- */
    while (g_running) {
        auto t0 = std::chrono::steady_clock::now();

        /* --- Capture & encode video ----------------------------------- */
        if (capture_frame(frame_pixels, frame_palette) == 0) {
            int enc_len = 0;
            int is_kf   = 0;
            int npkts = encoder_encode(frame_pixels, frame_palette,
                                       enc_buf, sizeof(enc_buf),
                                       &enc_len, &is_kf);
            (void)npkts;

            if (enc_len > 0) {
                net_host_send(enc_buf, enc_len);
            }
        }

        /* --- Capture & interleave audio ------------------------------- */
        if (cfg.audio_enabled) {
            int alen = audio_enc_capture(audio_buf, sizeof(audio_buf));
            if (alen > 0) {
                net_host_send(audio_buf, alen);
            }
        }

        /* --- Receive upstream telemetry / congestion ------------------- */
        int rlen = net_host_recv(rx_buf, sizeof(rx_buf));
        while (rlen > 0) {
            if (rlen >= (int)sizeof(dsrd_header_t)) {
                const dsrd_header_t *hdr = (const dsrd_header_t *)rx_buf;
                if (dsrd_header_valid(hdr)) {
                    switch (hdr->type) {
                    case PKT_TELEMETRY: {
                        /* Optionally verify HMAC */
                        if (!hmac_host_verify(rx_buf, rlen, cfg.hmac_enabled))
                            break;  /* drop silently */

                        if (hdr->payload_len >= sizeof(dsrd_telemetry_t)) {
                            const dsrd_telemetry_t *tel =
                                (const dsrd_telemetry_t *)
                                (rx_buf + sizeof(dsrd_header_t));

                            if (capture_set_magnifier(
                                    tel->magnifier_enabled ? 1 : 0,
                                    tel->magnifier_zoom,
                                    tel->magnifier_mode,
                                    tel->touch_x,
                                    tel->touch_y,
                                    tel->touch_down)) {
                                encoder_force_keyframe();
                            }

                            input_inject_process(tel);
                        }
                        break;
                    }
                    case PKT_CONGESTION: {
                        if (hdr->payload_len >= sizeof(dsrd_congestion_t)) {
                            const dsrd_congestion_t *cng =
                                (const dsrd_congestion_t *)
                                (rx_buf + sizeof(dsrd_header_t));
                            throttle_update(cng);
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
            rlen = net_host_recv(rx_buf, sizeof(rx_buf));
        }

        /* --- Frame pacing --------------------------------------------- */
        uint32_t delay_us = throttle_get_frame_delay_us();
        auto t1 = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<
            std::chrono::microseconds>(t1 - t0).count();

        if ((uint32_t)elapsed_us < delay_us) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(delay_us - elapsed_us));
        }

        tx_seq++;
    }

    /* ---- Cleanup ----------------------------------------------------- */
    printf("\nShutting down...\n");
    if (cfg.audio_enabled) audio_enc_shutdown();
    input_inject_shutdown();
    net_host_shutdown();
    capture_shutdown();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
