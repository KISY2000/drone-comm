#ifndef DC_ZYNQ_BOARD_CONFIG_H
#define DC_ZYNQ_BOARD_CONFIG_H
#include "xparameters.h"
#include "dc_link_config.h"
#ifndef DC_ZYNQ_BOARD_READY
#define DC_ZYNQ_BOARD_READY 0
#endif
/* Physical UART identity, not the order of BSP device IDs. */
#ifndef XPAR_PS7_UART_0_BASEADDR
#error "BSP must enable PS UART0 on MIO14/15 for communication"
#endif
#define DC_ZYNQ_COMM_BASE XPAR_PS7_UART_0_BASEADDR
#if defined(XPAR_PS7_UART_0_INTR)
#define DC_ZYNQ_COMM_IRQ XPAR_PS7_UART_0_INTR
#else
#error "Regenerate BSP with the physical PS UART0 interrupt"
#endif
#if defined(STDOUT_BASEADDRESS) || defined(STDIN_BASEADDRESS)
#error "Communication BSP requires stdin=none and stdout=none; UART0 is protocol-only"
#endif
/* Board-ready requires P5 links 1-3/2-4 removed, correct MCU-side TTL
 * wiring and electrical checks. UART1/PL/EMIO is not required for this link. */
#endif
