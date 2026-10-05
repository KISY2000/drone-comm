#ifndef DC_RADIO_H
#define DC_RADIO_H
#include "dc_protocol.h"

#define DC_RF_GROUND_ADDRESS 1u
#define DC_RF_AIR_ADDRESS 2u
#define DC_CC1101_PKTLEN (DC_RAW_MAX + 1u)
#define DC_RF_TX_TIMEOUT_MS 20u
#define DC_RF_REPLY_TIMEOUT_MS 30u
#define DC_RF_DEADLINE_MS 200u
#define DC_RF_QUERY_PERIOD_MS 100u

/* All operations run in main context. The adapter owns CSN and bounded MISO-ready
 * waits. Status register reads must use the CC1101 burst/status-read bit. */
typedef struct {
    void *ctx;
    bool (*read_reg)(void *ctx, uint8_t address, uint8_t *value);
    bool (*write_reg)(void *ctx, uint8_t address, uint8_t value);
    bool (*read_burst)(void *ctx, uint8_t address, uint8_t *data, size_t count);
    bool (*write_burst)(void *ctx, uint8_t address, const uint8_t *data, size_t count);
    bool (*strobe)(void *ctx, uint8_t command, uint8_t *status);
} dc_radio_port_t;

typedef enum { DC_RADIO_OFF, DC_RADIO_CALIBRATING, DC_RADIO_RX_START,
               DC_RADIO_RX, DC_RADIO_TX, DC_RADIO_FAULT } dc_radio_state_t;
typedef enum { DC_RF_TXN_IDLE, DC_RF_TXN_TX, DC_RF_TXN_WAIT_REPLY,
               DC_RF_TXN_BACKOFF } dc_radio_transaction_state_t;
typedef enum { DC_RF_COMPLETE, DC_RF_TIMEOUT, DC_RF_IO_ERROR } dc_radio_completion_t;
typedef void (*dc_radio_receive_fn)(void *user, const dc_frame_t *frame);
typedef void (*dc_radio_transaction_fn)(void *user, dc_radio_completion_t result,
                                      const dc_frame_t *request, const dc_frame_t *reply);
typedef struct {
    uint32_t rx_valid, rx_hw_crc, rx_app_crc, rx_invalid, rx_overflow;
    uint32_t tx_ok, tx_timeout, tx_underflow, retries, transaction_ok;
    uint32_t transaction_timeout, io_errors, recoveries, rx_partial_timeout;
    uint32_t rx_sync_timeout, calibration_timeout, rx_start_timeout, status_unstable;
} dc_radio_stats_t;
typedef struct {
    dc_radio_port_t port;
    dc_radio_receive_fn on_frame;
    dc_radio_transaction_fn on_transaction;
    void *user;
    dc_radio_state_t state;
    dc_radio_transaction_state_t transaction;
    dc_radio_stats_t stats;
    dc_frame_t request;
    uint32_t state_deadline, transaction_deadline, reply_deadline, retry_at;
    uint32_t reply_session;
    uint32_t last_rx_ms;
    int16_t last_rssi_x2_dbm;
    uint8_t last_lqi;
    bool rssi_valid;
    uint32_t partial_deadline, tx_event_start, gdo_ignored_event;
    /* ISR is the sole writer. Low bit is level; upper bits are event generation.
     * Main clears stale/aborted events by recording a snapshot, never by writing
     * this word. Aligned 32-bit accesses are atomic on the supported ARM CPUs. */
    volatile uint32_t gdo_level_event;
    volatile uint32_t gdo_falling_events;
    uint8_t local_address, peer_address, attempts, rx_length;
    bool tracked_tx, rx_sync_pending;
} dc_radio_t;

/* Initialization checks chip identity, programs and reads back the profile.
 * Calibration and RX readiness are nonblocking and progress in tick(). */
bool dc_radio_init(dc_radio_t *radio, const dc_radio_port_t *port,
                   uint8_t local_address, dc_radio_receive_fn on_frame,
                   dc_radio_transaction_fn on_transaction, void *user, uint32_t now_ms);
void dc_radio_tick(dc_radio_t *radio, uint32_t now_ms);
/* ISR-safe marker only: no SPI, parsing, callback, or recovery here. */
void dc_radio_gdo_event(dc_radio_t *radio, bool high);
bool dc_radio_ready(const dc_radio_t *radio);
bool dc_radio_transaction_active(const dc_radio_t *radio);
/* Ground bridge learns AIR's separately assigned session from HELLO_ACK
 * payload+8. A changed binding cancels old transactions and RX/TX contents.
 * Use zero to invalidate the binding on a new handshake. */
void dc_radio_set_reply_session(dc_radio_t *radio, uint32_t session, uint32_t now_ms);
/* Neither API queues. False means busy, invalid input, or unavailable radio.
 * Send untracked supports HELLO/ACK, heartbeat, and AIR replies. Every TX still
 * has a 20 ms watchdog. A tracked request accepts only QUERY or TEST_REQUEST
 * after a nonzero reply-session binding has been installed. */
bool dc_radio_send_untracked(dc_radio_t *radio, const dc_frame_t *frame,
                             uint8_t peer_address, uint32_t now_ms);
bool dc_radio_request(dc_radio_t *radio, const dc_frame_t *frame,
                      uint8_t peer_address, uint32_t now_ms);
void dc_radio_cancel(dc_radio_t *radio, uint32_t now_ms);
#endif
