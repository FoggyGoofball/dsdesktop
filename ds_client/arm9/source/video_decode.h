/*============================================================================
 * ds_client/arm9/source/video_decode.h
 *==========================================================================*/
#ifndef DSRD_VIDEO_DECODE_H
#define DSRD_VIDEO_DECODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dsrd_video_init(void);
void dsrd_video_process(void);
void dsrd_video_decode_keyframe(const uint8_t *payload, uint16_t len);
void dsrd_video_decode_delta(const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif
#endif
