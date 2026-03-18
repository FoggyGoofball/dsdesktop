/*============================================================================
 * wii_proxy/source/nifi_tx.h
 *==========================================================================*/
#ifndef WII_NIFI_TX_H
#define WII_NIFI_TX_H

#include <stdint.h>
#include "config_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void nifi_tx_init(const wii_config_t *cfg);
void nifi_tx_send(const uint8_t *payload, uint16_t len);
void nifi_tx_set_channel(uint8_t channel);
uint8_t nifi_tx_get_channel(void);

#ifdef __cplusplus
}
#endif
#endif
