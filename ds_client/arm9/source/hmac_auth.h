/*============================================================================
 * ds_client/arm9/source/hmac_auth.h
 *==========================================================================*/
#ifndef DSRD_HMAC_AUTH_H
#define DSRD_HMAC_AUTH_H

#include <stdint.h>
#include "../../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void dsrd_hmac_init(void);

/* Sign: compute HMAC-SHA256 over data[0..len) and fill trailer */
void dsrd_hmac_sign(const uint8_t *data, uint16_t len,
                    dsrd_hmac_trailer_t *trailer);

/* Verify: returns 1 if valid, 0 if failed */
int  dsrd_hmac_verify(const uint8_t *data, uint16_t len,
                      const dsrd_hmac_trailer_t *trailer);

#ifdef __cplusplus
}
#endif
#endif
