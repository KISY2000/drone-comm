#include "dc_byte_ring.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    dc_byte_ring_t r; dc_dma_cursor_t c;
    uint8_t buf[8]={0,1,2,3,4,5,6,7}, b; unsigned i;
    dc_byte_ring_init(&r); dc_dma_cursor_init(&c,8);
    dc_dma_cursor_sample(&c,buf,4,&r); /* HT */
    dc_dma_cursor_sample(&c,buf,4,&r); /* same-position IDLE */
    for(i=0;i<4;i++) { assert(dc_byte_ring_pop(&r,&b)==1); assert(b==i); }
    assert(dc_byte_ring_pop(&r,&b)==0);
    dc_dma_cursor_sample(&c,buf,0,&r); /* TC NDTR=0 */
    dc_dma_cursor_sample(&c,buf,8,&r); /* duplicate after reload */
    for(i=4;i<8;i++) { assert(dc_byte_ring_pop(&r,&b)==1); assert(b==i); }
    dc_dma_cursor_sample(&c,buf,5,&r);
    dc_dma_cursor_sample(&c,buf,1,&r);
    dc_dma_cursor_sample(&c,buf,6,&r); /* wrap 7->0->1 */
    for(i=0;i<10;i++) { assert(dc_byte_ring_pop(&r,&b)==1); assert(b==(i%8)); }
    assert(dc_byte_ring_pop(&r,&b)==0);
    for(i=0;i<DC_BYTE_RING_CAPACITY;i++) assert(dc_byte_ring_push(&r,(uint8_t)i));
    assert(!dc_byte_ring_push(&r,1)); assert(dc_byte_ring_pop(&r,&b)==-1);
    dc_byte_ring_reset(&r); assert(dc_byte_ring_pop(&r,&b)==0);
    r.head=r.tail=0xfffffff0u;
    for(i=0;i<32;i++) assert(dc_byte_ring_push(&r,(uint8_t)i));
    for(i=0;i<32;i++) { assert(dc_byte_ring_pop(&r,&b)==1); assert(b==i); }
    puts("adapter ring: HT/TC/IDLE, wrap, overflow, index rollover PASS");
    return 0;
}
