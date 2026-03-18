/*============================================================================
 * ds_client/arm9/source/sha256_soft.h
 *
 * Minimal software SHA-256 for HMAC computation on the ARM9.
 * No external dependencies — replaces mbedtls on the DS.
 *==========================================================================*/
#ifndef DSRD_SHA256_SOFT_H
#define DSRD_SHA256_SOFT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buffer[64];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32]);

/* Convenience: single-shot */
void sha256(const uint8_t *data, size_t len, uint8_t digest[32]);

/* HMAC-SHA256 */
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t mac[32]);

#ifdef __cplusplus
}
#endif
#endif
