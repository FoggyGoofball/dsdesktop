/*============================================================================
 * ds_client/arm9/source/audio_stream.c
 *
 * Toggleable audio streaming using the libnds soundPlaySample() API.
 *
 * Incoming ADPCM / 8-bit PCM chunks are pushed into a statically
 * allocated double-buffer.  While one half is being played by the
 * default ARM7 firmware (via the sound IPC), the ARM9 fills the other
 * half with new data.  When a buffer is full, it is submitted for
 * looped playback and the write cursor moves to the other half.
 *
 * No dynamic memory allocation — all buffers are static.
 *==========================================================================*/
#include <nds.h>
#include <string.h>

#include "audio_stream.h"
#include "config.h"
#include "../../../common/protocol.h"

/*--------------------------------------------------------------------------
 * Double-buffer for streaming audio
 *------------------------------------------------------------------------*/
#define AUDIO_BUF_SAMPLES  2048   /* samples per half-buffer */
#define AUDIO_SAMPLE_RATE  16384  /* Hz */

static uint8_t s_buf_a[AUDIO_BUF_SAMPLES] __attribute__((aligned(32)));
static uint8_t s_buf_b[AUDIO_BUF_SAMPLES] __attribute__((aligned(32)));

static uint8_t *s_write_buf   = NULL;   /* buffer currently being filled  */
static uint16_t s_write_pos   = 0;      /* write cursor within write_buf  */
static int      s_active_half = 0;      /* 0 = A playing, 1 = B playing   */
static int      s_sound_id    = -1;     /* libnds sound channel            */
static int      s_started     = 0;

/*--------------------------------------------------------------------------*/
void dsrd_audio_init(void)
{
    memset(s_buf_a, 128, sizeof(s_buf_a)); /* 128 = silence for unsigned 8-bit */
    memset(s_buf_b, 128, sizeof(s_buf_b));
    s_write_buf   = s_buf_a;
    s_write_pos   = 0;
    s_active_half = 0;
    s_sound_id    = -1;
    s_started     = 0;

    soundEnable();
}

/*--------------------------------------------------------------------------
 * Submit the current write buffer for playback and swap
 *------------------------------------------------------------------------*/
static void submit_and_swap(void)
{
    /* Flush the buffer we just filled so ARM7 DMA can read it */
    DC_FlushRange(s_write_buf, AUDIO_BUF_SAMPLES);

    /* Stop the previous playback if running */
    if (s_sound_id >= 0) {
        soundKill(s_sound_id);
        s_sound_id = -1;
    }

    /* Start looping playback of the filled buffer */
    s_sound_id = soundPlaySample(
        s_write_buf,
        SoundFormat_8Bit,
        AUDIO_BUF_SAMPLES,
        AUDIO_SAMPLE_RATE,
        127,                /* volume */
        64,                 /* pan = centre */
        false,              /* don't loop — we manually double-buffer */
        0                   /* loop point */
    );

    /* Swap to the other buffer for writing */
    if (s_write_buf == s_buf_a) {
        s_write_buf = s_buf_b;
    } else {
        s_write_buf = s_buf_a;
    }
    s_write_pos = 0;
    s_active_half ^= 1;
    s_started = 1;
}

/*--------------------------------------------------------------------------
 * Push a decoded audio chunk into the write buffer.
 *------------------------------------------------------------------------*/
void dsrd_audio_push_chunk(const uint8_t *payload, uint16_t len)
{
    if (!g_cfg.audio_enabled) return;
    if (len < sizeof(dsrd_audio_hdr_t)) return;

    const dsrd_audio_hdr_t *ah = (const dsrd_audio_hdr_t *)payload;
    const uint8_t *samples = payload + sizeof(dsrd_audio_hdr_t);
    uint16_t data_len = len - sizeof(dsrd_audio_hdr_t);

    (void)ah;  /* format field reserved for future ADPCM support */

    for (uint16_t i = 0; i < data_len; i++) {
        s_write_buf[s_write_pos++] = samples[i];

        if (s_write_pos >= AUDIO_BUF_SAMPLES) {
            submit_and_swap();
        }
    }
}
