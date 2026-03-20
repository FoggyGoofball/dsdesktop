/*============================================================================
 * common/protocol.h
 *
 * Shared binary protocol definitions for the DS Remote Desktop stack.
 * Included by all three nodes (DS, Wii, PC).
 *==========================================================================*/
#ifndef DSRD_PROTOCOL_H
#define DSRD_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------
 * Magic & version
 *------------------------------------------------------------------------*/
#define DSRD_MAGIC          0x44535244u   /* "DSRD" */
#define DSRD_VERSION        1

/*--------------------------------------------------------------------------
 * Screen geometry
 *------------------------------------------------------------------------*/
#define DSRD_SCREEN_W       256
#define DSRD_SCREEN_H       192
#define DSRD_PALETTE_SIZE   256
#define DSRD_FRAME_PIXELS   (DSRD_SCREEN_W * DSRD_SCREEN_H)

/*--------------------------------------------------------------------------
 * Network tuning
 *------------------------------------------------------------------------*/
#define DSRD_MTU            1400         /* safe NiFi payload size        */
#define DSRD_PORT           17394        /* UDP port PC<->Wii             */
#define DSRD_CIRC_BUF_SLOTS 8           /* circular packet buffer depth  */
#define DSRD_CMD_INTERVAL_MS 4          /* NiFi CMD frame interval       */
#define DSRD_KEYFRAME_EVERY 60          /* frames between full keyframes */
#define DSRD_DELTA_FALLBACK_PCT 60      /* % changed → send full frame   */

/*--------------------------------------------------------------------------
 * Packet types  (fits in uint8_t)
 *------------------------------------------------------------------------*/
typedef enum {
    PKT_VIDEO_KEYFRAME  = 0x01,  /* full 8-bit paletted frame + palette  */
    PKT_VIDEO_DELTA     = 0x02,  /* list of changed pixels               */
    PKT_AUDIO_CHUNK     = 0x03,  /* ADPCM / 8-bit PCM audio chunk        */
    PKT_TELEMETRY       = 0x10,  /* DS → PC  controller + touch + keys   */
    PKT_CONGESTION      = 0x11,  /* DS → PC  buffer overflow counters    */
    PKT_CONFIG          = 0x20,  /* bidirectional runtime config toggle  */
    PKT_ACK_SPOOF       = 0x30,  /* Wii-local spoofed ACK (never on wire)*/
    PKT_DIAG_PING       = 0x40,  /* channel-calibration probe            */
    PKT_DIAG_PONG       = 0x41,  /* channel-calibration probe response   */
    PKT_HANDSHAKE       = 0x50,  /* connection lifecycle handshake       */
} dsrd_pkt_type_t;

/*--------------------------------------------------------------------------
 * Common packet header  (12 bytes, packed)
 *------------------------------------------------------------------------*/
#pragma pack(push,1)

typedef struct {
    uint32_t magic;              /* DSRD_MAGIC                           */
    uint8_t  version;            /* DSRD_VERSION                         */
    uint8_t  type;               /* dsrd_pkt_type_t                      */
    uint16_t seq;                /* per-type monotonic sequence number   */
    uint16_t payload_len;        /* bytes following this header          */
    uint8_t  client_id;          /* origin node ID  (0=PC,1=Wii,2..=DS) */
    uint8_t  flags;              /* bit 0 = HMAC present, bit 1 = audio  */
} dsrd_header_t;

/*--------------------------------------------------------------------------
 * Flag bits
 *------------------------------------------------------------------------*/
#define DSRD_FLAG_HMAC      (1u << 0)
#define DSRD_FLAG_AUDIO     (1u << 1)

/*--------------------------------------------------------------------------
 * Video: keyframe payload
 *   [palette: 256 × 2 bytes RGB555]  [pixels: 49152 bytes, RLE-compressed]
 *------------------------------------------------------------------------*/
#define DSRD_PALETTE_BYTES  (DSRD_PALETTE_SIZE * 2)

/*--------------------------------------------------------------------------
 * Video: delta payload
 *   uint16_t  count;           // number of changed pixels
 *   struct { uint8_t x; uint8_t y; uint8_t color_idx; } deltas[count];
 *------------------------------------------------------------------------*/
typedef struct {
    uint8_t  x;
    uint8_t  y;
    uint8_t  color_idx;
} dsrd_delta_pixel_t;

/*--------------------------------------------------------------------------
 * Audio chunk payload
 *   uint16_t sample_count;
 *   uint8_t  format;           // 0 = 8-bit PCM, 1 = IMA-ADPCM
 *   uint8_t  data[];           // variable length
 *------------------------------------------------------------------------*/
#define DSRD_AUDIO_FMT_PCM8    0
#define DSRD_AUDIO_FMT_ADPCM   1

typedef struct {
    uint16_t sample_count;
    uint8_t  format;
    /* uint8_t data[] follows */
} dsrd_audio_hdr_t;

/*--------------------------------------------------------------------------
 * Telemetry payload  (DS → PC)
 *------------------------------------------------------------------------*/
typedef struct {
    uint32_t buttons_held;       /* libnds keysHeld() bitmask            */
    uint16_t touch_x;
    uint16_t touch_y;
    uint8_t  touch_down;         /* 1 = stylus touching                  */
    uint8_t  kbd_scancode;       /* on-screen keyboard output, 0 = none  */
    uint8_t  remap_id;           /* primary mapped virtual output id     */
    uint8_t  _pad;               /* secondary virtual output id (or 0)   */
    uint8_t  magnifier_enabled;  /* host-side magnifier toggle           */
    uint8_t  magnifier_zoom;     /* host-side magnifier zoom level       */
    uint8_t  magnifier_mode;     /* 0=cursor follow, 1=stylus pan        */
    uint8_t  _pad2;
} dsrd_telemetry_t;

/*--------------------------------------------------------------------------
 * Congestion report  (DS → PC)
 *------------------------------------------------------------------------*/
typedef struct {
    uint16_t rx_overflows;       /* circular-buffer overwrite count      */
    uint16_t rx_drops;           /* packets dropped (bad CRC / origin)   */
    uint16_t avg_rtt_us;         /* average measured round-trip µs       */
    uint16_t _pad;
} dsrd_congestion_t;

/*--------------------------------------------------------------------------
 * Config toggle  (bidirectional)
 *------------------------------------------------------------------------*/
typedef struct {
    uint8_t  audio_enabled;      /* 0/1                                  */
    uint8_t  hmac_enabled;       /* 0/1                                  */
    uint8_t  target_fps;         /* requested runtime FPS, clamped <= 30 */
    uint8_t  wifi_channel;       /* 1..11 (requested)                    */
    uint8_t  apply_now;          /* 1 = apply config immediately         */
    uint8_t  _pad[3];
} dsrd_config_t;

/*--------------------------------------------------------------------------
 * HMAC trailer  (appended after payload when DSRD_FLAG_HMAC is set)
 *   uint32_t  hmac_seq;        // anti-replay monotonic counter
 *   uint8_t   hmac[32];        // HMAC-SHA256 over header+payload+seq
 *------------------------------------------------------------------------*/
#define DSRD_HMAC_LEN 32

typedef struct {
    uint32_t hmac_seq;
    uint8_t  hmac[DSRD_HMAC_LEN];
} dsrd_hmac_trailer_t;

/* Pre-shared key — in production, load from config / NVS */
#define DSRD_PSK  "DS_Remote_Desktop_PSK_2025!changeme"
#define DSRD_PSK_LEN  (sizeof(DSRD_PSK) - 1)

/*--------------------------------------------------------------------------
 * Per-button remap descriptor
 *------------------------------------------------------------------------*/
#define DSRD_MAX_REMAPS  16

typedef struct {
    uint32_t ds_mask;            /* single DS hardware button bitmask     */
    uint8_t  virtual_output;     /* primary virtual key / button id       */
    uint8_t  _pad[3];
} dsrd_remap_entry_t;

/*--------------------------------------------------------------------------
 * Diagnostics ping/pong payloads
 *------------------------------------------------------------------------*/
typedef struct {
    uint32_t token;              /* probe correlation token               */
    uint32_t tx_time_us;         /* sender timestamp (for diagnostics)    */
    uint8_t  channel;            /* channel currently under test          */
    uint8_t  _pad[3];
} dsrd_diag_ping_t;

typedef struct {
    uint32_t token;              /* must match incoming ping token        */
    uint32_t rx_time_us;         /* responder local timestamp             */
    uint8_t  channel;            /* responder observed channel            */
    uint8_t  _pad[3];
} dsrd_diag_pong_t;

/*--------------------------------------------------------------------------
 * Connection lifecycle handshake
 *
 * State machine:
 *   IDLE --> Wii sends ANNOUNCE (periodically while no DS connected)
 *   DS receives ANNOUNCE --> DS sends JOIN
 *   Wii receives JOIN --> Wii sends ACCEPT, enters CONNECTED
 *   DS receives ACCEPT --> DS enters CONNECTED, starts streaming
 *   Either side can send HEARTBEAT to maintain connection
 *   If no heartbeat/telemetry activity is received for the timeout window,
 *   the connection is considered lost and returns to IDLE.
 *   (DS uses frame timeout, Wii uses wall-clock milliseconds.)
 *------------------------------------------------------------------------*/
typedef enum {
    CONN_IDLE       = 0,   /* no DS connected                           */
    CONN_ANNOUNCING = 1,   /* Wii periodically broadcasting ANNOUNCE    */
    CONN_JOINING    = 2,   /* DS sent JOIN, waiting for ACCEPT          */
    CONN_CONNECTED  = 3,   /* bidirectional stream active               */
    CONN_LOST       = 4,   /* heartbeat timeout, attempting recovery    */
} dsrd_conn_state_t;

typedef enum {
    HS_ANNOUNCE     = 0,   /* Wii → DS: "I am here, connect to me"     */
    HS_JOIN         = 1,   /* DS → Wii: "I want to connect"            */
    HS_ACCEPT       = 2,   /* Wii → DS: "Connection accepted"          */
    HS_HEARTBEAT    = 3,   /* bidirectional: "I am still alive"        */
    HS_DISCONNECT   = 4,   /* either: "I am disconnecting"             */
} dsrd_hs_type_t;

/* Heartbeat timing:
 * - DS side polls in VBlank cadence, so timeout is also exposed in frames.
 * - Wii side uses wall-clock timeout in milliseconds. */
#define DSRD_HEARTBEAT_INTERVAL       30      /* VBlanks between heartbeats     */
#define DSRD_HEARTBEAT_TIMEOUT_FRAMES 1800    /* 30s @ 60 VBlank/s (DS side)    */
#define DSRD_HEARTBEAT_TIMEOUT_MS     30000   /* 30 seconds (Wii side)          */
#define DSRD_ANNOUNCE_INTERVAL        60      /* VBlanks between ANNOUNCE bursts */

typedef struct {
    uint8_t  hs_type;        /* dsrd_hs_type_t                          */
    uint8_t  client_id;      /* node originating the handshake          */
    uint8_t  proto_version;  /* DSRD_VERSION                            */
    uint8_t  _pad;
    uint32_t session_token;  /* random token; must match on ACCEPT      */
} dsrd_handshake_t;

#pragma pack(pop)

/*--------------------------------------------------------------------------
 * Inline helpers
 *------------------------------------------------------------------------*/
static inline void dsrd_header_init(dsrd_header_t *h, uint8_t type,
                                    uint16_t seq, uint16_t len,
                                    uint8_t cid, uint8_t flags)
{
    h->magic       = DSRD_MAGIC;
    h->version     = DSRD_VERSION;
    h->type        = type;
    h->seq         = seq;
    h->payload_len = len;
    h->client_id   = cid;
    h->flags       = flags;
}

static inline int dsrd_header_valid(const dsrd_header_t *h)
{
    return h->magic == DSRD_MAGIC && h->version == DSRD_VERSION;
}

#ifdef __cplusplus
}
#endif
#endif /* DSRD_PROTOCOL_H */
