/* Standalone communication-only HAL initialization. Use this OR CubeMX's
 * generated peripheral/MSP/IRQ code; never link both sets of definitions.
 * Final executable additionally needs vendor startup/system/linker files. */
#include "dc_stm32_port.h"
#include "board_config.h"
#if defined(DC_ROLE_GROUND_F103)
UART_HandleTypeDef huart2;
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_usart2_rx;
static bool setup_clock(void) {
    RCC_OscInitTypeDef osc={0}; RCC_ClkInitTypeDef clk={0};
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE; osc.HSEState=RCC_HSE_ON;
    osc.HSEPredivValue=RCC_HSE_PREDIV_DIV1; osc.PLL.PLLState=RCC_PLL_ON;
    osc.PLL.PLLSource=RCC_PLLSOURCE_HSE; osc.PLL.PLLMUL=RCC_PLL_MUL9;
    if(HAL_RCC_OscConfig(&osc)!=HAL_OK) return false;
    clk.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK; clk.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider=RCC_HCLK_DIV2; clk.APB2CLKDivider=RCC_HCLK_DIV1;
    return HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_2)==HAL_OK;
}
bool dc_board_peripherals_init(void) {
    GPIO_InitTypeDef gpio={0};
    if(!DC_BOARD_READY) return false;
    HAL_Init(); if(!setup_clock()) return false;
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_AFIO_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_SET);
    gpio.Pin=GPIO_PIN_4; gpio.Mode=GPIO_MODE_OUTPUT_PP; gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA,&gpio);
    gpio.Pin=GPIO_PIN_0; gpio.Mode=GPIO_MODE_IT_RISING_FALLING; gpio.Pull=GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB,&gpio);
    /* IOCFG2 high-impedance: reserve GDO2 without a floating EXTI source. */
    gpio.Pin=GPIO_PIN_1; gpio.Mode=GPIO_MODE_INPUT; gpio.Pull=GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB,&gpio);
    HAL_NVIC_SetPriority(EXTI0_IRQn,DC_UART_IRQ_PRIORITY,0); HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    huart2.Instance=USART2; huart2.Init.BaudRate=DC_UART_BAUD;
    huart2.Init.WordLength=UART_WORDLENGTH_8B; huart2.Init.StopBits=UART_STOPBITS_1;
    huart2.Init.Parity=UART_PARITY_NONE; huart2.Init.Mode=UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl=UART_HWCONTROL_NONE; huart2.Init.OverSampling=UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&huart2)!=HAL_OK) return false;
    hspi1.Instance=SPI1; hspi1.Init.Mode=SPI_MODE_MASTER; hspi1.Init.Direction=SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize=SPI_DATASIZE_8BIT; hspi1.Init.CLKPolarity=SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase=SPI_PHASE_1EDGE; hspi1.Init.NSS=SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_32; hspi1.Init.FirstBit=SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode=SPI_TIMODE_DISABLE; hspi1.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial=7; return HAL_SPI_Init(&hspi1)==HAL_OK;
}
void HAL_UART_MspInit(UART_HandleTypeDef *u) {
    GPIO_InitTypeDef gpio={0};
    if(u->Instance!=USART2) return;
    __HAL_RCC_USART2_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin=GPIO_PIN_2; gpio.Mode=GPIO_MODE_AF_PP; gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA,&gpio); gpio.Pin=GPIO_PIN_3; gpio.Mode=GPIO_MODE_INPUT;
    gpio.Pull=GPIO_NOPULL; HAL_GPIO_Init(GPIOA,&gpio);
    hdma_usart2_rx.Instance=DMA1_Channel6; hdma_usart2_rx.Init.Direction=DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc=DMA_PINC_DISABLE; hdma_usart2_rx.Init.MemInc=DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode=DMA_CIRCULAR; hdma_usart2_rx.Init.Priority=DMA_PRIORITY_HIGH;
    if(HAL_DMA_Init(&hdma_usart2_rx)!=HAL_OK) return;
    __HAL_LINKDMA(u,hdmarx,hdma_usart2_rx);
    HAL_NVIC_SetPriority(DMA1_Channel6_IRQn,DC_DMA_IRQ_PRIORITY,0); HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn,DC_UART_IRQ_PRIORITY,0); HAL_NVIC_EnableIRQ(USART2_IRQn);
}
void HAL_SPI_MspInit(SPI_HandleTypeDef *s) {
    GPIO_InitTypeDef gpio={0}; if(s->Instance!=SPI1) return;
    __HAL_RCC_SPI1_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin=GPIO_PIN_5|GPIO_PIN_7; gpio.Mode=GPIO_MODE_AF_PP; gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA,&gpio); gpio.Pin=GPIO_PIN_6; gpio.Mode=GPIO_MODE_INPUT;
    gpio.Pull=GPIO_NOPULL; HAL_GPIO_Init(GPIOA,&gpio);
}
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
void DMA1_Channel6_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart2_rx); }
void EXTI0_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
#elif defined(DC_ROLE_GROUND_F407)
UART_HandleTypeDef huart3,huart2;
RNG_HandleTypeDef hrng;
DMA_HandleTypeDef hdma_usart3_rx,hdma_usart2_rx;
static bool setup_clock(void) {
    RCC_OscInitTypeDef osc={0}; RCC_ClkInitTypeDef clk={0};
    __HAL_RCC_PWR_CLK_ENABLE(); __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE; osc.HSEState=RCC_HSE_ON;
    osc.PLL.PLLState=RCC_PLL_ON; osc.PLL.PLLSource=RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM=8; osc.PLL.PLLN=336; osc.PLL.PLLP=RCC_PLLP_DIV2; osc.PLL.PLLQ=7;
    if(HAL_RCC_OscConfig(&osc)!=HAL_OK) return false;
    clk.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK; clk.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider=RCC_HCLK_DIV4; clk.APB2CLKDivider=RCC_HCLK_DIV2;
    return HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_5)==HAL_OK;
}
static bool setup_uart(UART_HandleTypeDef *u,USART_TypeDef *instance) {
    u->Instance=instance; u->Init.BaudRate=DC_UART_BAUD;
    u->Init.WordLength=UART_WORDLENGTH_8B; u->Init.StopBits=UART_STOPBITS_1;
    u->Init.Parity=UART_PARITY_NONE; u->Init.Mode=UART_MODE_TX_RX;
    u->Init.HwFlowCtl=UART_HWCONTROL_NONE; u->Init.OverSampling=UART_OVERSAMPLING_16;
    return HAL_UART_Init(u)==HAL_OK;
}
bool dc_board_peripherals_init(void) {
    GPIO_InitTypeDef gpio={0};
    if(!DC_BOARD_READY) return false;
    HAL_Init(); if(!setup_clock()) return false;
    /* PA2 also reaches PHY MDIO. Hold PHY reset and MDC low before selecting
     * USART2; never enable ETH/audio here. Physical isolation and electrical
     * loading still need verification; this is not a PHY high-Z guarantee. */
    __HAL_RCC_GPIOD_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOD,GPIO_PIN_3,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_1,GPIO_PIN_RESET);
    gpio.Pin=GPIO_PIN_3; gpio.Mode=GPIO_MODE_OUTPUT_PP; gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_LOW; HAL_GPIO_Init(GPIOD,&gpio);
    gpio.Pin=GPIO_PIN_1; HAL_GPIO_Init(GPIOC,&gpio);
    if(!setup_uart(&huart3,USART3) || !setup_uart(&huart2,USART2)) return false;
    __HAL_RCC_RNG_CLK_ENABLE(); hrng.Instance=RNG; return HAL_RNG_Init(&hrng)==HAL_OK;
}
void HAL_UART_MspInit(UART_HandleTypeDef *u) {
    GPIO_InitTypeDef gpio={0}; DMA_HandleTypeDef *d; IRQn_Type di,ui;
    gpio.Mode=GPIO_MODE_AF_PP; gpio.Pull=GPIO_NOPULL; gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
    if(u->Instance==USART3) {
        __HAL_RCC_USART3_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
        gpio.Pin=GPIO_PIN_10|GPIO_PIN_11; gpio.Alternate=GPIO_AF7_USART3; HAL_GPIO_Init(GPIOB,&gpio);
        d=&hdma_usart3_rx; d->Instance=DMA1_Stream1; d->Init.Channel=DMA_CHANNEL_4;
        di=DMA1_Stream1_IRQn; ui=USART3_IRQn;
    } else if(u->Instance==USART2) {
        __HAL_RCC_USART2_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin=GPIO_PIN_2|GPIO_PIN_3; gpio.Alternate=GPIO_AF7_USART2; HAL_GPIO_Init(GPIOA,&gpio);
        d=&hdma_usart2_rx; d->Instance=DMA1_Stream5; d->Init.Channel=DMA_CHANNEL_4;
        di=DMA1_Stream5_IRQn; ui=USART2_IRQn;
    } else return;
    d->Init.Direction=DMA_PERIPH_TO_MEMORY; d->Init.PeriphInc=DMA_PINC_DISABLE;
    d->Init.MemInc=DMA_MINC_ENABLE; d->Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;
    d->Init.MemDataAlignment=DMA_MDATAALIGN_BYTE; d->Init.Mode=DMA_CIRCULAR;
    d->Init.Priority=DMA_PRIORITY_HIGH; d->Init.FIFOMode=DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(d)!=HAL_OK) return;
    u->hdmarx=d; d->Parent=u;
    HAL_NVIC_SetPriority(di,DC_DMA_IRQ_PRIORITY,0); HAL_NVIC_EnableIRQ(di);
    HAL_NVIC_SetPriority(ui,DC_UART_IRQ_PRIORITY,0); HAL_NVIC_EnableIRQ(ui);
}
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&huart3); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
void DMA1_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart3_rx); }
void DMA1_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart2_rx); }
#elif defined(DC_ROLE_AIR_F407)
/* Proposed Explorer V3.4 wiring, not an assertion that E07 is already connected.
 * User must complete docs/proposed_wiring.md before enabling BOARD_READY. */
SPI_HandleTypeDef hspi1;
static dc_stm32_radio_bus_t air_bus;
static bool setup_clock(void) {
    RCC_OscInitTypeDef osc={0}; RCC_ClkInitTypeDef clk={0};
    __HAL_RCC_PWR_CLK_ENABLE(); __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE; osc.HSEState=RCC_HSE_ON;
    osc.PLL.PLLState=RCC_PLL_ON; osc.PLL.PLLSource=RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM=8; osc.PLL.PLLN=336; osc.PLL.PLLP=RCC_PLLP_DIV2; osc.PLL.PLLQ=7;
    if(HAL_RCC_OscConfig(&osc)!=HAL_OK) return false;
    clk.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK; clk.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider=RCC_HCLK_DIV4; clk.APB2CLKDivider=RCC_HCLK_DIV2;
    return HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_5)==HAL_OK;
}
bool dc_board_peripherals_init(void) {
    GPIO_InitTypeDef gpio={0};
    if(!DC_BOARD_READY) return false;
    HAL_Init(); if(!setup_clock()) return false;
    __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOG_CLK_ENABLE(); __HAL_RCC_SYSCFG_CLK_ENABLE();
    /* Deselect the shared 25Q128 before any SPI clock toggles. No Flash driver.
     * Deassert E07 CS before changing the output mode, avoiding a low glitch. */
    HAL_GPIO_WritePin(DC_AIR_FLASH_CSN_PORT,DC_AIR_FLASH_CSN_PIN,GPIO_PIN_SET);
    gpio.Pin=DC_AIR_FLASH_CSN_PIN; gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_PULLUP; gpio.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DC_AIR_FLASH_CSN_PORT,&gpio);
    HAL_GPIO_WritePin(DC_AIR_CSN_PORT,DC_AIR_CSN_PIN,GPIO_PIN_SET);
    gpio.Pin=DC_AIR_CSN_PIN; gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_PULLUP; gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(DC_AIR_CSN_PORT,&gpio);
    /* Socket CE becomes an input from CC1101 GDO0. Keep SWD PA13/14; do not
     * enable JTAG/SWV on PB3/4. AF5 below explicitly selects SPI1 on those pins. */
    CLEAR_BIT(DBGMCU->CR,DBGMCU_CR_TRACE_IOEN);
    gpio.Pin=DC_AIR_GDO0_PIN; gpio.Mode=GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull=GPIO_PULLDOWN; HAL_GPIO_Init(DC_AIR_GDO0_PORT,&gpio);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn,DC_UART_IRQ_PRIORITY,0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    hspi1.Instance=SPI1; hspi1.Init.Mode=SPI_MODE_MASTER; hspi1.Init.Direction=SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize=SPI_DATASIZE_8BIT; hspi1.Init.CLKPolarity=SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase=SPI_PHASE_1EDGE; hspi1.Init.NSS=SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_32; hspi1.Init.FirstBit=SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode=SPI_TIMODE_DISABLE; hspi1.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial=7;
    if(HAL_SPI_Init(&hspi1)!=HAL_OK) return false;
    air_bus.spi=&hspi1; air_bus.cs_port=DC_AIR_CSN_PORT; air_bus.cs_pin=DC_AIR_CSN_PIN;
    air_bus.miso_port=DC_AIR_MISO_PORT; air_bus.miso_pin=DC_AIR_MISO_PIN;
    air_bus.ready_timeout_ms=2u; air_bus.configured_cr1=0;
    return true;
}
void HAL_SPI_MspInit(SPI_HandleTypeDef *s) {
    GPIO_InitTypeDef gpio={0}; if(s->Instance!=SPI1) return;
    __HAL_RCC_SPI1_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin=GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5; gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_NOPULL; gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate=GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB,&gpio);
}
void EXTI9_5_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(DC_AIR_GDO0_PIN); }
dc_stm32_radio_bus_t *dc_air_radio_bus(void) { return &air_bus; }
uint16_t dc_air_gdo0_pin(void) { return DC_AIR_GDO0_PIN; }
GPIO_TypeDef *dc_air_gdo0_port(void) { return DC_AIR_GDO0_PORT; }
#endif
/* If using the vendor HAL timebase, add HAL_IncTick() to generated SysTick_Handler.
 * No duplicate SysTick handler here. */
