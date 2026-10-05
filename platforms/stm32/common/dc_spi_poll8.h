#ifndef DC_SPI_POLL8_H
#define DC_SPI_POLL8_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* STM32F1/F4 SPI register bit layout, checked against real CMSIS in the adapter. */
#define DC_SPI_RXNE 0x01u
#define DC_SPI_TXE 0x02u
#define DC_SPI_MODF 0x20u
#define DC_SPI_OVR 0x40u
#define DC_SPI_BSY 0x80u
#define DC_SPI_SPE 0x40u
#define DC_SPI_MSTR 0x04u
#define DC_SPI_TRANSFER_TIMEOUT_MS 2u
#define DC_SPI_TRANSFER_POLL_BUDGET 4096u
typedef struct {
    volatile uint32_t *sr,*cr1;
    volatile uint8_t *dr;
    uint32_t configured_cr1;
    uint32_t (*now_ms)(void *ctx);
    void *ctx;
} dc_spi_poll8_t;
static inline bool dc_spi_wait_flag(const dc_spi_poll8_t *p,uint32_t flag,bool set,
                                    uint32_t start,unsigned *budget) {
    for(;;) {
        uint32_t status;
        if(!*budget) return false;
        --*budget;
        /* Check time even when the hardware flag is already asserted. Same
         * start and budget are shared by every byte, TXE, RXNE and final BSY. */
        if((uint32_t)(p->now_ms(p->ctx)-start)>=DC_SPI_TRANSFER_TIMEOUT_MS) return false;
        status=*p->sr;
        if(status & (DC_SPI_OVR|DC_SPI_MODF)) return false;
        if(((status & flag)!=0)==set) return true;
    }
}
static inline void dc_spi_stop(const dc_spi_poll8_t *p) {
    uint8_t drain=*p->dr;
    uint32_t status=*p->sr;
    (void)drain; (void)status;
    /* DR then SR clears OVR/RXNE; SR then CR1 clears MODF. Restore configured
     * MSTR/SSI/SSM, polarity, phase and prescaler even if MODF cleared MSTR.
     * Leave SPE disabled; the next main-context transaction can re-enable it. */
    *p->cr1=p->configured_cr1 & ~DC_SPI_SPE;
}
static inline bool dc_spi_poll_transfer8(const dc_spi_poll8_t *p,const uint8_t *tx,
                                         uint8_t *rx,size_t count) {
    uint32_t start;
    unsigned budget=DC_SPI_TRANSFER_POLL_BUDGET;
    size_t i;
    if(!p || !p->sr || !p->cr1 || !p->dr || !p->now_ms || !tx || !rx || !count || count>64u ||
       !(p->configured_cr1 & DC_SPI_MSTR)) return false;
    start=p->now_ms(p->ctx);
    *p->cr1=p->configured_cr1 | DC_SPI_SPE;
    for(i=0;i<count;i++) {
        if(!dc_spi_wait_flag(p,DC_SPI_TXE,true,start,&budget)) goto fail;
        *p->dr=tx[i];
        if(!dc_spi_wait_flag(p,DC_SPI_RXNE,true,start,&budget)) goto fail;
        rx[i]=*p->dr;
    }
    if(!dc_spi_wait_flag(p,DC_SPI_TXE,true,start,&budget) ||
       !dc_spi_wait_flag(p,DC_SPI_BSY,false,start,&budget)) goto fail;
    return true;
fail:
    dc_spi_stop(p);
    return false;
}
#endif
