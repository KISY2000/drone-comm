#ifndef DC_FIXTURE_XPARAMETERS_H
#define DC_FIXTURE_XPARAMETERS_H
#ifndef DC_COMPILE_FIXTURE
#error "Compile-only configuration, never a substitute for a generated hardware BSP"
#endif
/* Physical Zynq-7000 PS base addresses and IRQs are fixed by silicon. Clock is
 * only a compile fixture; real clocks and device table come from exported HDF. */
#define XPAR_XUARTPS_NUM_INSTANCES 1
#define XPAR_PS7_UART_0_BASEADDR 0xE0000000U
#define XPAR_PS7_UART_0_INTR 59U
#define XPAR_CPU_CORTEXA9_CORE_CLOCK_FREQ_HZ 666666666U
#endif
