/*============================================================================
 * ds_client/arm9/source/config.c
 *==========================================================================*/
#include "config.h"

dsrd_ds_config_t g_cfg;

void dsrd_config_init(void)
{
    g_cfg.audio_enabled = 0;    /* off by default — user toggles */
    g_cfg.hmac_enabled  = 0;
    g_cfg.client_id     = 2;    /* DS nodes start at ID 2 */
    g_cfg.wifi_channel  = 1;
}
