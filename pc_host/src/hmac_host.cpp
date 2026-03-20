/*============================================================================
 * pc_host/src/hmac_host.cpp
 *
 * HMAC-SHA256 verification for incoming DS telemetry.
 *
 * Uses OpenSSL's HMAC on the PC side (lighter dependency than mbedtls
 * on a full desktop OS).  Falls back to a simple hand-rolled HMAC if
 * OpenSSL is not available.
 *==========================================================================*/
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "../../common/protocol.h"
#include "../include/hmac_host.h"

#ifdef HAS_OPENSSL
#  include <openssl/hmac.h>
#  include <openssl/evp.h>
#endif

static uint32_t s_last_seen_seq = 0;

#ifndef HAS_OPENSSL
/*--------------------------------------------------------------------------
 * Software SHA-256 / HMAC-SHA256 fallback
 *------------------------------------------------------------------------*/
typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buffer[64];
} sha256_ctx_t;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)      (ROR32(x,2) ^ ROR32(x,13) ^ ROR32(x,22))
#define EP1(x)      (ROR32(x,6) ^ ROR32(x,11) ^ ROR32(x,25))
#define SIG0(x)     (ROR32(x,7) ^ ROR32(x,18) ^ ((x) >> 3))
#define SIG1(x)     (ROR32(x,17) ^ ROR32(x,19) ^ ((x) >> 10))

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t W[64], a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++)
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + W[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init_soft(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->bitcount = 0;
}

static void sha256_update_soft(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t buf_used = (size_t)(ctx->bitcount >> 3) & 63;
    ctx->bitcount += (uint64_t)len << 3;

    if (buf_used > 0) {
        size_t space = 64 - buf_used;
        if (len < space) {
            memcpy(ctx->buffer + buf_used, data, len);
            return;
        }
        memcpy(ctx->buffer + buf_used, data, space);
        sha256_transform(ctx, ctx->buffer);
        data += space;
        len -= space;
    }

    while (len >= 64) {
        sha256_transform(ctx, data);
        data += 64;
        len -= 64;
    }

    if (len > 0)
        memcpy(ctx->buffer, data, len);
}

static void sha256_final_soft(sha256_ctx_t *ctx, uint8_t digest[32])
{
    size_t buf_used = (size_t)(ctx->bitcount >> 3) & 63;
    ctx->buffer[buf_used++] = 0x80;

    if (buf_used > 56) {
        memset(ctx->buffer + buf_used, 0, 64 - buf_used);
        sha256_transform(ctx, ctx->buffer);
        buf_used = 0;
    }
    memset(ctx->buffer + buf_used, 0, 56 - buf_used);

    uint64_t bits = ctx->bitcount;
    for (int i = 7; i >= 0; i--)
        ctx->buffer[56 + (7 - i)] = (uint8_t)(bits >> (i * 8));
    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

static void hmac_sha256_soft(const uint8_t *key, size_t key_len,
                             const uint8_t *data_a, size_t len_a,
                             const uint8_t *data_b, size_t len_b,
                             uint8_t mac[32])
{
    uint8_t k_pad[64];
    uint8_t key_hash[32];
    sha256_ctx_t ctx;

    if (key_len > 64) {
        sha256_init_soft(&ctx);
        sha256_update_soft(&ctx, key, key_len);
        sha256_final_soft(&ctx, key_hash);
        key = key_hash;
        key_len = 32;
    }

    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < key_len; i++)
        k_pad[i] ^= key[i];

    sha256_init_soft(&ctx);
    sha256_update_soft(&ctx, k_pad, 64);
    sha256_update_soft(&ctx, data_a, len_a);
    sha256_update_soft(&ctx, data_b, len_b);
    uint8_t inner[32];
    sha256_final_soft(&ctx, inner);

    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < key_len; i++)
        k_pad[i] ^= key[i];

    sha256_init_soft(&ctx);
    sha256_update_soft(&ctx, k_pad, 64);
    sha256_update_soft(&ctx, inner, 32);
    sha256_final_soft(&ctx, mac);
}
#endif

static int ct_compare_32(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    for (int i = 0; i < DSRD_HMAC_LEN; i++)
        diff |= a[i] ^ b[i];
    return diff == 0;
}

void hmac_host_init(void)
{
    s_last_seen_seq = 0;
}

int hmac_host_verify(const uint8_t *pkt, int pkt_len, int hmac_enabled)
{
    if (!hmac_enabled)
        return 1;

    const dsrd_header_t *hdr = (const dsrd_header_t *)pkt;
    if (!(hdr->flags & DSRD_FLAG_HMAC))
        return 0;

    int payload_end = (int)sizeof(dsrd_header_t) + hdr->payload_len;
    int trailer_start = payload_end;
    if (trailer_start + (int)sizeof(dsrd_hmac_trailer_t) > pkt_len)
        return 0;

    const dsrd_hmac_trailer_t *trailer =
        (const dsrd_hmac_trailer_t *)(pkt + trailer_start);

    /* Anti-replay: sequence must be strictly increasing */
    if (trailer->hmac_seq <= s_last_seen_seq && s_last_seen_seq != 0)
        return 0;

    uint8_t computed[32];
#ifdef HAS_OPENSSL
    unsigned int computed_len = 0;
    HMAC_CTX *ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, DSRD_PSK, DSRD_PSK_LEN, EVP_sha256(), NULL);
    HMAC_Update(ctx, pkt, payload_end);
    HMAC_Update(ctx, (const unsigned char *)&trailer->hmac_seq, 4);
    HMAC_Final(ctx, computed, &computed_len);
    HMAC_CTX_free(ctx);
#else
    hmac_sha256_soft((const uint8_t *)DSRD_PSK, DSRD_PSK_LEN,
                     pkt, payload_end,
                     (const uint8_t *)&trailer->hmac_seq, 4,
                     computed);
#endif

    if (!ct_compare_32(computed, trailer->hmac))
        return 0;

    s_last_seen_seq = trailer->hmac_seq;
    return 1;
}
