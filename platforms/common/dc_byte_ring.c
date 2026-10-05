#include "dc_byte_ring.h"
#if defined(_MSC_VER)
#include <intrin.h>
#endif
static void publication_barrier(void) {
#if defined(__arm__) || defined(__thumb__)
    __asm__ volatile("dmb sy" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" ::: "memory");
#endif
}
void dc_byte_ring_init(dc_byte_ring_t *r) {
    r->head=0; r->tail=0; r->fault=false; r->overflows=0;
}
bool dc_byte_ring_push(dc_byte_ring_t *r,uint8_t b) {
    uint32_t h=r->head;
    if(r->fault) return false;
    if((uint32_t)(h-r->tail)>=DC_BYTE_RING_CAPACITY) {
        r->fault=true; r->overflows++; return false;
    }
    r->bytes[h & (DC_BYTE_RING_CAPACITY-1u)]=b;
    publication_barrier();
    r->head=h+1u;
    return true;
}
int dc_byte_ring_pop(dc_byte_ring_t *r,uint8_t *b) {
    uint32_t t=r->tail;
    if(r->fault) return -1;
    if(t==r->head) return 0;
    publication_barrier();
    *b=r->bytes[t & (DC_BYTE_RING_CAPACITY-1u)];
    publication_barrier();
    r->tail=t+1u; return 1;
}
void dc_byte_ring_reset(dc_byte_ring_t *r) {
    r->tail=r->head; publication_barrier(); r->fault=false;
}
void dc_dma_cursor_init(dc_dma_cursor_t *c,uint16_t capacity) {
    c->last=0; c->capacity=capacity;
}
void dc_dma_cursor_sample(dc_dma_cursor_t *c,const uint8_t *buf,
                          uint16_t remaining,dc_byte_ring_t *r) {
    uint16_t p;
    if(!c->capacity || remaining>c->capacity) { r->fault=true; return; }
    p=(uint16_t)((c->capacity-remaining)%c->capacity);
    publication_barrier();
    while(c->last!=p) {
        (void)dc_byte_ring_push(r,buf[c->last]);
        c->last=(uint16_t)((c->last+1u)%c->capacity);
    }
}
