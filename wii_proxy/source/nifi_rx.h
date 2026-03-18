/*============================================================================
 * wii_proxy/source/nifi_rx.h
 *==========================================================================*/
#ifndef WII_NIFI_RX_H
#define WII_NIFI_RX_H

#include <stdint.h>
#include "config_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void nifi_rx_init(const wii_config_t *cfg);
int  nifi_rx_recv(uint8_t *buf, int max_len);  /* non-blocking */

#ifdef __cplusplus
}
#endif
#endif
