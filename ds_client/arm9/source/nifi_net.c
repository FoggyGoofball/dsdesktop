/*============================================================================
 * ds_client/arm9/source/nifi_net.c
 *
 * NiFi networking — event-driven RX via the calico wlmgr raw-packet API,
 * zero-copy DMA ingestion with NetBuf, circular packet buffers, congestion
 * tracking, and upstream TX.
 *
 * Design mirrors the dsnifi template by jpenny1993: register a raw RX
 * callback, classify incoming frames, push them into typed circular
 * buffers, and let the main loop drain them.
 *
 * Calico API mapping:
 *   wlmgrInitDefault()         → initialise wireless manager
 *   wlmgrStart(Infrastructure) → start the radio
 *   wlmgrSetRawRxHandler()     → register RX callback
 *   wlmgrRawTx(NetBuf*)        → transmit raw frame
 *   netbufAlloc/netbufFree     → zero-copy packet management
 *   netbufGet(nb)              → direct pointer to payload (DMA-coherent)
 *   DC_FlushRange()            → ensure ARM9 cache coherency
 *==========================================================================*/
#include <nds.h>
#include <calico/nds/wlmgr.h>
#include <calico/dev/netbuf.h>
#include <string.h>
#include <stdio.h>

#include "nifi_net.h"
#include "circ_buf.h"
#include "video_decode.h"
#include "audio_stream.h"
#include "input.h"
#include "hmac_auth.h"
#include "config.h"
#include "../../../common/protocol.h"

/*--------------------------------------------------------------------------
 * Static packet buffers — no dynamic allocation
 *------------------------------------------------------------------------*/
static dsrd_circ_buf_t s_video_buf;
static dsrd_circ_buf_t s_audio_buf;

/* Scratch buffer for building TX packets */
static uint8_t s_tx_buf[DSRD_MTU] __attribute__((aligned(4)));

/* Congestion stats */
static uint16_t s_rx_drops      = 0;
static uint16_t s_congestion_timer = 0;
#define CONGESTION_REPORT_FRAMES 120  /* ~2 s at 60 VBL */

/* Upstream sequence counters */
static uint16_t s_telem_seq  = 0;
static uint16_t s_cong_seq   = 0;

/* Diagnostics ping/pong state for channel calibration */
static volatile int s_diag_pong_pending = 0;
static dsrd_diag_pong_t s_diag_pong;
static uint16_t s_diag_seq = 0;

/* Connection lifecycle state */
static dsrd_conn_state_t s_conn_state = CONN_IDLE;
static uint32_t s_session_token = 0;
static volatile int s_hs_pending = 0;      /* handshake TX needed        */
static dsrd_handshake_t s_hs_outgoing;     /* pending outgoing handshake */
static uint16_t s_hs_seq = 0;
static uint16_t s_heartbeat_timer = 0;
static uint16_t s_heartbeat_watchdog = 0;

/* Forward declaration */
static void nifi_raw_send(const uint8_t *data, uint16_t len);

/*--------------------------------------------------------------------------
 * IEEE 802.11 header length (we embed our payload after it)
 *------------------------------------------------------------------------*/
#define IEEE80211_HDR_LEN  24

/*--------------------------------------------------------------------------
 * NiFi RX callback — called from the calico wireless manager thread.
 *
 * Receives ownership of the NetBuf.  We copy the DSRD payload into our
 * circular buffer and immediately free the NetBuf to avoid exhausting
 * the RX packet heap.
 *
 * The NetBuf data pointer (netbufGet) already points past any lower-layer
 * headers that calico has stripped, so we check for our dsrd_header_t
 * directly.  If calico delivers raw 802.11 frames, we skip past the
 * IEEE 802.11 header ourselves.
 *------------------------------------------------------------------------*/
static void nifi_rx_callback(void *user, NetBuf *pPacket)
{
    (void)user;

    uint8_t *raw = (uint8_t *)netbufGet(pPacket);
    uint16_t raw_len = pPacket->len;

    /* Flush ARM9 data cache to ensure coherency with ARM7 DMA writes */
    DC_FlushRange(raw, raw_len);

    /* Determine where the DSRD header starts.
       If the frame starts with a valid IEEE 802.11 data frame control
       byte (0x08), skip past the 802.11 header.  Otherwise assume
       calico already stripped lower-layer headers. */
    const uint8_t *dsrd_start = raw;
    uint16_t dsrd_len = raw_len;

    if (raw_len > IEEE80211_HDR_LEN && (raw[0] & 0x0C) == 0x08) {
        dsrd_start = raw + IEEE80211_HDR_LEN;
        dsrd_len   = raw_len - IEEE80211_HDR_LEN;
    }

    if (dsrd_len < sizeof(dsrd_header_t)) {
        s_rx_drops++;
        netbufFree(pPacket);
        return;
    }

    const dsrd_header_t *hdr = (const dsrd_header_t *)dsrd_start;

    /* Validate magic */
    if (!dsrd_header_valid(hdr)) {
        s_rx_drops++;
        netbufFree(pPacket);
        return;
    }

    /* Drop packets that originated from ourselves (echo prevention) */
    if (hdr->client_id == g_cfg.client_id) {
        netbufFree(pPacket);
        return;
    }

    uint16_t total_len = sizeof(dsrd_header_t) + hdr->payload_len;
    if (total_len > dsrd_len) {
        s_rx_drops++;
        netbufFree(pPacket);
        return;
    }

    /* Route to the appropriate circular buffer */
    switch (hdr->type) {
    case PKT_VIDEO_KEYFRAME:
    case PKT_VIDEO_DELTA:
        circ_buf_push(&s_video_buf, dsrd_start, total_len);
        break;

    case PKT_AUDIO_CHUNK:
        if (g_cfg.audio_enabled)
            circ_buf_push(&s_audio_buf, dsrd_start, total_len);
        break;

    case PKT_CONFIG: {
        if (hdr->payload_len >= sizeof(dsrd_config_t)) {
            const dsrd_config_t *cfg =
                (const dsrd_config_t *)(dsrd_start + sizeof(dsrd_header_t));

            g_cfg.audio_enabled = cfg->audio_enabled;
            g_cfg.hmac_enabled  = cfg->hmac_enabled;

            /* Track requested channel for diagnostics/calibration.
               Note: current calico wlmgr public API doesn't expose an
               explicit fixed-channel setter for this mode. */
            if (cfg->wifi_channel >= 1 && cfg->wifi_channel <= 11)
                g_cfg.wifi_channel = cfg->wifi_channel;
        }
        break;
    }

    case PKT_DIAG_PING: {
        if (hdr->payload_len >= sizeof(dsrd_diag_ping_t)) {
            const dsrd_diag_ping_t *ping =
                (const dsrd_diag_ping_t *)(dsrd_start + sizeof(dsrd_header_t));

            s_diag_pong.token      = ping->token;
            s_diag_pong.rx_time_us = 0;
            s_diag_pong.channel    = g_cfg.wifi_channel;
            s_diag_pong._pad[0] = s_diag_pong._pad[1] =
            s_diag_pong._pad[2] = 0;
            s_diag_pong_pending = 1;
        }
        break;
    }

    case PKT_HANDSHAKE: {
        if (hdr->payload_len >= sizeof(dsrd_handshake_t)) {
            const dsrd_handshake_t *hs =
                (const dsrd_handshake_t *)(dsrd_start + sizeof(dsrd_header_t));

            if (hs->hs_type == HS_ANNOUNCE && s_conn_state != CONN_CONNECTED) {
                /* Wii is advertising — send JOIN */
                s_session_token = hs->session_token;
                s_hs_outgoing.hs_type       = HS_JOIN;
                s_hs_outgoing.client_id     = g_cfg.client_id;
                s_hs_outgoing.proto_version = DSRD_VERSION;
                s_hs_outgoing._pad          = 0;
                s_hs_outgoing.session_token = s_session_token;
                s_hs_pending = 1;
                s_conn_state = CONN_JOINING;

            } else if (hs->hs_type == HS_ACCEPT &&
                       hs->session_token == s_session_token) {
                /* Wii accepted our JOIN — we are connected */
                s_conn_state = CONN_CONNECTED;
                s_heartbeat_watchdog = 0;

            } else if (hs->hs_type == HS_HEARTBEAT &&
                       s_conn_state == CONN_CONNECTED) {
                /* Reset watchdog */
                s_heartbeat_watchdog = 0;

            } else if (hs->hs_type == HS_DISCONNECT) {
                s_conn_state = CONN_IDLE;
            }
        }
        break;
    }

    default:
        s_rx_drops++;
        break;
    }

    netbufFree(pPacket);
}

/*--------------------------------------------------------------------------
 * Initialise calico wireless manager + raw RX handler
 *------------------------------------------------------------------------*/
void dsrd_nifi_init(void)
{
    circ_buf_init(&s_video_buf);
    circ_buf_init(&s_audio_buf);

    /* Initialise the calico wireless manager with default settings */
    if (!wlmgrInitDefault()) {
        iprintf("wlmgr init FAILED\n");
        return;
    }

    /* Register our raw packet RX handler */
    wlmgrSetRawRxHandler(nifi_rx_callback, NULL);

    /* Start the wireless interface in infrastructure mode.
       Raw RX/TX still works in this mode — we receive and send
       802.11 frames via the raw handler alongside normal traffic. */
    wlmgrStart(WlMgrMode_Infrastructure);

    iprintf("NiFi radio ready (calico wlmgr)\n");
}

/*--------------------------------------------------------------------------
 * Poll — drain video & audio circular buffers in the main loop
 *------------------------------------------------------------------------*/
static uint8_t s_rx_scratch[DSRD_MTU] __attribute__((aligned(4)));

void dsrd_nifi_poll(void)
{
    /* Reply to channel-calibration ping probes (any state) */
    if (s_diag_pong_pending) {
        s_diag_pong_pending = 0;

        dsrd_header_t *hdr = (dsrd_header_t *)s_tx_buf;
        dsrd_header_init(hdr, PKT_DIAG_PONG, s_diag_seq++,
                         sizeof(dsrd_diag_pong_t), g_cfg.client_id, 0);

        dsrd_diag_pong_t *pong =
            (dsrd_diag_pong_t *)(s_tx_buf + sizeof(dsrd_header_t));
        memcpy(pong, &s_diag_pong, sizeof(dsrd_diag_pong_t));

        nifi_raw_send(s_tx_buf,
                      sizeof(dsrd_header_t) + sizeof(dsrd_diag_pong_t));
    }

    /* Send pending handshake TX (JOIN response to ANNOUNCE) */
    if (s_hs_pending) {
        s_hs_pending = 0;

        dsrd_header_t *hdr = (dsrd_header_t *)s_tx_buf;
        dsrd_header_init(hdr, PKT_HANDSHAKE, s_hs_seq++,
                         sizeof(dsrd_handshake_t), g_cfg.client_id, 0);

        dsrd_handshake_t *hs =
            (dsrd_handshake_t *)(s_tx_buf + sizeof(dsrd_header_t));
        memcpy(hs, &s_hs_outgoing, sizeof(dsrd_handshake_t));

        nifi_raw_send(s_tx_buf,
                      sizeof(dsrd_header_t) + sizeof(dsrd_handshake_t));
    }

    /* Heartbeat logic (only when connected) */
    if (s_conn_state == CONN_CONNECTED) {
        s_heartbeat_timer++;
        if (s_heartbeat_timer >= DSRD_HEARTBEAT_INTERVAL) {
            s_heartbeat_timer = 0;

            dsrd_header_t *hdr = (dsrd_header_t *)s_tx_buf;
            dsrd_header_init(hdr, PKT_HANDSHAKE, s_hs_seq++,
                             sizeof(dsrd_handshake_t), g_cfg.client_id, 0);

            dsrd_handshake_t *hs =
                (dsrd_handshake_t *)(s_tx_buf + sizeof(dsrd_header_t));
            hs->hs_type       = HS_HEARTBEAT;
            hs->client_id     = g_cfg.client_id;
            hs->proto_version = DSRD_VERSION;
            hs->_pad          = 0;
            hs->session_token = s_session_token;

            nifi_raw_send(s_tx_buf,
                          sizeof(dsrd_header_t) + sizeof(dsrd_handshake_t));
        }

        /* Heartbeat watchdog */
        s_heartbeat_watchdog++;
        if (s_heartbeat_watchdog >= DSRD_HEARTBEAT_TIMEOUT_FRAMES) {
            s_conn_state = CONN_IDLE;
            s_heartbeat_watchdog = 0;
            iprintf("Wii heartbeat lost. Waiting...\n");
        }
    }

    /* Only process video/audio when connected */
    if (s_conn_state != CONN_CONNECTED)
        return;

    /* Drain video buffer — process newest, skip stale */
    while (!circ_buf_empty(&s_video_buf)) {
        uint16_t len = circ_buf_pop(&s_video_buf, s_rx_scratch);
        if (len < sizeof(dsrd_header_t)) continue;

        const dsrd_header_t *hdr = (const dsrd_header_t *)s_rx_scratch;
        const uint8_t *payload = s_rx_scratch + sizeof(dsrd_header_t);

        if (hdr->type == PKT_VIDEO_KEYFRAME)
            dsrd_video_decode_keyframe(payload, hdr->payload_len);
        else if (hdr->type == PKT_VIDEO_DELTA)
            dsrd_video_decode_delta(payload, hdr->payload_len);
    }

    /* Drain audio buffer */
    if (g_cfg.audio_enabled) {
        while (!circ_buf_empty(&s_audio_buf)) {
            uint16_t len = circ_buf_pop(&s_audio_buf, s_rx_scratch);
            if (len < sizeof(dsrd_header_t)) continue;

            const dsrd_header_t *hdr = (const dsrd_header_t *)s_rx_scratch;
            const uint8_t *payload = s_rx_scratch + sizeof(dsrd_header_t);

            dsrd_audio_push_chunk(payload, hdr->payload_len);
        }
    }
}

/*--------------------------------------------------------------------------
 * Transmit a raw frame via calico wlmgrRawTx
 *------------------------------------------------------------------------*/
static void nifi_raw_send(const uint8_t *data, uint16_t len)
{
    if (len > DSRD_MTU) len = DSRD_MTU;

    /* Allocate a NetBuf from the TX pool with room for 802.11 header */
    NetBuf *nb = netbufAlloc(IEEE80211_HDR_LEN, len, NetBufPool_Tx);
    if (!nb) return;

    /* Copy our DSRD payload into the NetBuf data area */
    uint8_t *dst = (uint8_t *)netbufGet(nb);
    memcpy(dst, data, len);

    /* Prepend a minimal 802.11 broadcast data-frame header */
    uint8_t *hdr80211 = (uint8_t *)netbufPushHeader(nb, IEEE80211_HDR_LEN);
    if (!hdr80211) {
        netbufFree(nb);
        return;
    }

    memset(hdr80211, 0, IEEE80211_HDR_LEN);
    /* Frame Control: Data frame, ToDS=0, FromDS=0 */
    hdr80211[0] = 0x08;
    hdr80211[1] = 0x00;
    /* Destination: broadcast */
    memset(&hdr80211[4], 0xFF, 6);
    /* Source: locally-administered MAC with our client_id */
    hdr80211[10] = 0x02; hdr80211[11] = 0xD5; hdr80211[12] = 0xED;
    hdr80211[13] = 0x00; hdr80211[14] = 0x00; hdr80211[15] = g_cfg.client_id;
    /* BSSID: broadcast */
    memset(&hdr80211[16], 0xFF, 6);

    /* Flush cache before hardware DMA picks it up */
    netbufFlush(nb);

    /* Transmit — calico takes ownership of the NetBuf */
    wlmgrRawTx(nb);
}

void dsrd_nifi_send_telemetry(void)
{
    /* Only send telemetry when connected */
    if (s_conn_state != CONN_CONNECTED)
        return;

    dsrd_header_t *hdr = (dsrd_header_t *)s_tx_buf;
    uint8_t flags = 0;
    uint16_t payload_len = sizeof(dsrd_telemetry_t);

    if (g_cfg.hmac_enabled)
        flags |= DSRD_FLAG_HMAC;

    dsrd_header_init(hdr, PKT_TELEMETRY, s_telem_seq++,
                     payload_len, g_cfg.client_id, flags);

    dsrd_telemetry_t *tel =
        (dsrd_telemetry_t *)(s_tx_buf + sizeof(dsrd_header_t));
    dsrd_input_fill_telemetry(tel);

    uint16_t total = sizeof(dsrd_header_t) + payload_len;

    /* Optionally append HMAC trailer */
    if (g_cfg.hmac_enabled) {
        dsrd_hmac_trailer_t *trailer =
            (dsrd_hmac_trailer_t *)(s_tx_buf + total);
        dsrd_hmac_sign(s_tx_buf, total, trailer);
        total += sizeof(dsrd_hmac_trailer_t);
    }

    nifi_raw_send(s_tx_buf, total);
}

/*--------------------------------------------------------------------------
 * Congestion report — sent every CONGESTION_REPORT_FRAMES VBlanks
 *------------------------------------------------------------------------*/
void dsrd_nifi_send_congestion(void)
{
    /* Only send congestion reports when connected */
    if (s_conn_state != CONN_CONNECTED)
        return;

    s_congestion_timer++;
    if (s_congestion_timer < CONGESTION_REPORT_FRAMES)
        return;
    s_congestion_timer = 0;

    dsrd_header_t *hdr = (dsrd_header_t *)s_tx_buf;
    dsrd_header_init(hdr, PKT_CONGESTION, s_cong_seq++,
                     sizeof(dsrd_congestion_t), g_cfg.client_id, 0);

    dsrd_congestion_t *cong =
        (dsrd_congestion_t *)(s_tx_buf + sizeof(dsrd_header_t));
    cong->rx_overflows = circ_buf_drain_overflows(&s_video_buf)
                       + circ_buf_drain_overflows(&s_audio_buf);
    cong->rx_drops     = s_rx_drops;
    s_rx_drops = 0;
    cong->avg_rtt_us   = 0;  /* reserved until RTT measurement is wired in */
    cong->_pad         = 0;

    uint16_t total = sizeof(dsrd_header_t) + sizeof(dsrd_congestion_t);
    nifi_raw_send(s_tx_buf, total);
}
