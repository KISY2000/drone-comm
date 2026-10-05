#ifndef DC_STM32_APP_H
#define DC_STM32_APP_H
#include "dc_stm32_port.h"
#include "dc_node.h"
typedef struct {
    dc_node_t node;
    dc_stm32_uart_t ports[2];
    dc_parser_t parsers[2];
    dc_radio_t radio;
    dc_radio_port_t rf_port;
    dc_queue_t rf_tx;
    uint32_t rf_enqueued[DC_QUEUE_CAPACITY];
    uint32_t local_session;
    uint32_t air_hub_session,air_challenge;
    uint32_t air_retired_hubs[8],air_binding_at;
    uint8_t air_retired_next;
    bool air_binding_pending;
    uint32_t air_expected_reply_session;
    uint32_t rf_reinit_at,rf_reinit_count,rf_reinit_failures;
    uint8_t rf_local_address;
    uint8_t port_count;
    bool has_radio;
    uint32_t rf_expired;
    uint32_t binding_replays;
} dc_stm32_app_t;
/* Call after HAL/GPIO/clock/peripherals initialization, only if board ready.
 * HUB seed must come from HAL_RNG_GenerateRandomNumber(), fail closed on error.
 * Ports: F103 [USART2]; HUB [USART3 PB10/11, USART2 PA2/3]; AIR []. */
bool dc_stm32_app_init(dc_stm32_app_t *app,uint8_t id,uint32_t boot_nonce,
                      uint32_t seed,UART_HandleTypeDef *a,UART_HandleTypeDef *b,
                      const dc_radio_port_t *radio);
void dc_stm32_app_poll(dc_stm32_app_t *app);
void dc_stm32_app_rx_irq(dc_stm32_app_t *app,UART_HandleTypeDef *uart);
void dc_stm32_app_tx_irq(dc_stm32_app_t *app,UART_HandleTypeDef *uart);
void dc_stm32_app_uart_error_irq(dc_stm32_app_t *app,UART_HandleTypeDef *uart);
#endif
