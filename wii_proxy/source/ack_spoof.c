/*============================================================================
 * wii_proxy/source/ack_spoof.c
 *
 * Local acknowledgment spoofing.
 *
 * The DS NiFi protocol expects a CMD/ACK handshake every 4 ms.
 * Because the PC backhaul will inevitably have latency spikes, the Wii
 * must generate fake ACK / REPLY frames locally to keep the DS's
 * hardware timers satisfied.
 *
 * This runs on a high-priority hardware timer (Timer 3) at 250 Hz
 * (4 ms interval) and immediately blasts a minimal ACK frame.
 *==========================================================================*/
#include <gccore.h>
#include <ogc/lwp.h>
#include <string.h>
#include <stdio.h>

#include "ack_spoof.h"
#include "../../common/protocol.h"

/* Low-level Wi-Fi driver hook */
extern int WL_SendRawFrame(const void *data, int len);

#define IEEE80211_HDR_LEN 24
#define ACK_FRAME_LEN     (IEEE80211_HDR_LEN + sizeof(dsrd_header_t))

static uint8_t s_ack_frame[ACK_FRAME_LEN] __attribute__((aligned(32)));
static volatile int s_running = 0;
static syswd_t s_alarm;

/*--------------------------------------------------------------------------
 * Build the ACK template once at startup
 *------------------------------------------------------------------------*/
static void build_ack_template(void)
{
    memset(s_ack_frame, 0, ACK_FRAME_LEN);

    /* 802.11 header — broadcast */
    s_ack_frame[0] = 0x08;
    s_ack_frame[1] = 0x00;
    memset(&s_ack_frame[4], 0xFF, 6);   /* dest: broadcast */
    s_ack_frame[10] = 0x02;             /* source: Wii proxy */
    s_ack_frame[11] = 0xAA;
    s_ack_frame[12] = 0xBB;
    s_ack_frame[13] = 0x00;
    s_ack_frame[14] = 0x00;
    s_ack_frame[15] = 0x01;
    memset(&s_ack_frame[16], 0xFF, 6);  /* BSSID: broadcast */

    /* DSRD ACK spoof header */
    dsrd_header_t *hdr = (dsrd_header_t *)(s_ack_frame + IEEE80211_HDR_LEN);
    dsrd_header_init(hdr, PKT_ACK_SPOOF, 0, 0, 1 /* Wii */, 0);
}

/*--------------------------------------------------------------------------
 * Timer callback — fires every 4 ms
 *------------------------------------------------------------------------*/
static void ack_timer_callback(syswd_t alarm, void *ctx)
{
    (void)alarm;
    (void)ctx;

    if (!s_running) return;

    /* Increment sequence in the pre-built frame */
    dsrd_header_t *hdr = (dsrd_header_t *)(s_ack_frame + IEEE80211_HDR_LEN);
    hdr->seq++;

    WL_SendRawFrame(s_ack_frame, ACK_FRAME_LEN);
}

/*--------------------------------------------------------------------------*/
void ack_spoof_start(void)
{
    build_ack_template();
    s_running = 1;

    /* Create a periodic alarm — 4 ms = 4,000 µs */
    struct timespec period;
    period.tv_sec  = 0;
    period.tv_nsec = DSRD_CMD_INTERVAL_MS * 1000000; /* 4 ms */

    SYS_CreateAlarm(&s_alarm);
    SYS_SetPeriodicAlarm(s_alarm, &period, &period,
                         ack_timer_callback, NULL);

    printf("ACK spoof started (every %d ms)\n", DSRD_CMD_INTERVAL_MS);
}

void ack_spoof_stop(void)
{
    s_running = 0;
    SYS_RemoveAlarm(s_alarm);
    printf("ACK spoof stopped.\n");
}
