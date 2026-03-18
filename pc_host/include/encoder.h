/*============================================================================
 * pc_host/include/encoder.h
 *
 * Video encoder — delta calculation and hybrid keyframe generation.
 *==========================================================================*/
#ifndef DSRD_PC_ENCODER_H
#define DSRD_PC_ENCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void encoder_init(void);

/* Encode the current frame against the previous.
 * Writes one or more packets into `out_buf` and sets `out_len`.
 * `is_keyframe` is set to 1 if a full frame was sent.
 * Returns number of packets written (may be >1 for large deltas). */
int  encoder_encode(const uint8_t *pixels, const uint16_t *palette,
                    uint8_t *out_buf, int out_cap,
                    int *out_len, int *is_keyframe);

/* Force the next call to be a keyframe */
void encoder_force_keyframe(void);

#ifdef __cplusplus
}
#endif
#endif
