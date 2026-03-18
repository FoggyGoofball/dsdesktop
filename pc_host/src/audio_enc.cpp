/*============================================================================
 * pc_host/src/audio_enc.cpp
 *
 * Audio capture and encoding.
 *
 * Captures system audio via platform loopback, downsamples to 16 kHz
 * mono, encodes as 8-bit PCM (or IMA-ADPCM), and packages into
 * DSRD audio packets for interleaved transmission with video.
 *
 * Windows: WASAPI loopback capture
 * Linux:   PulseAudio monitor source
 *==========================================================================*/
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "../../common/protocol.h"
#include "../include/audio_enc.h"

/* Maximum audio chunk size per packet */
#define AUDIO_CHUNK_MAX  512

static uint16_t s_audio_seq = 0;

#ifdef _WIN32
/*--------------------------------------------------------------------------
 * Windows: WASAPI loopback (simplified stub)
 *------------------------------------------------------------------------*/
#include <windows.h>

static int s_audio_ready = 0;

int audio_enc_init(void)
{
    /* In production: initialise WASAPI loopback capture here.
       For this template, we provide a compilable stub. */
    printf("  Audio capture: WASAPI loopback (stub)\n");
    s_audio_ready = 1;
    return 0;
}

void audio_enc_shutdown(void)
{
    s_audio_ready = 0;
}

int audio_enc_capture(uint8_t *out_buf, int out_cap)
{
    if (!s_audio_ready) return 0;

    /* Stub: produce silence — in production, read WASAPI buffer,
       downsample to 16 kHz mono, convert to 8-bit PCM. */
    int chunk_size = 256;  /* 256 samples @ 16 kHz = 16 ms */

    int needed = (int)sizeof(dsrd_header_t) + (int)sizeof(dsrd_audio_hdr_t)
                 + chunk_size;
    if (needed > out_cap) return 0;

    /* Build packet */
    dsrd_header_t *hdr = (dsrd_header_t *)out_buf;
    dsrd_header_init(hdr, PKT_AUDIO_CHUNK, s_audio_seq++,
                     (uint16_t)(sizeof(dsrd_audio_hdr_t) + chunk_size),
                     0 /* PC */, DSRD_FLAG_AUDIO);

    dsrd_audio_hdr_t *ah = (dsrd_audio_hdr_t *)(out_buf + sizeof(dsrd_header_t));
    ah->sample_count = (uint16_t)chunk_size;
    ah->format       = DSRD_AUDIO_FMT_PCM8;

    /* Silence fill — replace with real capture data */
    uint8_t *samples = out_buf + sizeof(dsrd_header_t) + sizeof(dsrd_audio_hdr_t);
    memset(samples, 128, chunk_size);  /* 128 = silence in unsigned 8-bit PCM */

    return needed;
}

#else
/*--------------------------------------------------------------------------
 * Linux: PulseAudio monitor (simplified stub)
 *------------------------------------------------------------------------*/
static int s_audio_ready = 0;

int audio_enc_init(void)
{
    printf("  Audio capture: PulseAudio monitor (stub)\n");
    s_audio_ready = 1;
    return 0;
}

void audio_enc_shutdown(void)
{
    s_audio_ready = 0;
}

int audio_enc_capture(uint8_t *out_buf, int out_cap)
{
    if (!s_audio_ready) return 0;

    int chunk_size = 256;
    int needed = (int)sizeof(dsrd_header_t) + (int)sizeof(dsrd_audio_hdr_t)
                 + chunk_size;
    if (needed > out_cap) return 0;

    dsrd_header_t *hdr = (dsrd_header_t *)out_buf;
    dsrd_header_init(hdr, PKT_AUDIO_CHUNK, s_audio_seq++,
                     (uint16_t)(sizeof(dsrd_audio_hdr_t) + chunk_size),
                     0, DSRD_FLAG_AUDIO);

    dsrd_audio_hdr_t *ah = (dsrd_audio_hdr_t *)(out_buf + sizeof(dsrd_header_t));
    ah->sample_count = (uint16_t)chunk_size;
    ah->format       = DSRD_AUDIO_FMT_PCM8;

    uint8_t *samples = out_buf + sizeof(dsrd_header_t) + sizeof(dsrd_audio_hdr_t);
    memset(samples, 128, chunk_size);

    return needed;
}
#endif
