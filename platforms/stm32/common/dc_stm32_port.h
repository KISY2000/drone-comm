#ifndef DC_STM32_PORT_H
#define DC_STM32_PORT_H
#include "dc_protocol.h"
#include "dc_radio.h"
#include "dc_byte_ring.h"
#if defined(DC_STM32_F1)
#include "stm32f1xx_hal.h"
#else
#include "stm32f4xx_hal.h"
#endif
#define DC_DMA_RX_SIZE 256u
typedef struct {
    UART_HandleTypeDef *uart;
    dc_byte_ring_t rx;
    dc_dma_cursor_t cursor;
    uint8_t dma_rx[DC_DMA_RX_SIZE]; /* SRAM, never F407 CCMRAM */
    dc_queue_t tx;
    uint8_t tx_bytes[DC_UART_MAX];
    size_t tx_length;
    volatile bool tx_busy, recovery;
    bool tx_pending;
    uint32_t tx_started, errors, recoveries;
} dc_stm32_uart_t;
bool dc_stm32_uart_init(dc_stm32_uart_t *p,UART_HandleTypeDef *uart);
bool dc_stm32_uart_send(dc_stm32_uart_t *p,const dc_frame_t *frame);
void dc_stm32_uart_service(dc_stm32_uart_t *p,uint32_t now);
/* Returns -1 once for stream reset; main must reinitialize its parser. */
int dc_stm32_uart_read(dc_stm32_uart_t *p,uint8_t *byte);
void dc_stm32_uart_rx_irq(dc_stm32_uart_t *p);
void dc_stm32_uart_tx_irq(dc_stm32_uart_t *p);
void dc_stm32_uart_error_irq(dc_stm32_uart_t *p);
void dc_stm32_uart_clear_tx(dc_stm32_uart_t *p);
typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port,*miso_port;
    uint16_t cs_pin,miso_pin;
    uint32_t ready_timeout_ms;
    uint32_t configured_cr1;
} dc_stm32_radio_bus_t;
dc_radio_port_t dc_stm32_radio_port(dc_stm32_radio_bus_t *bus);
#endif
