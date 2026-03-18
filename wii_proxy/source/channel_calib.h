/*============================================================================
 * wii_proxy/source/channel_calib.h
 *
 * Channel-latency calibration for DS↔Wii local wireless link.
 *============================================================================*/
#ifndef WII_CHANNEL_CALIB_H
#define WII_CHANNEL_CALIB_H

#include <stdint.h>
#include "config_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runs latency benchmark across candidate channels.
 * Returns best channel (1..11), or 0 if no valid result.
 */
uint8_t channel_calib_run(const wii_config_t *cfg);

#ifdef __cplusplus
}
#endif
#endif
