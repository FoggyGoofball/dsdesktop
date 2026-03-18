/*============================================================================
 * ds_client/arm9/source/circ_buf.h
 *
 * Fixed-size circular packet buffer.  Intentionally overwrites the oldest
 * unprocessed slot when full — this is the "lossy UDP" design required by
 * the NiFi latency budget.  All memory is statically allocated.
 *==========================================================================*/
#ifndef DSRD_CIRC_BUF_H
#define DSRD_CIRC_BUF_H

#include <stdint.h>
#include <string.h>
#include "../../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CIRC_SLOT_SIZE  DSRD_MTU

typedef struct {
    uint8_t  slots[DSRD_CIRC_BUF_SLOTS][CIRC_SLOT_SIZE];
    uint16_t lengths[DSRD_CIRC_BUF_SLOTS];
    uint16_t head;           /* next write position */
    uint16_t tail;           /* next read  position */
    uint16_t count;          /* current occupancy   */
    uint16_t overflow_cnt;   /* times we overwrote  */
} dsrd_circ_buf_t;

static inline void circ_buf_init(dsrd_circ_buf_t *cb)
{
    memset(cb, 0, sizeof(*cb));
}

/* Push a packet.  Overwrites oldest slot if full. */
static inline void circ_buf_push(dsrd_circ_buf_t *cb,
                                 const uint8_t *data, uint16_t len)
{
    if (len > CIRC_SLOT_SIZE) len = CIRC_SLOT_SIZE;

    if (cb->count == DSRD_CIRC_BUF_SLOTS) {
        /* Overwrite oldest — advance tail */
        cb->tail = (cb->tail + 1) % DSRD_CIRC_BUF_SLOTS;
        cb->count--;
        cb->overflow_cnt++;
    }

    memcpy(cb->slots[cb->head], data, len);
    cb->lengths[cb->head] = len;
    cb->head = (cb->head + 1) % DSRD_CIRC_BUF_SLOTS;
    cb->count++;
}

/* Pop oldest packet.  Returns 0 if empty. */
static inline uint16_t circ_buf_pop(dsrd_circ_buf_t *cb, uint8_t *out)
{
    if (cb->count == 0) return 0;

    uint16_t len = cb->lengths[cb->tail];
    memcpy(out, cb->slots[cb->tail], len);
    cb->tail = (cb->tail + 1) % DSRD_CIRC_BUF_SLOTS;
    cb->count--;
    return len;
}

static inline int circ_buf_empty(const dsrd_circ_buf_t *cb)
{
    return cb->count == 0;
}

static inline uint16_t circ_buf_drain_overflows(dsrd_circ_buf_t *cb)
{
    uint16_t n = cb->overflow_cnt;
    cb->overflow_cnt = 0;
    return n;
}

#ifdef __cplusplus
}
#endif
#endif /* DSRD_CIRC_BUF_H */
