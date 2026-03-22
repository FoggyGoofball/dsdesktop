/*============================================================================
 * ds_client/arm9/source/nifi_net.c
 *
 * NiFi networking - BlocksDS dswifi raw 802.11 local wireless.
 *
 * Uses Wifi_InitDefault(INIT_ONLY | WIFI_LOCAL_ONLY) for local-only
 * wireless (skips lwIP allocation), Wifi_RawSetPacketHandler() for RX,
 * and Wifi_RawTxFrame() for TX.
 *
 * Requires the ARM7 to run installWifiFIFO() + Wifi_Update() from
 * dswifi7 so that ARM9 FIFO commands reach the Wi-Fi hardware.
 *
 * Architecture:
 *   DS  <-- NiFi raw 802.11 -->  Wii proxy  <-- UDP -->  PC host
 *==========================================================================*/
#include <nds.h>
#include <dswifi9.h>
#include <dswifi_common.h>
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

#define IEEE80211_HDR_LEN       24
#define NIFI_CHANNEL            1
#define TX_RATE                 WIFI_TRANSFER_RATE_2MBPS

static dsrd_circ_buf_t s_video_buf;
static dsrd_circ_buf_t s_audio_buf;

static uint8_t s_tx_buf[IEEE80211_HDR_LEN + DSRD_MTU]
    __attribute__((aligned(4)));
static uint8_t s_rx_scratch[DSRD_MTU] __attribute__((aligned(4)));

static uint16_t s_rx_drops = 0;
static uint16_t s_congestion_timer = 0;
#define CONGESTION_REPORT_FRAMES 120

static uint16_t s_telem_seq = 0;
static uint16_t s_cong_seq = 0;
static volatile int s_diag_pong_pending = 0;
static dsrd_diag_pong_t s_diag_pong;
static uint16_t s_diag_seq = 0;

static dsrd_conn_state_t s_conn_state = CONN_IDLE;
static uint32_t s_session_token = 0;
static volatile int s_hs_pending = 0;
static dsrd_handshake_t s_hs_outgoing;
static uint16_t s_hs_seq = 0;
static uint16_t s_heartbeat_timer = 0;
static uint16_t s_heartbeat_watchdog = 0;
static uint16_t s_join_probe_timer = 0;
static uint32_t s_join_probe_token = 0x44530000u;
static uint32_t s_join_tx_count = 0;
static uint32_t s_handshake_rx_count = 0;
#define JOIN_PROBE_INTERVAL_FRAMES 60
#define JOIN_PROBE_BACKOFF_FRAMES 180

static uint32_t s_tx_frames = 0;
static uint32_t s_tx_drop_no_buf = 0;
static uint32_t s_tx_drop_not_ready = 0;
static uint16_t s_last_tx_len = 0;
static uint32_t s_last_tx_drop_seen = 0;
static int s_wifi_ready = 0;
static const char *s_wifi_state_str = "WIFI_INIT";

static void nifi_raw_send(const uint8_t *data, uint16_t len);

static void nifi_rx_handler(int pkt_addr, int pkt_len)
{
    if (pkt_len < IEEE80211_HDR_LEN + (int)sizeof(dsrd_header_t)) {
        s_rx_drops++;
        return;
    }

    uint8_t frame[IEEE80211_HDR_LEN + DSRD_MTU];
    int copy_len = pkt_len;
    if (copy_len > (int)sizeof(frame))
        copy_len = (int)sizeof(frame);

    Wifi_RxRawReadPacket((u32)pkt_addr, (u32)copy_len, frame);

    const uint8_t *dsrd_start = frame + IEEE80211_HDR_LEN;
    int dsrd_len = copy_len - IEEE80211_HDR_LEN;

    if (dsrd_len < (int)sizeof(dsrd_header_t)) { s_rx_drops++; return; }

    const dsrd_header_t *hdr = (const dsrd_header_t *)dsrd_start;
    if (!dsrd_header_valid(hdr)) { s_rx_drops++; return; }
    if (hdr->client_id == g_cfg.client_id) return;

    uint16_t total_len = sizeof(dsrd_header_t) + hdr->payload_len;
    if (total_len > (uint16_t)dsrd_len) { s_rx_drops++; return; }

    switch (hdr->type) {
    case PKT_VIDEO_KEYFRAME:
    case PKT_VIDEO_DELTA:
        circ_buf_push(&s_video_buf, dsrd_start, total_len);
        break;
    case PKT_AUDIO_CHUNK:
        if (g_cfg.audio_enabled)
            circ_buf_push(&s_audio_buf, dsrd_start, total_len);
        break;
    case PKT_CONFIG:
        if (hdr->payload_len >= sizeof(dsrd_config_t)) {
            const dsrd_config_t *cfg =
                (const dsrd_config_t *)(dsrd_start + sizeof(dsrd_header_t));
            g_cfg.audio_enabled = cfg->audio_enabled;
            g_cfg.hmac_enabled = cfg->hmac_enabled;
        }
        break;
    case PKT_DIAG_PING:
        if (hdr->payload_len >= sizeof(dsrd_diag_ping_t)) {
            const dsrd_diag_ping_t *ping =
                (const dsrd_diag_ping_t *)(dsrd_start + sizeof(dsrd_header_t));
            s_diag_pong.token = ping->token;
            s_diag_pong.rx_time_us = 0;
            s_diag_pong.channel = g_cfg.wifi_channel;
            s_diag_pong._pad[0] = s_diag_pong._pad[1] =
                s_diag_pong._pad[2] = 0;
            s_diag_pong_pending = 1;
        }
        break;
    case PKT_HANDSHAKE:
        if (hdr->payload_len >= sizeof(dsrd_handshake_t)) {
            const dsrd_handshake_t *hs =
                (const dsrd_handshake_t *)(dsrd_start + sizeof(dsrd_header_t));
            s_handshake_rx_count++;
            if (hs->hs_type == HS_ANNOUNCE && s_conn_state != CONN_CONNECTED) {
                s_session_token = hs->session_token;
                s_hs_outgoing.hs_type = HS_JOIN;
                s_hs_outgoing.client_id = g_cfg.client_id;
                s_hs_outgoing.proto_version = DSRD_VERSION;
                s_hs_outgoing._pad = 0;
                s_hs_outgoing.session_token = s_session_token;
                s_hs_pending = 1;
                s_conn_state = CONN_JOINING;
            } else if (hs->hs_type == HS_ACCEPT &&
                       hs->session_token == s_session_token) {
                s_conn_state = CONN_CONNECTED;
                s_heartbeat_watchdog = 0;
            } else if (hs->hs_type == HS_HEARTBEAT &&
                       s_conn_state == CONN_CONNECTED) {
                s_heartbeat_watchdog = 0;
            } else if (hs->hs_type == HS_DISCONNECT) {
                s_conn_state = CONN_IDLE;
            }
        }
        break;
    default:
        s_rx_drops++;
        break;
    }
}

void dsrd_nifi_init(void)
{
    circ_buf_init(&s_video_buf);
    circ_buf_init(&s_audio_buf);
    g_cfg.wifi_channel = NIFI_CHANNEL;
    s_conn_state = CONN_IDLE;
    s_hs_pending = 0;
    s_join_probe_timer = 0;
    s_heartbeat_timer = 0;
    s_heartbeat_watchdog = 0;
    s_tx_frames = 0;
    s_tx_drop_no_buf = 0;
    s_tx_drop_not_ready = 0;
    s_last_tx_len = 0;
    s_last_tx_drop_seen = 0;
    s_wifi_ready = 0;

    printf("WiFi: BlocksDS local init\n");

    if (!Wifi_InitDefault(INIT_ONLY | WIFI_LOCAL_ONLY)) {
        printf("WiFi init FAILED\n");
        s_wifi_state_str = "WIFI_FAIL";
        return;
    }

    printf("WiFi: hw ok\n");
    Wifi_SetChannel(NIFI_CHANNEL);
    Wifi_SetPromiscuousMode(1);
    Wifi_RawSetPacketHandler(nifi_rx_handler);

    s_wifi_ready = 1;
    s_wifi_state_str = "WIFI_READY";

    memset(s_tx_buf, 0, IEEE80211_HDR_LEN);
    s_tx_buf[0] = 0x08;
    s_tx_buf[1] = 0x00;
    memset(&s_tx_buf[4], 0xFF, 6);
    s_tx_buf[10] = 0x02; s_tx_buf[11] = 0xD5; s_tx_buf[12] = 0xED;
    s_tx_buf[13] = 0x00; s_tx_buf[14] = 0x00; s_tx_buf[15] = g_cfg.client_id;
    memset(&s_tx_buf[16], 0xFF, 6);

    printf("NiFi: ch%d promisc raw\n", NIFI_CHANNEL);

    s_join_probe_token++;
    s_session_token = s_join_probe_token;
    s_hs_outgoing.hs_type = HS_JOIN;
    s_hs_outgoing.client_id = g_cfg.client_id;
    s_hs_outgoing.proto_version = DSRD_VERSION;
    s_hs_outgoing._pad = 0;
    s_hs_outgoing.session_token = s_session_token;
    s_hs_pending = 1;
    s_conn_state = CONN_JOINING;
}

static void nifi_raw_send(const uint8_t *data, uint16_t len)
{
    if (!s_wifi_ready) { s_tx_drop_not_ready++; return; }
    if (len > DSRD_MTU) len = DSRD_MTU;

    memcpy(s_tx_buf + IEEE80211_HDR_LEN, data, len);
    uint16_t total = IEEE80211_HDR_LEN + len;

    int ret = Wifi_RawTxFrame(total, TX_RATE, s_tx_buf);
    if (ret != 0) { s_tx_drop_no_buf++; return; }

    s_tx_frames++;
    s_last_tx_len = len;
}

void dsrd_nifi_poll(void)
{
    if (s_diag_pong_pending) {
        s_diag_pong_pending = 0;
        uint8_t pkt[sizeof(dsrd_header_t) + sizeof(dsrd_diag_pong_t)];
        dsrd_header_init((dsrd_header_t *)pkt, PKT_DIAG_PONG, s_diag_seq++,
                         sizeof(dsrd_diag_pong_t), g_cfg.client_id, 0);
        memcpy(pkt + sizeof(dsrd_header_t), &s_diag_pong,
               sizeof(dsrd_diag_pong_t));
        nifi_raw_send(pkt, sizeof(pkt));
    }

    if (s_conn_state != CONN_CONNECTED) {
        if (s_tx_drop_no_buf != s_last_tx_drop_seen) {
            s_last_tx_drop_seen = s_tx_drop_no_buf;
            s_join_probe_timer = 0;
        }
        s_join_probe_timer++;
        uint16_t interval = (s_tx_drop_no_buf > 0)
            ? JOIN_PROBE_BACKOFF_FRAMES : JOIN_PROBE_INTERVAL_FRAMES;
        if (s_join_probe_timer >= interval) {
            s_join_probe_timer = 0;
            s_join_probe_token++;
            s_session_token = s_join_probe_token;
            s_hs_outgoing.hs_type = HS_JOIN;
            s_hs_outgoing.client_id = g_cfg.client_id;
            s_hs_outgoing.proto_version = DSRD_VERSION;
            s_hs_outgoing._pad = 0;
            s_hs_outgoing.session_token = s_join_probe_token;
            s_hs_pending = 1;
            s_conn_state = CONN_JOINING;
        }
    }

    if (s_hs_pending) {
        s_hs_pending = 0;
        uint8_t pkt[sizeof(dsrd_header_t) + sizeof(dsrd_handshake_t)];
        dsrd_header_init((dsrd_header_t *)pkt, PKT_HANDSHAKE, s_hs_seq++,
                         sizeof(dsrd_handshake_t), g_cfg.client_id, 0);
        memcpy(pkt + sizeof(dsrd_header_t), &s_hs_outgoing,
               sizeof(dsrd_handshake_t));
        if (s_hs_outgoing.hs_type == HS_JOIN)
            s_join_tx_count++;
        nifi_raw_send(pkt, sizeof(pkt));
    }

    if (s_conn_state == CONN_CONNECTED) {
        s_heartbeat_timer++;
        if (s_heartbeat_timer >= DSRD_HEARTBEAT_INTERVAL) {
            s_heartbeat_timer = 0;
            uint8_t pkt[sizeof(dsrd_header_t) + sizeof(dsrd_handshake_t)];
            dsrd_header_init((dsrd_header_t *)pkt, PKT_HANDSHAKE, s_hs_seq++,
                             sizeof(dsrd_handshake_t), g_cfg.client_id, 0);
            dsrd_handshake_t *hs =
                (dsrd_handshake_t *)(pkt + sizeof(dsrd_header_t));
            hs->hs_type = HS_HEARTBEAT;
            hs->client_id = g_cfg.client_id;
            hs->proto_version = DSRD_VERSION;
            hs->_pad = 0;
            hs->session_token = s_session_token;
            nifi_raw_send(pkt, sizeof(pkt));
        }
        s_heartbeat_watchdog++;
        if (s_heartbeat_watchdog >= DSRD_HEARTBEAT_TIMEOUT_FRAMES) {
            s_conn_state = CONN_IDLE;
            s_heartbeat_watchdog = 0;
            printf("Wii heartbeat lost.\n");
        }
    }

    if (s_conn_state != CONN_CONNECTED) return;

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

void dsrd_nifi_send_telemetry(void)
{
    if (s_conn_state != CONN_CONNECTED) return;

    uint8_t pkt[sizeof(dsrd_header_t) + sizeof(dsrd_telemetry_t) +
                sizeof(dsrd_hmac_trailer_t)];
    uint8_t flags = 0;
    uint16_t payload_len = sizeof(dsrd_telemetry_t);
    if (g_cfg.hmac_enabled) flags |= DSRD_FLAG_HMAC;

    dsrd_header_init((dsrd_header_t *)pkt, PKT_TELEMETRY, s_telem_seq++,
                     payload_len, g_cfg.client_id, flags);
    dsrd_input_fill_telemetry(
        (dsrd_telemetry_t *)(pkt + sizeof(dsrd_header_t)));

    uint16_t total = sizeof(dsrd_header_t) + payload_len;
    if (g_cfg.hmac_enabled) {
        dsrd_hmac_sign(pkt, total, (dsrd_hmac_trailer_t *)(pkt + total));
        total += sizeof(dsrd_hmac_trailer_t);
    }
    nifi_raw_send(pkt, total);
}

void dsrd_nifi_send_congestion(void)
{
    if (s_conn_state != CONN_CONNECTED) return;
    s_congestion_timer++;
    if (s_congestion_timer < CONGESTION_REPORT_FRAMES) return;
    s_congestion_timer = 0;

    uint8_t pkt[sizeof(dsrd_header_t) + sizeof(dsrd_congestion_t)];
    dsrd_header_init((dsrd_header_t *)pkt, PKT_CONGESTION, s_cong_seq++,
                     sizeof(dsrd_congestion_t), g_cfg.client_id, 0);
    dsrd_congestion_t *cong =
        (dsrd_congestion_t *)(pkt + sizeof(dsrd_header_t));
    cong->rx_overflows = circ_buf_drain_overflows(&s_video_buf)
                       + circ_buf_drain_overflows(&s_audio_buf);
    cong->rx_drops = s_rx_drops;
    s_rx_drops = 0;
    cong->avg_rtt_us = 0;
    cong->_pad = 0;
    nifi_raw_send(pkt, sizeof(pkt));
}

const char *dsrd_nifi_state_name(void)
{
    switch (s_conn_state) {
    case CONN_IDLE:       return "IDLE";
    case CONN_ANNOUNCING: return "ANNOUNCING";
    case CONN_JOINING:    return "JOINING";
    case CONN_CONNECTED:  return "CONNECTED";
    case CONN_LOST:       return "LOST";
    default:              return "UNKNOWN";
    }
}

uint32_t dsrd_nifi_join_tx_count(void)      { return s_join_tx_count; }
uint32_t dsrd_nifi_handshake_rx_count(void) { return s_handshake_rx_count; }
uint32_t dsrd_nifi_tx_frame_count(void)     { return s_tx_frames; }
uint32_t dsrd_nifi_tx_drop_count(void)      { return s_tx_drop_no_buf; }
uint16_t dsrd_nifi_last_tx_len(void)        { return s_last_tx_len; }
uint32_t dsrd_nifi_tx_suppressed_count(void){ return s_tx_drop_not_ready; }
uint32_t dsrd_nifi_wl_cmd_failed_count(void){ return 0; }
const char *dsrd_nifi_wl_state_name(void)   { return s_wifi_state_str; }

