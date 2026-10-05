#include "dc_zynq_port.h"
#include "board_config.h"
#include "xtime_l.h"
#include "xpseudo_asm.h"
extern XUartPs_Config XUartPs_ConfigTable[];
static XUartPs_Config *physical_config(uint32_t base) {
    unsigned i;
    for(i=0;i<XPAR_XUARTPS_NUM_INSTANCES;i++)
        if(XUartPs_ConfigTable[i].BaseAddress==base) return &XUartPs_ConfigTable[i];
    return 0;
}
static void rx_interrupt(void *ref) {
    dc_zynq_port_t *p=ref; uint32_t base=p->uart.Config.BaseAddress;
    uint32_t status=XUartPs_ReadReg(base,XUARTPS_ISR_OFFSET); unsigned budget=64;
    XUartPs_WriteReg(base,XUARTPS_ISR_OFFSET,status);
    if(status & (XUARTPS_IXR_OVER|XUARTPS_IXR_FRAMING|XUARTPS_IXR_PARITY)) {
        p->errors++; p->rx.fault=true;
    }
    while(budget-- && !(XUartPs_ReadReg(base,XUARTPS_SR_OFFSET)&XUARTPS_SR_RXEMPTY))
        (void)dc_byte_ring_push(&p->rx,(uint8_t)XUartPs_ReadReg(base,XUARTPS_FIFO_OFFSET));
    /* Restart timeout counter for the next sub-threshold burst. */
    XUartPs_WriteReg(base,XUARTPS_CR_OFFSET,
        XUartPs_ReadReg(base,XUARTPS_CR_OFFSET)|XUARTPS_CR_TORST);
}
bool dc_zynq_port_init(dc_zynq_port_t *p,XScuGic *gic) {
    XUartPs_Config *cfg=physical_config(DC_ZYNQ_COMM_BASE);
    XUartPsFormat fmt={DC_UART_BAUD,XUARTPS_FORMAT_8_BITS,XUARTPS_FORMAT_NO_PARITY,XUARTPS_FORMAT_1_STOP_BIT};
    if(!cfg || XUartPs_CfgInitialize(&p->uart,cfg,cfg->BaseAddress)!=XST_SUCCESS) return false;
    if(XUartPs_SetDataFormat(&p->uart,&fmt)!=XST_SUCCESS) return false;
    dc_byte_ring_init(&p->rx); dc_queue_init(&p->tx);
    p->tx_length=0; p->tx_offset=0; p->errors=0; p->tx_started=0; p->tx_timeouts=0; p->tx_active=false;
    XUartPs_SetOperMode(&p->uart,XUARTPS_OPER_MODE_NORMAL);
    XUartPs_SetFifoThreshold(&p->uart,16); XUartPs_SetRecvTimeout(&p->uart,8);
    XUartPs_SetInterruptMask(&p->uart,0);
    XUartPs_WriteReg(cfg->BaseAddress,XUARTPS_ISR_OFFSET,XUARTPS_IXR_MASK);
    if(XScuGic_Connect(gic,DC_ZYNQ_COMM_IRQ,rx_interrupt,p)!=XST_SUCCESS) return false;
    XScuGic_SetPriorityTriggerType(gic,DC_ZYNQ_COMM_IRQ,0xa0u,0x1u);
    XScuGic_Enable(gic,DC_ZYNQ_COMM_IRQ);
    XUartPs_SetInterruptMask(&p->uart,XUARTPS_IXR_RXOVR|XUARTPS_IXR_TOUT|
                XUARTPS_IXR_RXFULL|XUARTPS_IXR_OVER|XUARTPS_IXR_FRAMING|XUARTPS_IXR_PARITY);
    return true;
}
bool dc_zynq_port_send(dc_zynq_port_t *p,const dc_frame_t *f) { return dc_queue_push(&p->tx,f); }
void dc_zynq_port_service(dc_zynq_port_t *p) {
    dc_frame_t f; unsigned budget=64; uint32_t base=p->uart.Config.BaseAddress;
    uint32_t now=dc_zynq_now_ms();
    if(p->tx_active && p->tx_offset==p->tx_length &&
       (XUartPs_ReadReg(base,XUARTPS_SR_OFFSET)&XUARTPS_SR_TXEMPTY)) p->tx_active=false;
    if(p->tx_active && (uint32_t)(now-p->tx_started)>=20u) {
        uint32_t cr=XUartPs_ReadReg(base,XUARTPS_CR_OFFSET);
        XUartPs_WriteReg(base,XUARTPS_CR_OFFSET,cr|XUARTPS_CR_TXRST);
        p->tx_offset=p->tx_length; p->tx_active=false; p->tx_timeouts++; p->errors++;
    }
    if(!p->tx_active && dc_queue_pop(&p->tx,&f)) {
        p->tx_length=dc_encode_uart(&f,p->tx_bytes,sizeof p->tx_bytes); p->tx_offset=0; p->tx_started=now;
        p->tx_active=p->tx_length!=0;
    }
    while(budget-- && p->tx_offset<p->tx_length &&
         !(XUartPs_ReadReg(base,XUARTPS_SR_OFFSET)&XUARTPS_SR_TXFULL))
        XUartPs_WriteReg(base,XUARTPS_FIFO_OFFSET,p->tx_bytes[p->tx_offset++]);
}
int dc_zynq_port_read(dc_zynq_port_t *p,uint8_t *byte) {
    if(p->rx.fault) {
        /* Main-only reset: mask IRQ without changing its previous enabled state. */
        u32 previous; __asm__ volatile("mrs %0, cpsr" : "=r"(previous));
        __asm__ volatile("cpsid i" ::: "memory"); dc_byte_ring_reset(&p->rx);
        if(!(previous & 0x80u)) __asm__ volatile("cpsie i" ::: "memory");
        return -1;
    }
    return dc_byte_ring_pop(&p->rx,byte);
}
void dc_zynq_port_clear(dc_zynq_port_t *p) {
    uint32_t base=p->uart.Config.BaseAddress;
    XUartPs_WriteReg(base,XUARTPS_CR_OFFSET,XUartPs_ReadReg(base,XUARTPS_CR_OFFSET)|XUARTPS_CR_TXRST);
    dc_queue_init(&p->tx); p->tx_length=0; p->tx_offset=0; p->tx_active=false;
}
uint32_t dc_zynq_now_ms(void) {
    XTime ticks; XTime_GetTime(&ticks);
    return (uint32_t)(ticks / (COUNTS_PER_SECOND/1000u));
}
