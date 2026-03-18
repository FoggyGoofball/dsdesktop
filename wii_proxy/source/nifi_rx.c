/*============================================================================
 * wii_proxy/source/nifi_rx.c
 *
 * NiFi receive — captures raw 802.11b frames from the DS in promiscuous
 * mode, strips the 802.11 header, validates the DSRD header, and makes
 * the payload available to the main loop for upstream forwarding to the PC.
 *
 * Uses a small circular buffer so the main loop can drain at its own pace
 * without losing packets to timing jitter.
 *==========================================================================*/
#include <gccore.h>
#include <string.h>
#include <stdio.h>

#include "nifi_rx.h"
#include "../../common/protocol.h"

/* Low-level Wi-Fi driver hooks */
extern int WL_RecvRawFrame(void *buf, int max_len);  /* non-blocking */

#define IEEE80211_HDR_LEN 24

/*--------------------------------------------------------------------------
 * Receive circular buffer (statically allocated)
 *------------------------------------------------------------------------*/
#define RX_RING_SLOTS 8

static uint8_t  s_ring_data[RX_RING_SLOTS][DSRD_MTU];
static uint16_t s_ring_lens[RX_RING_SLOTS];
static volatile int s_ring_head = 0;
static volatile int s_ring_tail = 0;

static uint8_t s_raw_frame[IEEE80211_HDR_LEN + DSRD_MTU + 32]
    __attribute__((aligned(32)));

/*--------------------------------------------------------------------------*/
void nifi_rx_init(const wii_config_t *cfg)
{
    (void)cfg;
    memset(s_ring_data, 0, sizeof(s_ring_data));
    memset(s_ring_lens, 0, sizeof(s_ring_lens));
    s_ring_head = 0;
    s_ring_tail = 0;
}

/*--------------------------------------------------------------------------
 * Poll raw radio for an incoming NiFi frame and push into ring
 *------------------------------------------------------------------------*/
static void nifi_rx_poll(void)
{
    int n = WL_RecvRawFrame(s_raw_frame, sizeof(s_raw_frame));
    if (n <= IEEE80211_HDR_LEN)
        return;

    /* Strip 802.11 header */
    const uint8_t *payload = s_raw_frame + IEEE80211_HDR_LEN;
    int payload_len = n - IEEE80211_HDR_LEN;

    if (payload_len < (int)sizeof(dsrd_header_t))
        return;

    const dsrd_header_t *hdr = (const dsrd_header_t *)payload;
    if (!dsrd_header_valid(hdr))
        return;

    /* Drop echo — don't process packets from the Wii itself */
    if (hdr->client_id == 1)  /* Wii = ID 1 */
        return;

    /* Push into ring (overwrite oldest if full) */
    int next = (s_ring_head + 1) % RX_RING_SLOTS;
    if (next == s_ring_tail) {
        /* Ring full — advance tail (drop oldest) */
        s_ring_tail = (s_ring_tail + 1) % RX_RING_SLOTS;
    }

    uint16_t copy_len = (payload_len > DSRD_MTU) ? DSRD_MTU : payload_len;
    memcpy(s_ring_data[s_ring_head], payload, copy_len);
    s_ring_lens[s_ring_head] = copy_len;
    s_ring_head = next;
}

/*--------------------------------------------------------------------------
 * Non-blocking receive — returns extracted DSRD payload or 0
 *------------------------------------------------------------------------*/
int nifi_rx_recv(uint8_t *buf, int max_len)
{
    /* First poll the radio */
    nifi_rx_poll();

    /* Drain one item from ring */
    if (s_ring_head == s_ring_tail)
        return 0;

    uint16_t len = s_ring_lens[s_ring_tail];
    if (len > max_len) len = (uint16_t)max_len;

    memcpy(buf, s_ring_data[s_ring_tail], len);
    s_ring_tail = (s_ring_tail + 1) % RX_RING_SLOTS;
    return len;
}
