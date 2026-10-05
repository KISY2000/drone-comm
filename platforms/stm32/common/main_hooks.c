/* Add this file to CubeMX generated main project. Do not duplicate its HAL callbacks.
 * USART ReceiveToIdle uses circular DMA with HT+TC enabled. UART/DMA IRQs priority 5.
 * GPIO/clock/DMA/UART/SPI/RNG setup is generated from the templates and checklist. */
#include "dc_stm32_app.h"
#include "board_config.h"
static dc_stm32_app_t app;
bool dc_firmware_started;
#if defined(DC_ROLE_GROUND_F103)
extern UART_HandleTypeDef huart2;
extern SPI_HandleTypeDef hspi1;
static dc_stm32_radio_bus_t bus;
#elif defined(DC_ROLE_GROUND_F407)
extern UART_HandleTypeDef huart3,huart2;
extern RNG_HandleTypeDef hrng;
#else
extern dc_stm32_radio_bus_t *dc_air_radio_bus(void);
extern uint16_t dc_air_gdo0_pin(void);
#endif
bool dc_firmware_start(void) {
    uint32_t nonce=HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetTick();
    if(!DC_BOARD_READY) return false;
#if defined(DC_ROLE_GROUND_F103)
    dc_radio_port_t rf;
    bus.spi=&hspi1; bus.cs_port=DC_F103_CSN_PORT; bus.cs_pin=DC_F103_CSN_PIN;
    bus.miso_port=DC_F103_MISO_PORT; bus.miso_pin=DC_F103_MISO_PIN; bus.ready_timeout_ms=2;
    HAL_GPIO_WritePin(bus.cs_port,bus.cs_pin,GPIO_PIN_SET);
    rf=dc_stm32_radio_port(&bus);
    dc_firmware_started=dc_stm32_app_init(&app,DC_RADIO_GROUND,nonce,0,&huart2,0,&rf);
#elif defined(DC_ROLE_GROUND_F407)
    uint32_t seed=0;
    if(HAL_RNG_GenerateRandomNumber(&hrng,&seed)!=HAL_OK || !seed) return false;
    dc_firmware_started=dc_stm32_app_init(&app,DC_HUB,nonce,seed,&huart3,&huart2,0);
#else
    dc_stm32_radio_bus_t *b=dc_air_radio_bus(); dc_radio_port_t rf;
    if(!b || !b->spi || !b->cs_port || !b->miso_port || !b->cs_pin || !b->miso_pin) return false;
    HAL_GPIO_WritePin(b->cs_port,b->cs_pin,GPIO_PIN_SET); rf=dc_stm32_radio_port(b);
    dc_firmware_started=dc_stm32_app_init(&app,DC_AIR,nonce,0,0,0,&rf);
#endif
    return dc_firmware_started;
}
void dc_firmware_poll(void) { if(dc_firmware_started) dc_stm32_app_poll(&app); }
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *u,uint16_t size) {
    (void)size; if(dc_firmware_started) dc_stm32_app_rx_irq(&app,u);
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *u) {
    if(dc_firmware_started) dc_stm32_app_tx_irq(&app,u);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *u) {
    if(dc_firmware_started) dc_stm32_app_uart_error_irq(&app,u);
}
void HAL_GPIO_EXTI_Callback(uint16_t pin) {
#if defined(DC_ROLE_GROUND_F103)
    if(dc_firmware_started && pin==DC_F103_GDO0_PIN)
        dc_radio_gdo_event(&app.radio,HAL_GPIO_ReadPin(DC_F103_GDO0_PORT,pin)==GPIO_PIN_SET);
#elif defined(DC_ROLE_AIR_F407)
    /* Proposed PG6 input/EXTI6, implemented by the standalone AIR board file.
     * CubeMX integration must retain both-edge input and provide these helpers. */
    extern GPIO_TypeDef *dc_air_gdo0_port(void);
    if(dc_firmware_started && pin==dc_air_gdo0_pin())
        dc_radio_gdo_event(&app.radio,HAL_GPIO_ReadPin(dc_air_gdo0_port(),pin)==GPIO_PIN_SET);
#else
    (void)pin;
#endif
}
