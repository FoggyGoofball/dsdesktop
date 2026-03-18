/*============================================================================
 * wii_proxy/source/config_ui.h
 *
 * Simple configuration for the Wii proxy — supports Wi-Fi Only and
 * USB Ethernet backhaul modes, plus channel alignment setting.
 *==========================================================================*/
#ifndef WII_CONFIG_UI_H
#define WII_CONFIG_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  use_usb_ethernet;   /* 0 = Wi-Fi Only (Mode A), 1 = USB (B) */
    uint8_t  wifi_channel;       /* 1-11, forced for channel alignment    */
    uint8_t  auto_channel;       /* 1 = benchmark candidates and choose best */
    uint8_t  channel_candidates[11];
    uint8_t  channel_candidate_count;
    uint8_t  probe_count;        /* probes per channel */
    uint16_t probe_timeout_ms;   /* timeout per probe  */
    char     pc_ip[16];          /* PC host IP for upstream telemetry     */
    uint16_t pc_port;            /* DSRD_PORT                             */
} wii_config_t;

void wii_config_load(wii_config_t *cfg);
void wii_config_show(const wii_config_t *cfg);

#ifdef __cplusplus
}
#endif
#endif
