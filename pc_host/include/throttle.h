/*============================================================================
 * pc_host/include/throttle.h
 *
 * Dynamic framerate throttling based on DS congestion telemetry.
 *==========================================================================*/
#ifndef DSRD_PC_THROTTLE_H
#define DSRD_PC_THROTTLE_H

#include <stdint.h>
#include "../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void throttle_init(int initial_fps);

/* Feed a congestion report from the DS */
void throttle_update(const dsrd_congestion_t *cong);

/* Returns the number of microseconds to sleep between frames */
uint32_t throttle_get_frame_delay_us(void);

/* Current effective FPS */
int  throttle_get_fps(void);

#ifdef __cplusplus
}
#endif
#endif
