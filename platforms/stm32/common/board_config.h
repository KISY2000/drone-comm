#ifndef DC_BOARD_CONFIG_H
#define DC_BOARD_CONFIG_H
#include "dc_link_config.h"
/* Default fails closed: keep 0 until the physical wiring and shared peripherals
 * listed in docs/board_config.md are checked. Compile-only is permitted. */
#ifndef DC_BOARD_READY
#define DC_BOARD_READY 0
#endif
#define DC_BOARD_HSE_HZ 8000000u
#define DC_UART_IRQ_PRIORITY 5u
#define DC_DMA_IRQ_PRIORITY 5u
#if defined(DC_ROLE_GROUND_F103)
#define DC_BOARD_NODE DC_RADIO_GROUND
#define DC_F103_CSN_PORT GPIOA
#define DC_F103_CSN_PIN GPIO_PIN_4
#define DC_F103_MISO_PORT GPIOA
#define DC_F103_MISO_PIN GPIO_PIN_6
#define DC_F103_GDO0_PORT GPIOB
#define DC_F103_GDO0_PIN GPIO_PIN_0
#define DC_F103_GDO2_PORT GPIOB
#define DC_F103_GDO2_PIN GPIO_PIN_1
#elif defined(DC_ROLE_GROUND_F407)
#define DC_BOARD_NODE DC_HUB
/* USART3 PB10/11 via Explorer P2; USART2 PA2/3 via P4 to Navigator P5.
 * PD3 holds the Ethernet PHY in reset; PC1 holds MDC low before UART init.
 * This does not certify PHY MDIO isolation: board jumpers and PA2/3 waveforms
 * must be checked before enabling BOARD_READY. No ETH/audio initialization. */
#elif defined(DC_ROLE_AIR_F407)
#define DC_BOARD_NODE DC_AIR
/* Proposed Explorer V3.4 NRF socket adapter; hardware wiring is not completed.
 * See docs/proposed_wiring.md. BOARD_READY remains 0 until that checklist passes.
 * SPI1 PB3/4/5 uses AF5, SWD-only debug; disable onboard Flash via PB14 high. */
#define DC_AIR_CSN_PORT GPIOG
#define DC_AIR_CSN_PIN GPIO_PIN_7
#define DC_AIR_MISO_PORT GPIOB
#define DC_AIR_MISO_PIN GPIO_PIN_4
#define DC_AIR_GDO0_PORT GPIOG
#define DC_AIR_GDO0_PIN GPIO_PIN_6
#define DC_AIR_FLASH_CSN_PORT GPIOB
#define DC_AIR_FLASH_CSN_PIN GPIO_PIN_14
/* NRF socket IRQ/PG8 shares RS485 RE/DE and is deliberately unused.
 * E07 GDO2 remains disconnected and high-impedance in the radio profile. */
#else
#error "Choose exactly one DC_ROLE_GROUND_F103/GROUND_F407/AIR_F407"
#endif
#endif
