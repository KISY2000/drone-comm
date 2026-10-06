#include "dc_stm32_port.h"
#include "dc_spi_poll8.h"
typedef char dc_spi_register_bits_match[
    SPI_SR_RXNE==DC_SPI_RXNE && SPI_SR_TXE==DC_SPI_TXE && SPI_SR_MODF==DC_SPI_MODF &&
    SPI_SR_OVR==DC_SPI_OVR && SPI_SR_BSY==DC_SPI_BSY && SPI_CR1_SPE==DC_SPI_SPE &&
    SPI_CR1_MSTR==DC_SPI_MSTR ? 1 : -1];
bool dc_stm32_uart_init(dc_stm32_uart_t *p,UART_HandleTypeDef *uart) {
    p->uart=uart; dc_byte_ring_init(&p->rx); dc_dma_cursor_init(&p->cursor,DC_DMA_RX_SIZE);
    dc_queue_init(&p->tx); p->tx_busy=false; p->tx_pending=false; p->recovery=false;
    p->tx_length=0; p->errors=0; p->recoveries=0;
    if(!uart->hdmarx || uart->hdmarx->Init.Mode!=DMA_CIRCULAR) return false;
    return HAL_UARTEx_ReceiveToIdle_DMA(uart,p->dma_rx,DC_DMA_RX_SIZE)==HAL_OK;
}
bool dc_stm32_uart_send(dc_stm32_uart_t *p,const dc_frame_t *f) {
    return dc_queue_push(&p->tx,f);
}
void dc_stm32_uart_rx_irq(dc_stm32_uart_t *p) {
    if(p && !p->recovery && p->uart->hdmarx)
        dc_dma_cursor_sample(&p->cursor,p->dma_rx,
            (uint16_t)__HAL_DMA_GET_COUNTER(p->uart->hdmarx),&p->rx);
}
void dc_stm32_uart_tx_irq(dc_stm32_uart_t *p) { p->tx_busy=false; }
void dc_stm32_uart_error_irq(dc_stm32_uart_t *p) {
    p->errors++; p->recovery=true; p->rx.fault=true;
}
/* Called with IRQs masked. Stop only this RX channel/stream, never the shared
 * DMA controller or UART TX. HAL_DMA_Abort/Init can wait on F4 EN using SysTick;
 * first observe EN clear ourselves so neither can wait with IRQs masked. A
 * delayed/stuck EN is retried by a later poll, even if SysTick has stopped. */
static bool restart_uart_rx(dc_stm32_uart_t *p) {
    DMA_HandleTypeDef *dma=p->uart->hdmarx;
    if(!dma || dma->Init.Mode!=DMA_CIRCULAR) return false;
    CLEAR_BIT(p->uart->Instance->CR1,USART_CR1_RXNEIE|USART_CR1_PEIE|USART_CR1_IDLEIE);
    CLEAR_BIT(p->uart->Instance->CR3,USART_CR3_EIE|USART_CR3_DMAR);
    __HAL_DMA_DISABLE_IT(dma,DMA_IT_TC|DMA_IT_HT|DMA_IT_TE);
#if !defined(DC_STM32_F1)
    __HAL_DMA_DISABLE_IT(dma,DMA_IT_DME);
    __HAL_DMA_DISABLE_IT(dma,DMA_IT_FE);
#endif
    __HAL_DMA_DISABLE(dma);
#if defined(DC_STM32_F1)
    if(dma->Instance->CCR & DMA_CCR_EN) return false;
#else
    if(dma->Instance->CR & DMA_SxCR_EN) return false;
#endif
    if(HAL_DMA_GetState(dma)==HAL_DMA_STATE_BUSY && HAL_DMA_Abort(dma)!=HAL_OK)
        return false;
    /* DeInit clears stale flags/callbacks and Init restores the saved DMA
     * configuration through HAL, including a prior HAL_DMA_STATE_TIMEOUT.
     * ReceiveToIdle installs fresh callbacks before re-enabling this stream. */
    if(HAL_DMA_DeInit(dma)!=HAL_OK || HAL_DMA_Init(dma)!=HAL_OK ||
       HAL_UART_AbortReceive(p->uart)!=HAL_OK) return false;
    dc_byte_ring_reset(&p->rx); dc_dma_cursor_init(&p->cursor,DC_DMA_RX_SIZE);
    return HAL_UARTEx_ReceiveToIdle_DMA(p->uart,p->dma_rx,DC_DMA_RX_SIZE)==HAL_OK;
}
int dc_stm32_uart_read(dc_stm32_uart_t *p,uint8_t *byte) {
    uint32_t mask;
    if(p->recovery || p->rx.fault) {
        mask=__get_PRIMASK(); __disable_irq();
        p->recovery=true;
        if(restart_uart_rx(p)) { p->recovery=false; p->recoveries++; }
        __set_PRIMASK(mask); return -1;
    }
    return dc_byte_ring_pop(&p->rx,byte);
}
void dc_stm32_uart_service(dc_stm32_uart_t *p,uint32_t now) {
    dc_frame_t f;
    if(p->tx_busy && (uint32_t)(now-p->tx_started)>20u) {
        (void)HAL_UART_AbortTransmit(p->uart); p->tx_busy=false;
        p->tx_pending=false; p->errors++;
    }
    if(p->tx_busy) return;
    if(!p->tx_pending && dc_queue_pop(&p->tx,&f)) {
        p->tx_length=dc_encode_uart(&f,p->tx_bytes,sizeof p->tx_bytes);
        p->tx_pending=p->tx_length!=0;
    }
    if(p->tx_pending) {
        p->tx_busy=true; p->tx_started=now;
        if(HAL_UART_Transmit_IT(p->uart,p->tx_bytes,(uint16_t)p->tx_length)==HAL_OK)
            p->tx_pending=false;
        else p->tx_busy=false;
    }
}
void dc_stm32_uart_clear_tx(dc_stm32_uart_t *p) {
    (void)HAL_UART_AbortTransmit(p->uart); p->tx_busy=false; p->tx_pending=false;
    dc_queue_init(&p->tx);
}
static bool select_bus(dc_stm32_radio_bus_t *b) {
    uint32_t start=HAL_GetTick(); unsigned budget=10000;
    HAL_GPIO_WritePin(b->cs_port,b->cs_pin,GPIO_PIN_RESET);
    while(HAL_GPIO_ReadPin(b->miso_port,b->miso_pin)!=GPIO_PIN_RESET) {
        if(!budget-- || (uint32_t)(HAL_GetTick()-start)>=b->ready_timeout_ms) {
            HAL_GPIO_WritePin(b->cs_port,b->cs_pin,GPIO_PIN_SET); return false;
        }
    }
    return true;
}
static void release_bus(dc_stm32_radio_bus_t *b) {
    HAL_GPIO_WritePin(b->cs_port,b->cs_pin,GPIO_PIN_SET);
}
static uint32_t spi_now(void *ctx) { (void)ctx; return HAL_GetTick(); }
static bool transfer(dc_stm32_radio_bus_t *b,uint8_t *tx,uint8_t *rx,uint16_t n) {
    dc_spi_poll8_t p;
    if(!b->spi || !b->spi->Instance || b->spi->Init.Mode!=SPI_MODE_MASTER ||
       b->spi->Init.Direction!=SPI_DIRECTION_2LINES || b->spi->Init.DataSize!=SPI_DATASIZE_8BIT ||
       b->spi->Init.CLKPolarity!=SPI_POLARITY_LOW || b->spi->Init.CLKPhase!=SPI_PHASE_1EDGE ||
       b->spi->Init.FirstBit!=SPI_FIRSTBIT_MSB || b->spi->Init.NSS!=SPI_NSS_SOFT ||
       b->spi->Init.TIMode!=SPI_TIMODE_DISABLE ||
       b->spi->Init.CRCCalculation!=SPI_CRCCALCULATION_DISABLE) return false;
    p.sr=&b->spi->Instance->SR; p.cr1=&b->spi->Instance->CR1;
    p.dr=(volatile uint8_t *)&b->spi->Instance->DR; p.configured_cr1=b->configured_cr1;
    p.now_ms=spi_now; p.ctx=b;
    /* HAL initializes the peripheral; this main-only adapter is its sole I/O
     * owner. Avoid HAL_SPI_End's remaining-time unsigned subtraction. */
    return dc_spi_poll_transfer8(&p,tx,rx,n);
}
static bool reset_pause(unsigned *budget) {
    uint32_t start=HAL_GetTick(),minimum_spins;
    /* Cortex-M3/M4 execute at most one NOP per core cycle. HCLK/25000 NOPs
     * provide >=40us even if starting immediately before a SysTick edge.
     * Also wait a 1ms tick difference. All three SRES pauses share a finite
     * iteration budget, so SysTick failure cannot leave reset spinning. */
    if(!SystemCoreClock || SystemCoreClock>168000000u) return false;
    minimum_spins=SystemCoreClock/25000u+1u;
    for(;;) {
        if(!*budget) return false;
        --*budget; __NOP();
        if(minimum_spins) { --minimum_spins; continue; }
        if((uint32_t)(HAL_GetTick()-start)>=1u) return true;
    }
}
static bool strobe(void *ctx,uint8_t command,uint8_t *status) {
    dc_stm32_radio_bus_t *b=ctx; uint8_t value=0; bool ok;
    /* Datasheet reset sequence: high/low/high, >40 us high, low then SO-ready.
     * Finite core-cycle pauses run only in main during reset, never interrupt. */
    if(command==0x30u) {
        unsigned reset_budget=65536u;
        release_bus(b); if(!reset_pause(&reset_budget)) return false;
        HAL_GPIO_WritePin(b->cs_port,b->cs_pin,GPIO_PIN_RESET);
        if(!reset_pause(&reset_budget)) { release_bus(b); return false; }
        release_bus(b); if(!reset_pause(&reset_budget)) return false;
    }
    if(!select_bus(b)) return false;
    ok=transfer(b,&command,&value,1);
    if(ok && command==0x30u) {
        uint32_t start=HAL_GetTick(); unsigned budget=10000;
        while(HAL_GPIO_ReadPin(b->miso_port,b->miso_pin)!=GPIO_PIN_RESET) {
            if(!budget-- || (uint32_t)(HAL_GetTick()-start)>=b->ready_timeout_ms) { ok=false; break; }
        }
    }
    release_bus(b);
    if(status) *status=value;
    return ok;
}
static bool read_burst(void *ctx,uint8_t address,uint8_t *data,size_t n) {
    dc_stm32_radio_bus_t *b=ctx; uint8_t cmd=(uint8_t)(address|0xc0u),status;
    uint8_t zeros[64]={0}; bool ok;
    if(n>sizeof zeros || !select_bus(b)) return false;
    ok=transfer(b,&cmd,&status,1) && transfer(b,zeros,data,(uint16_t)n);
    release_bus(b); return ok;
}
static bool write_burst(void *ctx,uint8_t address,const uint8_t *data,size_t n) {
    dc_stm32_radio_bus_t *b=ctx; uint8_t cmd=(uint8_t)(address|0x40u),status,dummy[64];
    uint8_t copy[64]; size_t i; bool ok;
    if(n>sizeof copy || !select_bus(b)) return false;
    for(i=0;i<n;i++) copy[i]=data[i];
    ok=transfer(b,&cmd,&status,1) && transfer(b,copy,dummy,(uint16_t)n);
    release_bus(b); return ok;
}
static bool read_reg(void *ctx,uint8_t address,uint8_t *value) {
    dc_stm32_radio_bus_t *b=ctx;
    uint8_t tx[2]={(uint8_t)(address|((address>=0x30u)?0xc0u:0x80u)),0},rx[2]; bool ok;
    if(!select_bus(b)) return false;
    ok=transfer(b,tx,rx,2); release_bus(b); if(ok) *value=rx[1]; return ok;
}
static bool write_reg(void *ctx,uint8_t address,uint8_t value) {
    dc_stm32_radio_bus_t *b=ctx; uint8_t tx[2]={address,value},rx[2]; bool ok;
    if(!select_bus(b)) return false;
    ok=transfer(b,tx,rx,2); release_bus(b); return ok;
}
dc_radio_port_t dc_stm32_radio_port(dc_stm32_radio_bus_t *b) {
    if(b && b->spi && b->spi->Instance) b->configured_cr1=b->spi->Instance->CR1 & ~SPI_CR1_SPE;
    dc_radio_port_t p={b,read_reg,write_reg,read_burst,write_burst,strobe}; return p;
}
