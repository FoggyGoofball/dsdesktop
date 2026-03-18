/*============================================================================
 * pc_host/include/input_inject.h
 *
 * Virtual input injection — translates DS telemetry into OS-level
 * mouse, keyboard, and gamepad events.
 *
 * Windows: uses vJoy + SendInput
 * Linux:   uses uinput kernel module
 *==========================================================================*/
#ifndef DSRD_PC_INPUT_INJECT_H
#define DSRD_PC_INPUT_INJECT_H

#include <stdint.h>
#include "../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

int  input_inject_init(void);
void input_inject_shutdown(void);

/* Process a telemetry packet and inject virtual inputs */
void input_inject_process(const dsrd_telemetry_t *tel);

/* Load remap table from config (or use built-in defaults) */
void input_inject_load_remaps(void);

#ifdef __cplusplus
}
#endif
#endif
