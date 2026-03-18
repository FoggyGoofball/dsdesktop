/*============================================================================
 * ds_client/arm9/source/input.h
 *
 * Controller polling, stylus trackpad, and button remapping.
 *==========================================================================*/
#ifndef DSRD_INPUT_H
#define DSRD_INPUT_H

#include <stdint.h>
#include "../../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void dsrd_input_init(void);
void dsrd_input_poll(void);
void dsrd_input_fill_telemetry(dsrd_telemetry_t *tel);

#ifdef __cplusplus
}
#endif
#endif
