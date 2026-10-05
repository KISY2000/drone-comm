#include "dc_zynq_log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Host linking deliberately supplies no UART/BSP backend: diagnostic logging
 * must remain usable without touching the UART carrying protocol frames. */
int main(void) {
    char message[100];
    unsigned char history[4096];
    size_t total = 0, round, i;
    memset(&dc_zynq_debug_log, 0, sizeof dc_zynq_debug_log);
    assert(dc_zynq_log(NULL) == 0);
    assert(dc_zynq_log("") == 0);
    assert(dc_zynq_debug_log.count == 0);
    for (round = 0; round < 50; ++round) {
        size_t length = (round * 17u + 3u) % 99u;
        size_t accepted = length < DC_ZYNQ_LOG_CALL_BUDGET ? length : DC_ZYNQ_LOG_CALL_BUDGET;
        size_t retained, start;
        for (i = 0; i < length; ++i) message[i] = (char)('!' + (round + i) % 90u);
        message[length] = '\0';
        assert(dc_zynq_log(message) == accepted);
        memcpy(history + total, message, accepted);
        total += accepted;
        retained = total < DC_ZYNQ_LOG_CAPACITY ? total : DC_ZYNQ_LOG_CAPACITY;
        assert(dc_zynq_debug_log.head < DC_ZYNQ_LOG_CAPACITY);
        assert(dc_zynq_debug_log.count == retained);
        assert(dc_zynq_debug_log.overwritten == total - retained);
        start = (dc_zynq_debug_log.head + DC_ZYNQ_LOG_CAPACITY - retained) % DC_ZYNQ_LOG_CAPACITY;
        for (i = 0; i < retained; ++i)
            assert(dc_zynq_debug_log.bytes[(start + i) % DC_ZYNQ_LOG_CAPACITY] == history[total - retained + i]);
    }
    assert(total > 3u * DC_ZYNQ_LOG_CAPACITY);
    puts("RAM log: no UART backend, bounded append/truncation, chronological eviction and repeated wrap PASS");
    return 0;
}
