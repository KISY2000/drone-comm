/* Deterministic host-only stress regression. Fixed unsigned 32-bit PRNG,
 * bounded iteration counts, no timing/random-device/network dependencies.
 * Exercises framing/noise recovery, one-bit corruption, sequence boundaries
 * and bounded queues; this does not validate physical UART/RF timing. */
#include "dc_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rng = 0x61cdf419u;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int main(void) {
    dc_frame_t f, decoded;
    dc_parser_t p;
    dc_seq_t s;
    dc_queue_t q;
    uint8_t raw[DC_RAW_MAX], uart[DC_UART_MAX];
    unsigned k, i, bit, noise, crc_cases=0, queue_rounds=0;
    uint16_t samples[]={0,1,2,32767,32768,65535};
    size_t n, wire;
    for (k=0;k<100000;k++) {
        memset(&f,0,sizeof(f)); f.src=(uint8_t)(next()%4+1); f.dst=(uint8_t)(next()%4+1);
        f.type=(uint8_t)next(); f.session=next(); f.seq=(uint16_t)next(); f.len=(uint16_t)(next()%33);
        for (i=0;i<f.len;i++) f.payload[i]=(uint8_t)next();
        n=dc_encode_raw(&f,raw,sizeof(raw)); wire=dc_encode_uart(&f,uart,sizeof(uart));
        assert(n==f.len+14u && wire==n+2 && wire<=sizeof(uart));
        dc_parser_init(&p);
        noise=next()%150;
        for (i=0;i<noise;i++) (void)dc_parser_feed(&p,(uint8_t)next(),&decoded);
        (void)dc_parser_feed(&p,0,&decoded);
        for (i=0;i<wire;i++) assert(dc_parser_feed(&p,uart[i],&decoded)==(i+1u==wire));
        assert(decoded.type==f.type && decoded.src==f.src && decoded.dst==f.dst &&
            decoded.session==f.session && decoded.seq==f.seq && decoded.len==f.len &&
            memcmp(decoded.payload,f.payload,f.len)==0);
        if (k<1000) {
            for (i=0;i<n;i++) {
                for (bit=0;bit<8;bit++) {
                    raw[i]^=(uint8_t)(1u<<bit);
                    assert(dc_decode_raw(raw,n,&decoded)!=DC_OK);
                    raw[i]^=(uint8_t)(1u<<bit); crc_cases++;
                }
            }
        }
    }
    for (k=0;k<65536;k++) {
        for (i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
            dc_seq_reset(&s); assert(dc_seq_accept(&s,(uint16_t)k));
            assert(dc_seq_accept(&s,(uint16_t)(k+samples[i]))==(samples[i]>0 && samples[i]<32768));
            assert(s.last==(uint16_t)(k+((samples[i]>0 && samples[i]<32768)?samples[i]:0)));
        }
    }
    dc_queue_init(&q);
    for (k=0;k<10000;k++) {
        for (i=0;i<DC_QUEUE_CAPACITY;i++) { f.seq=(uint16_t)(k*DC_QUEUE_CAPACITY+i); assert(dc_queue_push(&q,&f)); }
        assert(!dc_queue_push(&q,&f));
        for (i=0;i<DC_QUEUE_CAPACITY;i++) { assert(dc_queue_pop(&q,&decoded)); assert(decoded.seq==(uint16_t)(k*DC_QUEUE_CAPACITY+i)); }
        assert(!dc_queue_pop(&q,&decoded)); queue_rounds++;
    }
    assert(q.overflow==10000);
    printf("PASS random_frames=100000 noise_recovery=100000 single_bit_corruptions=%u seq_boundaries=393216 queue_wrap_rounds=%u\n",crc_cases,queue_rounds);
    return 0;
}
