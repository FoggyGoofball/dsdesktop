/*============================================================================
 * ds_client/arm9/source/config.h
 *
 * Runtime configuration state for the DS client.
 *==========================================================================*/
#ifndef DSRD_DS_CONFIG_H
#define DSRD_DS_CONFIG_H

#include <stdint.h>
#include "../../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t audio_enabled;
    uint8_t hmac_enabled;
    uint8_t client_id;          /* our NiFi origin tag */
    uint8_t wifi_channel;       /* requested channel from proxy */
} dsrd_ds_config_t;

extern dsrd_ds_config_t g_cfg;

void dsrd_config_init(void);

#ifdef __cplusplus
}
#endif
#endif /* DSRD_DS_CONFIG_H */
