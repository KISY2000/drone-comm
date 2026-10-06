#ifndef DC_PORT_HAL_MODEL_H
#define DC_PORT_HAL_MODEL_H
/* Host register/state fixture, never included by target builds. Only the DMA
 * and UART APIs used by the real port are modeled; SPI functions are stubs. */
#include <stdbool.h>
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { HAL_DMA_STATE_RESET, HAL_DMA_STATE_READY, HAL_DMA_STATE_BUSY,
               HAL_DMA_STATE_TIMEOUT } HAL_DMA_StateTypeDef;
typedef struct { volatile uint32_t CCR,CNDTR,CR,NDTR,FCR,flags; } DMA_Registers;
typedef struct {
    DMA_Registers *Instance;
    struct { uint32_t Mode; } Init;
    HAL_DMA_StateTypeDef State;
    void *Parent;
    bool hold_enable,callbacks_installed;
} DMA_HandleTypeDef;
typedef struct { volatile uint32_t CR1,CR3; } USART_TypeDef;
typedef struct {
    USART_TypeDef *Instance;
    DMA_HandleTypeDef *hdmarx;
    HAL_DMA_StateTypeDef RxState;
} UART_HandleTypeDef;
typedef struct { volatile uint32_t SR,CR1,DR; } SPI_TypeDef;
typedef struct {
    SPI_TypeDef *Instance;
    struct { uint32_t Mode,Direction,DataSize,CLKPolarity,CLKPhase,FirstBit,
                     NSS,TIMode,CRCCalculation; } Init;
} SPI_HandleTypeDef;
typedef struct { uint32_t unused; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET,GPIO_PIN_SET } GPIO_PinState;
#define DMA_CIRCULAR 0x100u
#define DMA_CCR_EN 1u
#define DMA_SxCR_EN 1u
#define DMA_IT_TC 2u
#define DMA_IT_HT 4u
#define DMA_IT_TE 8u
#define DMA_IT_DME 16u
#define DMA_IT_FE 128u
#define USART_CR1_PEIE 0x100u
#define USART_CR1_RXNEIE 0x20u
#define USART_CR1_IDLEIE 0x10u
#define USART_CR1_TXEIE 0x80u
#define USART_CR1_TCIE 0x40u
#define USART_CR3_EIE 1u
#define USART_CR3_DMAR 0x40u
#define CLEAR_BIT(reg,bits) ((reg)&=~(bits))
void dc_mock_dma_disable(DMA_HandleTypeDef *dma);
void dc_mock_dma_disable_it(DMA_HandleTypeDef *dma,uint32_t flags);
#define __HAL_DMA_DISABLE(dma) dc_mock_dma_disable(dma)
#define __HAL_DMA_DISABLE_IT(dma,flags) dc_mock_dma_disable_it(dma,flags)
#if defined(DC_STM32_F1)
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->Instance->CNDTR)
#else
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->Instance->NDTR)
#endif
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *dma);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *dma);
HAL_DMA_StateTypeDef HAL_DMA_GetState(DMA_HandleTypeDef *dma);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *uart,uint8_t *bytes,uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,uint8_t *bytes,uint16_t length);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
uint32_t HAL_GetTick(void);
extern uint32_t SystemCoreClock;
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio,uint16_t pin,GPIO_PinState value);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio,uint16_t pin);
#define __NOP() ((void)0)
#define SPI_SR_RXNE 0x01u
#define SPI_SR_TXE 0x02u
#define SPI_SR_MODF 0x20u
#define SPI_SR_OVR 0x40u
#define SPI_SR_BSY 0x80u
#define SPI_CR1_SPE 0x40u
#define SPI_CR1_MSTR 0x04u
#define SPI_MODE_MASTER 1u
#define SPI_DIRECTION_2LINES 2u
#define SPI_DATASIZE_8BIT 8u
#define SPI_POLARITY_LOW 0u
#define SPI_PHASE_1EDGE 0u
#define SPI_FIRSTBIT_MSB 0u
#define SPI_NSS_SOFT 1u
#define SPI_TIMODE_DISABLE 0u
#define SPI_CRCCALCULATION_DISABLE 0u
#endif
