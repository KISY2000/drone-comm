/* SDK 2017.4 standalone entry. PS initialization comes from the hardware
 * handoff/debug launch or bootloader. BOARD_READY remains zero until wiring,
 * device, voltage and the actual hardware image have been confirmed. */
#include "dc_zynq_port.h"
#include "xil_exception.h"
#include "xstatus.h"
#include "xparameters.h"
static XScuGic interrupts;
/* Protocol-only UART: deterministic stdio sinks override the SDK 2017.4
 * no-device inbyte (which has no return statement). Never touch a UART here. */
void outbyte(char c) { (void)c; }
char inbyte(void) { return 0; }
int main(void) {
    XScuGic_Config *cfg=XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
    if(!cfg || XScuGic_CfgInitialize(&interrupts,cfg,cfg->CpuBaseAddress)!=XST_SUCCESS)
        return 1;
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
        (Xil_ExceptionHandler)XScuGic_InterruptHandler,&interrupts);
    if(!dc_zynq_firmware_start(&interrupts)) return 2;
    Xil_ExceptionEnable();
    for(;;) dc_zynq_firmware_poll();
}
