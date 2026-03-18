/*============================================================================
 * wii_proxy/source/ip_manager.h
 *
 * Runtime IP management for Wii proxy.
 * Allows user to select/edit PC host IP from on-screen menu.
 * Persists IPs to sd:/apps/dsremote/ips.cfg
 *==========================================================================*/
#ifndef WII_IP_MANAGER_H
#define WII_IP_MANAGER_H

#include <stdint.h>
#include "config_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load saved IPs from sd:/apps/dsremote/ips.cfg */
void ip_manager_load(void);

/* Display on-screen IP selection menu.
 * Returns 1 if user selected an IP (cfg->pc_ip updated), 0 if cancelled.
 */
int ip_manager_show_menu(wii_config_t *cfg);

/* Add a new IP to the saved list and persist to SD card */
void ip_manager_add_ip(const char *ip);

#ifdef __cplusplus
}
#endif
#endif /* WII_IP_MANAGER_H */
