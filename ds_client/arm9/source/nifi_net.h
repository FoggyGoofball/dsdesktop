/*============================================================================
 * ds_client/arm9/source/nifi_net.h
 *
 * NiFi networking layer — event-driven architecture inspired by the
 * dsnifi template (jpenny1993).
 *==========================================================================*/
#ifndef DSRD_NIFI_NET_H
#define DSRD_NIFI_NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dsrd_nifi_init(void);
void dsrd_nifi_poll(void);
void dsrd_nifi_send_telemetry(void);
void dsrd_nifi_send_congestion(void);

#ifdef __cplusplus
}
#endif
#endif
