/*============================================================================
 * ds_client/arm9/source/audio_stream.h
 *==========================================================================*/
#ifndef DSRD_AUDIO_STREAM_H
#define DSRD_AUDIO_STREAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dsrd_audio_init(void);
void dsrd_audio_push_chunk(const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif
#endif
