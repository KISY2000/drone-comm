#ifndef DC_LINK_CONFIG_H
#define DC_LINK_CONFIG_H

/* All three wired nodes must use the same build setting. Start board bring-up
 * at 115200, then rebuild every wired node with DC_UART_BAUD=460800 after the
 * jumper/isolation and waveform checks. RF modulation is independent. */
#ifndef DC_UART_BAUD
#define DC_UART_BAUD 115200u
#endif
#if DC_UART_BAUD != 115200u && DC_UART_BAUD != 460800u
#error "DC_UART_BAUD must be 115200 or 460800 on every wired node"
#endif

#endif
