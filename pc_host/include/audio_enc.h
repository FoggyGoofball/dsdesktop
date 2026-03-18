/*============================================================================
 * pc_host/include/audio_enc.h
 *
 * Audio capture and compression for multiplexed streaming to the DS.
 *==========================================================================*/
#ifndef DSRD_PC_AUDIO_ENC_H
#define DSRD_PC_AUDIO_ENC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  audio_enc_init(void);
void audio_enc_shutdown(void);

/* Capture and encode an audio chunk.
 * Returns bytes written to out_buf, or 0 if no audio available. */
int  audio_enc_capture(uint8_t *out_buf, int out_cap);

#ifdef __cplusplus
}
#endif
#endif
