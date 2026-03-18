/*============================================================================
 * pc_host/src/throttle.cpp
 *
 * Dynamic framerate throttling.
 *
 * Monitors DS congestion reports (rx_overflows) and automatically steps
 * the capture rate down from 30 → 15 FPS when the network is congested,
 * then back up when it clears.
 *==========================================================================*/
#include <cstdio>
#include <cstdint>

#include "../include/throttle.h"
#include "../../common/protocol.h"

static int s_target_fps = 30;
static int s_min_fps    = 15;
static int s_max_fps    = 30;

/* Hysteresis: require N consecutive "clear" reports to increase FPS */
#define CLEAR_THRESHOLD 5
static int s_clear_streak = 0;

void throttle_init(int initial_fps)
{
    s_target_fps  = initial_fps;
    s_max_fps     = initial_fps;
    s_min_fps     = initial_fps / 2;
    if (s_min_fps < 5) s_min_fps = 5;
    s_clear_streak = 0;
}

void throttle_update(const dsrd_congestion_t *cong)
{
    if (cong->rx_overflows > 0 || cong->rx_drops > 10) {
        /* Network congested — step down */
        if (s_target_fps > s_min_fps) {
            s_target_fps = s_min_fps;
            printf("[THROTTLE] Congested (ovf=%d drops=%d) → %d FPS\n",
                   cong->rx_overflows, cong->rx_drops, s_target_fps);
        }
        s_clear_streak = 0;
    } else {
        /* Network clear */
        s_clear_streak++;
        if (s_clear_streak >= CLEAR_THRESHOLD && s_target_fps < s_max_fps) {
            s_target_fps = s_max_fps;
            printf("[THROTTLE] Clear → %d FPS\n", s_target_fps);
            s_clear_streak = 0;
        }
    }
}

uint32_t throttle_get_frame_delay_us(void)
{
    if (s_target_fps <= 0) return 33333;
    return 1000000u / (uint32_t)s_target_fps;
}

int throttle_get_fps(void)
{
    return s_target_fps;
}
