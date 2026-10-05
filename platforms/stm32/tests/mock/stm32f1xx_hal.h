#ifndef DC_TEST_MOCK_HAL_H
#define DC_TEST_MOCK_HAL_H
/* Host behavioral mock only. tools/check_platforms.py NEVER includes this path. */
#include <stdint.h>
typedef struct { unsigned unused; } UART_HandleTypeDef;
typedef struct { unsigned unused; } SPI_HandleTypeDef;
typedef struct { unsigned unused; } GPIO_TypeDef;
uint32_t HAL_GetTick(void);
#endif
