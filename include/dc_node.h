#ifndef DC_NODE_H
#define DC_NODE_H
#include "dc_protocol.h"
#define DC_HEARTBEAT_MS 500u
#define DC_LINK_TIMEOUT_MS 1500u
#define DC_POLL_MS 100u
#define DC_TRANSACTION_MS 200u
typedef bool (*dc_send_fn)(void *user,const dc_frame_t *frame);
typedef struct {
 bool ready,pending;uint32_t session,boot_nonce,challenge,last_seen,handshake_at,handshake_started;
 dc_seq_t rx;
} dc_peer_t;
typedef struct {
 uint8_t id;bool ready,poll_enabled,request_pending,cached_valid,binding_pending;
 uint32_t boot_nonce,session,hub_session,nonce_state,challenge;
 uint32_t retired_hubs[8];uint8_t retired_next;
 uint32_t binding_at;
 uint32_t last_hello,last_heartbeat,last_poll,request_at,last_telemetry,last_video;
 uint16_t tx_seq[DC_NODE_COUNT],request_seq,cached_request_seq;
 uint8_t request_type;
 dc_peer_t peers[DC_NODE_COUNT];dc_frame_t cached_reply;
 dc_frame_t latest_telemetry,latest_video_status,latest_radio_state,latest_system_status;
 dc_send_fn send;void *user;
 uint32_t telemetry_rx,video_rx,test_rx,forwarded,rejected,unsupported,tx_dropped,request_timeouts;
 bool telemetry_valid,video_valid;
} dc_node_t;
/* HUB: session_seed must be a fresh nonzero random boot token (hardware RNG).
 * Other nodes: seed ignored. F103 may use UID plus a local startup token;
 * binding freshness comes from the HUB's new challenge, not a flash counter.
 * tick/receive run in main context. The send callback must enqueue, not block. */
void dc_node_init(dc_node_t *n,uint8_t id,uint32_t boot_nonce,uint32_t session_seed,dc_send_fn send,void *user);
void dc_node_tick(dc_node_t *n,uint32_t now_ms);
void dc_node_receive(dc_node_t *n,const dc_frame_t *frame,uint32_t now_ms);
bool dc_node_request(dc_node_t *n,uint8_t type,const uint8_t *payload,uint16_t len,uint32_t now_ms);
bool dc_node_peer_healthy(const dc_node_t *n,uint8_t peer,uint32_t now_ms);
#endif
