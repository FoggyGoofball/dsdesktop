/*============================================================================
 * wii_proxy/source/backhaul.h
 *
 * PC ↔ Wii network backhaul abstraction.
 * Supports two modes:
 *   Mode A — Wi-Fi (Broadcom BCM4318, infrastructure mode)
 *   Mode B — USB Ethernet (ASIX AX88772 adapter via libogc)
 *==========================================================================*/
#ifndef WII_BACKHAUL_H
#define WII_BACKHAUL_H

#include <stdint.h>
#include "config_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

int  backhaul_init(const wii_config_t *cfg);
void backhaul_shutdown(void);
int  backhaul_recv(uint8_t *buf, int max_len);   /* non-blocking */
int  backhaul_send(const uint8_t *buf, uint16_t len);
const char *backhaul_ip_str(void);
int  backhaul_reconnect(const wii_config_t *cfg); /* non-blocking attempt */

#ifdef __cplusplus
}
#endif
#endif
