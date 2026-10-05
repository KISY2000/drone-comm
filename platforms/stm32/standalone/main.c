/* Standalone communication entry point. Link with the manual board file,
 * never CubeMX peripheral/MSP/IRQ definitions. BOARD_READY defaults to 0. */
#include "dc_stm32_app.h"

extern bool dc_board_peripherals_init(void);
extern bool dc_firmware_start(void);
extern void dc_firmware_poll(void);

/* Inspect this variable with a debugger after physical board configuration.
 * 0: entering, 1: board initialization stopped (also default disabled build),
 * 2: communication initialization failed, 3: polling communication. */
volatile uint32_t dc_startup_status;

static void stopped(uint32_t status) {
    dc_startup_status = status;
    for (;;) { __WFI(); }
}

int main(void) {
    if (!dc_board_peripherals_init()) { stopped(1u); }
    if (!dc_firmware_start()) { stopped(2u); }
    dc_startup_status = 3u;
    for (;;) { dc_firmware_poll(); }
}

void SysTick_Handler(void) {
    HAL_IncTick();
}
