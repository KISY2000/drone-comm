#ifndef DC_ZYNQ_PORT_H
#define DC_ZYNQ_PORT_H
#include "dc_protocol.h"
#include "dc_byte_ring.h"
#include "dc_zynq_log.h"
#include "xuartps.h"
#include "xscugic.h"
typedef struct {
    XUartPs uart;
    dc_byte_ring_t rx;
    dc_queue_t tx;
    uint8_t tx_bytes[DC_UART_MAX];
    size_t tx_length,tx_offset;
    uint32_t errors;
    uint32_t tx_started,tx_timeouts;
    bool tx_active;
} dc_zynq_port_t;
bool dc_zynq_port_init(dc_zynq_port_t *port,XScuGic *gic);
bool dc_zynq_port_send(dc_zynq_port_t *port,const dc_frame_t *frame);
void dc_zynq_port_service(dc_zynq_port_t *port);
int dc_zynq_port_read(dc_zynq_port_t *port,uint8_t *byte);
void dc_zynq_port_clear(dc_zynq_port_t *port);
uint32_t dc_zynq_now_ms(void);
bool dc_zynq_firmware_start(XScuGic *gic);
void dc_zynq_firmware_poll(void);
#endif
