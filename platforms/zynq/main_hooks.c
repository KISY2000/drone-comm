#include "dc_zynq_port.h"
#include "dc_node.h"
#include "board_config.h"
#include "xtime_l.h"
static dc_zynq_port_t port;
static dc_parser_t parser;
static dc_node_t node;
static bool started;
static uint32_t session;
static bool send_frame(void *ctx,const dc_frame_t *frame) {
    (void)ctx;
    if(node.session!=session) { dc_zynq_port_clear(&port); session=node.session; }
    return dc_zynq_port_send(&port,frame);
}
/* Call with initialized GIC; platform owns exception vector registration.
 * Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
 *      (Xil_ExceptionHandler)XScuGic_InterruptHandler,gic);
 * Xil_ExceptionEnable(); after this start succeeds. */
bool dc_zynq_firmware_start(XScuGic *gic) {
    XTime boot;
    if(!DC_ZYNQ_BOARD_READY || !dc_zynq_port_init(&port,gic)) return false;
    XTime_GetTime(&boot); session=0; dc_parser_init(&parser);
    dc_node_init(&node,DC_ZYNQ,(uint32_t)boot,0,send_frame,0);
    started=true; (void)dc_zynq_log("drone_comm UART0 MIO14/15 ready\r\n"); return true;
}
void dc_zynq_firmware_poll(void) {
    unsigned budget=128; uint8_t byte; dc_frame_t frame; int rc;
    uint32_t now=dc_zynq_now_ms();
    if(!started) return;
    while(budget--) {
        rc=dc_zynq_port_read(&port,&byte);
        if(rc<0) { dc_parser_init(&parser); parser.dropping=true; break; }
        if(!rc) break;
        if(dc_parser_feed(&parser,byte,&frame)) dc_node_receive(&node,&frame,now);
    }
    dc_node_tick(&node,now); dc_zynq_port_service(&port);
}
