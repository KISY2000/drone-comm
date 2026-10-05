#include "dc_zynq_log.h"
dc_zynq_debug_log_t dc_zynq_debug_log;
size_t dc_zynq_log(const char *text) {
    size_t n=0;
    if(!text) return 0;
    while(n<DC_ZYNQ_LOG_CALL_BUDGET && text[n]) {
        dc_zynq_debug_log.bytes[dc_zynq_debug_log.head]=(uint8_t)text[n++];
        dc_zynq_debug_log.head=(dc_zynq_debug_log.head+1u)%DC_ZYNQ_LOG_CAPACITY;
        if(dc_zynq_debug_log.count<DC_ZYNQ_LOG_CAPACITY) dc_zynq_debug_log.count++;
        else dc_zynq_debug_log.overwritten++;
    }
    return n;
}
