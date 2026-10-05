#ifndef DC_ZYNQ_LOG_H
#define DC_ZYNQ_LOG_H
#include <stddef.h>
#include <stdint.h>
#define DC_ZYNQ_LOG_CAPACITY 512u
#define DC_ZYNQ_LOG_CALL_BUDGET 64u
/* Single writer in main context only. JTAG can inspect these symbols.
 * head is the next slot; count is retained bytes; overwritten counts eviction.
 * The byte ring is not NUL terminated. No UART, timer or allocation is used. */
typedef struct {
    volatile uint8_t bytes[DC_ZYNQ_LOG_CAPACITY];
    volatile uint32_t head, count, overwritten;
} dc_zynq_debug_log_t;
extern dc_zynq_debug_log_t dc_zynq_debug_log;
/* NULL is ignored; long messages truncate after 64 bytes. */
size_t dc_zynq_log(const char *text);
#endif
