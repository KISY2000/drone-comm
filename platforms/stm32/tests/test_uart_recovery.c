/* Compile the unchanged complete dc_stm32_port.c against a small HAL fixture.
 * F4 state behavior follows the bundled HAL: AbortReceive clears DMAR before
 * an abort timeout, DMA stays TIMEOUT, and Start_IT accepts READY only. This
 * checks adapter recovery, not physical DMA timing or vendor HAL execution. */
#include "dc_stm32_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t irq_mask;
static unsigned deinit_calls,init_calls,receive_starts,abort_tx_calls;
static unsigned fail_deinit,fail_init,fail_receive;
static unsigned blocking_abort_attempts;
uint32_t SystemCoreClock=72000000u;
uint32_t HAL_GetTick(void) { return 123u; } /* deliberately frozen */
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask=1; }
void __set_PRIMASK(uint32_t value) { irq_mask=value; }
static bool enabled(const DMA_HandleTypeDef *dma) {
#if defined(DC_STM32_F1)
    return (dma->Instance->CCR & DMA_CCR_EN)!=0;
#else
    return (dma->Instance->CR & DMA_SxCR_EN)!=0;
#endif
}
void dc_mock_dma_disable(DMA_HandleTypeDef *dma) {
    if(!dma->hold_enable) {
        dma->Instance->CCR &= ~DMA_CCR_EN;
        dma->Instance->CR &= ~DMA_SxCR_EN;
    }
}
void dc_mock_dma_disable_it(DMA_HandleTypeDef *dma,uint32_t flags) {
#if defined(DC_STM32_F1)
    dma->Instance->CCR &= ~flags;
#else
    if(flags==DMA_IT_FE) dma->Instance->FCR &= ~flags;
    else dma->Instance->CR &= ~flags;
#endif
}
HAL_DMA_StateTypeDef HAL_DMA_GetState(DMA_HandleTypeDef *dma) { return dma->State; }
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma) {
    if(dma->State!=HAL_DMA_STATE_BUSY) return HAL_ERROR;
    dc_mock_dma_disable(dma);
    if(enabled(dma)) {
        ++blocking_abort_attempts;
        dma->State=HAL_DMA_STATE_TIMEOUT;
        return HAL_TIMEOUT;
    }
    dma->Instance->flags=0;dma->State=HAL_DMA_STATE_READY;return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *dma) {
    ++deinit_calls;
    if(dma->State==HAL_DMA_STATE_BUSY) return HAL_BUSY;
    assert(!enabled(dma)); /* never reconfigure a running stream */
    if(fail_deinit) {--fail_deinit;return HAL_ERROR;}
    memset(dma->Instance,0,sizeof(*dma->Instance));
    dma->callbacks_installed=false;dma->State=HAL_DMA_STATE_RESET;return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *dma) {
    ++init_calls;
    assert(!enabled(dma) && dma->Instance->flags==0);
    assert(dma->Init.Mode==DMA_CIRCULAR && dma->Parent!=NULL);
    if(fail_init) {--fail_init;dma->State=HAL_DMA_STATE_TIMEOUT;return HAL_ERROR;}
    dma->Instance->CCR=DMA_CIRCULAR;dma->Instance->CR=DMA_CIRCULAR;
    dma->State=HAL_DMA_STATE_READY;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart) {
    uart->Instance->CR1 &= ~(USART_CR1_RXNEIE|USART_CR1_PEIE|USART_CR1_IDLEIE);
    uart->Instance->CR3 &= ~USART_CR3_EIE;
    if(uart->Instance->CR3 & USART_CR3_DMAR) {
        uart->Instance->CR3 &= ~USART_CR3_DMAR;
        if(HAL_DMA_Abort(uart->hdmarx)==HAL_TIMEOUT) return HAL_TIMEOUT;
    }
    uart->RxState=HAL_DMA_STATE_READY;return HAL_OK;
}
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *uart,uint8_t *bytes,uint16_t length) {
    DMA_HandleTypeDef *dma=uart->hdmarx;
    assert(bytes && length==DC_DMA_RX_SIZE);
    if(uart->RxState!=HAL_DMA_STATE_READY) return HAL_BUSY;
    if(dma->State!=HAL_DMA_STATE_READY) return HAL_ERROR;
    if(fail_receive) {--fail_receive;return HAL_ERROR;}
    uart->RxState=HAL_DMA_STATE_BUSY;dma->State=HAL_DMA_STATE_BUSY;
    dma->callbacks_installed=true;
    dma->Instance->CCR=DMA_CIRCULAR|DMA_CCR_EN|DMA_IT_TC|DMA_IT_HT|DMA_IT_TE;
    dma->Instance->CR=DMA_CIRCULAR|DMA_SxCR_EN|DMA_IT_TC|DMA_IT_HT|DMA_IT_TE|DMA_IT_DME;
    dma->Instance->FCR=DMA_IT_FE;
    dma->Instance->CNDTR=length;dma->Instance->NDTR=length;
    uart->Instance->CR1 |= USART_CR1_IDLEIE;
    uart->Instance->CR3 |= USART_CR3_EIE|USART_CR3_DMAR;
    ++receive_starts;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart) {
    (void)uart;++abort_tx_calls;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,uint8_t *bytes,uint16_t length) {
    (void)uart;(void)bytes;(void)length;return HAL_OK;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio,uint16_t pin,GPIO_PinState value) {
    (void)gpio;(void)pin;(void)value;
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio,uint16_t pin) {
    (void)gpio;(void)pin;return GPIO_PIN_RESET;
}
typedef struct {
    dc_stm32_uart_t port;
    UART_HandleTypeDef uart;
    USART_TypeDef usart;
    DMA_HandleTypeDef dma;
    DMA_Registers regs;
} fixture_t;
static void prepare(fixture_t *f) {
    memset(f,0,sizeof(*f));
    f->uart.Instance=&f->usart;f->uart.hdmarx=&f->dma;f->uart.RxState=HAL_DMA_STATE_READY;
    f->dma.Instance=&f->regs;f->dma.State=HAL_DMA_STATE_READY;
    f->dma.Init.Mode=DMA_CIRCULAR;f->dma.Parent=&f->uart;
    assert(dc_stm32_uart_init(&f->port,&f->uart));
}
static void confirm_new_rx(fixture_t *f) {
    uint8_t byte;unsigned i;
    assert(!f->port.recovery && !f->port.rx.fault && f->port.cursor.last==0);
    assert(f->dma.callbacks_installed && enabled(&f->dma));
    assert(f->dma.State==HAL_DMA_STATE_BUSY && f->regs.flags==0);
    assert(f->dma.Init.Mode==DMA_CIRCULAR && f->dma.Parent==&f->uart);
    for(i=0;i<7;i++) f->port.dma_rx[i]=(uint8_t)(90+i);
    f->regs.CNDTR=f->regs.NDTR=DC_DMA_RX_SIZE-7u;
    dc_stm32_uart_rx_irq(&f->port);
    dc_stm32_uart_rx_irq(&f->port); /* duplicate IDLE/HT sample */
    for(i=0;i<7;i++) {
        assert(dc_stm32_uart_read(&f->port,&byte)==1);
        assert(byte==(uint8_t)(90+i));
    }
    assert(dc_stm32_uart_read(&f->port,&byte)==0);
}
static void test_late_disable_and_timeout_state(void) {
    fixture_t f,other;unsigned i,starts,before_deinit,before_init;uint8_t byte;
    DMA_Registers other_regs;USART_TypeDef other_usart;
    prepare(&f);prepare(&other);
    other_regs=other.regs;other_usart=other.usart;
    starts=receive_starts;before_deinit=deinit_calls;before_init=init_calls;
    f.usart.CR1 |= USART_CR1_TXEIE|USART_CR1_TCIE;
    f.port.tx_busy=true;f.port.tx_pending=true;f.port.tx_bytes[0]=0x5a;
    assert(dc_byte_ring_push(&f.port.rx,0x33));
    dc_stm32_uart_error_irq(&f.port);
    f.dma.State=HAL_DMA_STATE_TIMEOUT;f.dma.hold_enable=true;f.regs.flags=0x3fu;
    f.usart.CR3 &= ~USART_CR3_DMAR; /* UART Abort already removed the request. */
    irq_mask=0;
    for(i=0;i<32;i++) {
        assert(dc_stm32_uart_read(&f.port,&byte)==-1);
        assert(irq_mask==0 && f.port.recovery && f.port.recoveries==0);
    }
    assert(receive_starts==starts && deinit_calls==before_deinit && init_calls==before_init);
    assert(blocking_abort_attempts==0); /* no HAL wait with a frozen tick */
    f.dma.hold_enable=false;
    assert(dc_stm32_uart_read(&f.port,&byte)==-1);
    assert(receive_starts==starts+1 && f.port.recoveries==1);
    assert(deinit_calls==before_deinit+1 && init_calls==before_init+1);
    assert(f.port.tx_busy && f.port.tx_pending && f.port.tx_bytes[0]==0x5a);
    assert((f.usart.CR1 & (USART_CR1_TXEIE|USART_CR1_TCIE))==(USART_CR1_TXEIE|USART_CR1_TCIE));
    assert(abort_tx_calls==0 && memcmp(&other_regs,&other.regs,sizeof other_regs)==0);
    assert(memcmp(&other_usart,&other.usart,sizeof other_usart)==0);
    confirm_new_rx(&f);
}
static void test_software_overflow_and_mask_restore(void) {
    fixture_t f;unsigned i;uint8_t byte;
    prepare(&f);
    for(i=0;i<DC_BYTE_RING_CAPACITY;i++) assert(dc_byte_ring_push(&f.port.rx,0x55));
    assert(!dc_byte_ring_push(&f.port.rx,0x66));
    irq_mask=1;
    assert(dc_stm32_uart_read(&f.port,&byte)==-1);
    assert(irq_mask==1 && f.port.recoveries==1 && f.port.rx.overflows==1);
    irq_mask=0;confirm_new_rx(&f);
}
static void test_hal_failure_retries(void) {
    fixture_t f;uint8_t byte;
    prepare(&f);dc_stm32_uart_error_irq(&f.port);
    fail_deinit=1;
    assert(dc_stm32_uart_read(&f.port,&byte)==-1 && f.port.recovery);
    assert(!enabled(&f.dma) && f.port.recoveries==0);
    fail_init=1;
    assert(dc_stm32_uart_read(&f.port,&byte)==-1 && f.port.recovery);
    assert(f.dma.State==HAL_DMA_STATE_TIMEOUT && f.port.recoveries==0);
    fail_receive=1;
    assert(dc_stm32_uart_read(&f.port,&byte)==-1 && f.port.recovery);
    assert(!enabled(&f.dma) && f.port.recoveries==0);
    assert(dc_stm32_uart_read(&f.port,&byte)==-1 && !f.port.recovery);
    assert(f.port.recoveries==1);confirm_new_rx(&f);
}
int main(void) {
    test_late_disable_and_timeout_state();
    test_software_overflow_and_mask_restore();
    test_hal_failure_retries();
    puts("UART port: delayed DMA stop/frozen tick, TIMEOUT state recovery, HAL retry, RX gap, IRQ mask and TX/peer isolation PASS");
    return 0;
}
