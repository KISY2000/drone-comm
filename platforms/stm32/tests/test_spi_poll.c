/* Register/clock fixture for the real bounded poll helper, without HAL mocks. */
#include "dc_spi_poll8.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { NORMAL,READY_BUT_EXPIRED,STUCK_TXE,STUCK_RXNE,STUCK_BSY,OVERRUN,MODE_FAULT };
typedef struct {
    volatile uint32_t sr,cr1;
    volatile uint8_t dr;
    unsigned calls,count,mode;
    uint32_t base;
    uint8_t tx_seen[64],reply[64];
} fixture_t;
static uint32_t fixture_clock(void *user) {
    fixture_t *f=user; unsigned byte;
    f->calls++;
    if(f->mode==READY_BUT_EXPIRED) { f->sr=DC_SPI_TXE|DC_SPI_RXNE;return f->base+(f->calls>1?2u:0u); }
    if(f->mode==STUCK_TXE) { f->sr=0;return f->base; }
    if(f->mode==OVERRUN) { f->sr=DC_SPI_OVR|DC_SPI_TXE;return f->base; }
    if(f->mode==MODE_FAULT) {
        f->sr=DC_SPI_MODF|DC_SPI_TXE;f->cr1 &= ~(DC_SPI_MSTR|DC_SPI_SPE);return f->base;
    }
    f->sr=DC_SPI_TXE;
    if(f->mode==STUCK_RXNE) return f->base;
    if(f->calls>=3 && (f->calls&1u) && f->calls<=2*f->count+1) {
        byte=(f->calls-3)/2;f->tx_seen[byte]=f->dr;f->dr=f->reply[byte];f->sr|=DC_SPI_RXNE;
    }
    if(f->mode==STUCK_BSY && f->calls>2*f->count+1) f->sr|=DC_SPI_BSY;
    return f->base;
}
static dc_spi_poll8_t prepare(fixture_t *f,unsigned mode,unsigned count) {
    dc_spi_poll8_t p; unsigned i;
    memset(f,0,sizeof(*f));f->mode=mode;f->count=count;f->sr=DC_SPI_TXE;
    for(i=0;i<count;i++) f->reply[i]=(uint8_t)(255-i);
    p.sr=&f->sr;p.cr1=&f->cr1;p.dr=&f->dr;p.configured_cr1=DC_SPI_MSTR|0x300u;
    p.now_ms=fixture_clock;p.ctx=f;return p;
}
int main(void) {
    fixture_t f;dc_spi_poll8_t p;uint8_t tx[64],rx[64];unsigned i,mode;
    for(i=0;i<64;i++) tx[i]=(uint8_t)i;
    p=prepare(&f,NORMAL,64);assert(dc_spi_poll_transfer8(&p,tx,rx,64));
    assert(!memcmp(tx,f.tx_seen,64)&&!memcmp(rx,f.reply,64));
    assert(f.cr1 & DC_SPI_SPE);
    for(mode=READY_BUT_EXPIRED;mode<=MODE_FAULT;mode++) {
        p=prepare(&f,mode,4);assert(!dc_spi_poll_transfer8(&p,tx,rx,4));
        assert(!(f.cr1 & DC_SPI_SPE)&&(f.cr1 & DC_SPI_MSTR));
        assert(f.calls<=DC_SPI_TRANSFER_POLL_BUDGET+1u);
    }
    /* Reuse this same peripheral/config after MODF, without another HAL_Init. */
    f.mode=NORMAL;f.calls=0;f.sr=DC_SPI_TXE;
    assert(dc_spi_poll_transfer8(&p,tx,rx,4));
    assert(!memcmp(tx,f.tx_seen,4)&&!memcmp(rx,f.reply,4));
    /* Timeout arithmetic and the single deadline stay correct at tick rollover. */
    p=prepare(&f,READY_BUT_EXPIRED,4);f.base=0xffffffffu;
    assert(!dc_spi_poll_transfer8(&p,tx,rx,4));
    p=prepare(&f,NORMAL,4);assert(dc_spi_poll_transfer8(&p,tx,rx,4));
    assert(!dc_spi_poll_transfer8(&p,tx,rx,65));
    puts("SPI register fixture: 64-byte exchange, ready deadline, TXE/RXNE/BSY stalls, OVR/MODF, tick wrap PASS");
    return 0;
}
