/*============================================================================
 * pc_host/include/hmac_host.h
 *
 * HMAC-SHA256 verification for incoming DS telemetry.
 *==========================================================================*/
#ifndef DSRD_PC_HMAC_HOST_H
#define DSRD_PC_HMAC_HOST_H

#include <stdint.h>
#include "../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void hmac_host_init(void);

/* Verify an incoming telemetry packet.
 * Returns 1 if valid (or HMAC disabled), 0 if failed. */
int  hmac_host_verify(const uint8_t *pkt, int pkt_len,
                      int hmac_enabled);

#ifdef __cplusplus
}
#endif
#endif
