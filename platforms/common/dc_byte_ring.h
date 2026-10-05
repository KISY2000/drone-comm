#ifndef DC_BYTE_RING_H
#define DC_BYTE_RING_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define DC_BYTE_RING_CAPACITY 1024u
/* One serialized interrupt producer, one main-loop consumer. No parser in IRQ.
 * Explicit ARM DMB/compiler barriers preserve publication ordering on these
 * single-core targets. Do not invoke producers at nested interrupt priorities. */
typedef struct {
    volatile uint8_t bytes[DC_BYTE_RING_CAPACITY];
    volatile uint32_t head, tail;
    volatile bool fault;
    uint32_t overflows;
} dc_byte_ring_t;
void dc_byte_ring_init(dc_byte_ring_t *ring);
bool dc_byte_ring_push(dc_byte_ring_t *ring, uint8_t byte);
/* 1 = byte, 0 = empty, -1 = gap; stop parsing until reset under IRQ mask. */
int dc_byte_ring_pop(dc_byte_ring_t *ring, uint8_t *byte);
void dc_byte_ring_reset(dc_byte_ring_t *ring);
typedef struct { uint16_t last, capacity; } dc_dma_cursor_t;
void dc_dma_cursor_init(dc_dma_cursor_t *cursor, uint16_t capacity);
/* All HT, TC, and IDLE handlers sample the *current* NDTR, not event Size.
 * At least one handler must run per half-buffer. Duplicate samples do nothing. */
void dc_dma_cursor_sample(dc_dma_cursor_t *cursor, const uint8_t *buffer,
                          uint16_t remaining, dc_byte_ring_t *ring);
#endif
