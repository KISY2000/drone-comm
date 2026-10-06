/* Independent behavior review: malformed framing, stale sessions, restart,
 * duplicate traffic and bounded congestion. Build with dc_protocol.c/node.c. */
#include "dc_node.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(x) do { if (!(x)) { \
    printf("FAIL %s:%d: %s\n", __func__, __LINE__, #x); ++failures; \
} } while (0)

typedef struct {
    dc_frame_t frames[64];
    unsigned count;
    bool refuse;
} capture_t;

static bool capture_send(void *ctx, const dc_frame_t *frame) {
    capture_t *capture = (capture_t *)ctx;
    if (capture->refuse || capture->count >= 64u) return false;
    capture->frames[capture->count++] = *frame;
    return true;
}

static dc_frame_t frame(uint8_t src, uint8_t dst, uint8_t type,
                        uint32_t session, uint16_t seq) {
    dc_frame_t result;
    memset(&result, 0, sizeof(result));
    result.src = src; result.dst = dst; result.type = type;
    result.session = session; result.seq = seq;
    return result;
}

static dc_frame_t hello_to_air(uint32_t hub_session, uint32_t challenge) {
    dc_frame_t result = frame(DC_HUB, DC_AIR, DC_HELLO, hub_session, 0);
    result.len = 4;
    dc_put_u32(result.payload, challenge);
    return result;
}

static dc_frame_t ack_to_air(uint32_t boot_nonce, uint32_t hub_session,
                            uint32_t challenge, uint32_t assigned) {
    dc_frame_t result = frame(DC_HUB, DC_AIR, DC_HELLO_ACK, hub_session, 1);
    result.len = 12;
    dc_put_u32(result.payload, boot_nonce);
    dc_put_u32(result.payload + 4, challenge);
    dc_put_u32(result.payload + 8, assigned);
    return result;
}

static bool bind_pair(dc_node_t *hub, dc_node_t *air,
                       capture_t *hub_capture, capture_t *air_capture) {
    unsigned i;
    dc_frame_t probe;
    bool found = false;
    memset(hub_capture, 0, sizeof(*hub_capture));
    memset(air_capture, 0, sizeof(*air_capture));
    dc_node_init(hub, DC_HUB, 0x11u, 0x12345678u,
                 capture_send, hub_capture);
    dc_node_init(air, DC_AIR, 0x22u, 0, capture_send, air_capture);
    hub->poll_enabled = false;
    dc_node_tick(hub, 0);
    for (i = 0; i < hub_capture->count; ++i) {
        if (hub_capture->frames[i].dst == DC_AIR &&
            hub_capture->frames[i].type == DC_HELLO) {
            probe = hub_capture->frames[i]; found = true; break;
        }
    }
    if (!found) return false;
    dc_node_receive(air, &probe, 1);
    if (!air_capture->count) return false;
    hub_capture->count = 0;
    dc_node_receive(hub, &air_capture->frames[air_capture->count - 1u], 2);
    if (!hub_capture->count) return false;
    dc_node_receive(air, &hub_capture->frames[hub_capture->count - 1u], 3);
    return hub->peers[DC_AIR].ready && air->ready &&
           hub->peers[DC_AIR].session == air->session;
}

static void test_protocol_boundaries(void) {
    dc_frame_t input, output;
    dc_parser_t parser;
    dc_seq_t sequence;
    dc_queue_t queue;
    uint8_t bytes[DC_UART_MAX], raw[DC_RAW_MAX];
    size_t encoded, raw_length, i;
    unsigned length;
    uint32_t random = 1;
    CHECK(dc_crc16((const uint8_t *)"123456789", 9u) == 0x29b1u);
    for (length = 0; length <= DC_PAYLOAD_MAX; ++length) {
        input = frame(DC_AIR, DC_HUB, DC_TEST_RESULT, 0xff00ff00u, 65535);
        input.len = (uint16_t)length;
        for (i = 0; i < length; ++i) {
            random = random * 1664525u + 1013904223u;
            input.payload[i] = (i % 3u == 0) ? 0 : (uint8_t)(random >> 24);
        }
        raw_length = dc_encode_raw(&input, raw, sizeof(raw));
        CHECK(raw_length == 14u + length);
        CHECK(dc_encode_raw(&input, raw, raw_length - 1u) == 0);
        encoded = dc_encode_uart(&input, bytes, sizeof(bytes));
        CHECK(encoded == raw_length + 2u);
        CHECK(dc_encode_uart(&input, bytes, encoded - 1u) == 0);
        dc_parser_init(&parser);
        for (i = 0; i < encoded; ++i) {
            bool accepted = dc_parser_feed(&parser, bytes[i], &output);
            CHECK(accepted == (i + 1u == encoded));
        }
        CHECK(output.src == input.src && output.dst == input.dst);
        CHECK(output.session == input.session && output.seq == input.seq);
        CHECK(output.len == input.len &&
              memcmp(output.payload, input.payload, input.len) == 0);
    }
    dc_parser_init(&parser);
    CHECK(!dc_parser_feed(&parser, 5, &output));
    CHECK(!dc_parser_feed(&parser, 1, &output));
    CHECK(!dc_parser_feed(&parser, 0, &output));
    CHECK(parser.invalid == 1);
    for (i = 0; i < 100; ++i) CHECK(!dc_parser_feed(&parser, 0x55, &output));
    CHECK(!dc_parser_feed(&parser, 0, &output));
    CHECK(parser.oversize == 1);
    for (i = 0; i < encoded; ++i)
        CHECK(dc_parser_feed(&parser, bytes[i], &output) == (i + 1u == encoded));
    CHECK(parser.accepted == 1 && !parser.dropping);
    dc_seq_reset(&sequence);
    CHECK(dc_seq_accept(&sequence, 65534));
    CHECK(dc_seq_accept(&sequence, 0));
    CHECK(sequence.gaps == 1);
    CHECK(!dc_seq_accept(&sequence, 0));
    CHECK(!dc_seq_accept(&sequence, 65535));
    CHECK(!dc_seq_accept(&sequence, 32768));
    CHECK(sequence.last == 0 && sequence.duplicates == 1 && sequence.old == 2);
    dc_queue_init(&queue);
    for (i = 0; i < DC_QUEUE_CAPACITY; ++i) {
        input.seq = (uint16_t)i;
        CHECK(dc_queue_push(&queue, &input));
    }
    CHECK(!dc_queue_push(&queue, &input));
    CHECK(queue.count == DC_QUEUE_CAPACITY && queue.overflow == 1);
    for (i = 0; i < DC_QUEUE_CAPACITY; ++i) {
        CHECK(dc_queue_pop(&queue, &output));
        CHECK(output.seq == i);
    }
    CHECK(!dc_queue_pop(&queue, &output));
}

static void test_unsolicited_ack_and_zero_challenge(void) {
    dc_node_t air;
    capture_t capture;
    dc_frame_t incoming;
    memset(&capture, 0, sizeof(capture));
    dc_node_init(&air, DC_AIR, 0x22u, 0, capture_send, &capture);
    incoming = ack_to_air(0x22u, 0, 0, 0x99u);
    dc_node_receive(&air, &incoming, 1);
    CHECK(!air.ready && air.session == 0);
    CHECK(!air.peers[DC_HUB].ready);
    dc_node_init(&air, DC_AIR, 0x22u, 0, capture_send, &capture);
    incoming = hello_to_air(0x12345678u, 0);
    dc_node_receive(&air, &incoming, 2);
    CHECK(air.hub_session == 0 && air.challenge == 0 && !air.ready);
}

static void test_duplicate_ack_and_old_business(void) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    dc_frame_t incoming, ack;
    CHECK(bind_pair(&hub, &air, &hub_capture, &air_capture));
    ack = hub_capture.frames[hub_capture.count - 1u];
    incoming = frame(DC_HUB, DC_AIR, DC_HEARTBEAT, hub.session, 100);
    dc_node_receive(&air, &incoming, 100);
    CHECK(air.peers[DC_HUB].rx.last == 100);
    dc_node_receive(&air, &ack, 200);
    CHECK(air.peers[DC_HUB].rx.last == 100);
    CHECK(air.peers[DC_HUB].last_seen == 100);
    dc_node_receive(&air, &incoming, 201);
    incoming.seq = 99;
    dc_node_receive(&air, &incoming, 202);
    CHECK(air.peers[DC_HUB].last_seen == 100);
    CHECK(air.peers[DC_HUB].rx.duplicates == 1 &&
          air.peers[DC_HUB].rx.old == 1);
}

static void test_retired_hub_cannot_rebind_air(void) {
    dc_node_t air;
    capture_t capture;
    dc_frame_t hello_a, ack_a, hello_b, ack_b;
    memset(&capture, 0, sizeof(capture));
    dc_node_init(&air, DC_AIR, 0x22u, 0, capture_send, &capture);
    hello_a = hello_to_air(0xaaaa0001u, 0x11u);
    ack_a = ack_to_air(0x22u, 0xaaaa0001u, 0x11u, 0xaaaa0002u);
    hello_b = hello_to_air(0xbbbb0001u, 0x33u);
    ack_b = ack_to_air(0x22u, 0xbbbb0001u, 0x33u, 0xbbbb0002u);
    dc_node_receive(&air, &hello_a, 1);
    dc_node_receive(&air, &ack_a, 2);
    CHECK(air.ready && air.session == 0xaaaa0002u);
    dc_node_receive(&air, &hello_b, 3);
    dc_node_receive(&air, &ack_b, 4);
    CHECK(air.ready && air.session == 0xbbbb0002u);
    dc_node_receive(&air, &hello_a, 5);
    dc_node_receive(&air, &ack_a, 6);
    CHECK(air.ready && air.session == 0xbbbb0002u);
    CHECK(air.hub_session == 0xbbbb0001u);
}

static void test_retired_challenge_cannot_rebind_air(void) {
    dc_node_t air;
    capture_t capture;
    dc_frame_t hello_a, ack_a, hello_b, ack_b;
    memset(&capture, 0, sizeof(capture));
    dc_node_init(&air, DC_AIR, 0x22u, 0, capture_send, &capture);
    hello_a = hello_to_air(0xaaaa0001u, 0x11u);
    ack_a = ack_to_air(0x22u, 0xaaaa0001u, 0x11u, 0xaaaa0002u);
    hello_b = hello_to_air(0xaaaa0001u, 0x33u);
    ack_b = ack_to_air(0x22u, 0xaaaa0001u, 0x33u, 0xbbbb0002u);
    dc_node_receive(&air, &hello_a, 1);
    dc_node_receive(&air, &ack_a, 2);
    dc_node_tick(&air, 1502);
    CHECK(!air.ready);
    dc_node_receive(&air, &hello_b, 1503);
    dc_node_receive(&air, &ack_b, 1504);
    CHECK(air.ready && air.session == 0xbbbb0002u);
    dc_node_receive(&air, &hello_a, 1505);
    dc_node_receive(&air, &ack_a, 1506);
    CHECK(air.ready && air.session == 0xbbbb0002u);
    CHECK(air.challenge == 0x33u);
}

static void test_replayed_discovery_preserves_bound_peer(void) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    dc_frame_t discovery;
    uint32_t assigned;
    CHECK(bind_pair(&hub, &air, &hub_capture, &air_capture));
    assigned = hub.peers[DC_AIR].session;
    discovery = frame(DC_AIR, DC_HUB, DC_HELLO, 0, 0);
    discovery.len = 4;
    dc_put_u32(discovery.payload, air.boot_nonce);
    dc_node_receive(&hub, &discovery, 50);
    CHECK(hub.peers[DC_AIR].ready);
    CHECK(hub.peers[DC_AIR].session == assigned);
}

static void test_rebind_ack_rejects_older_queued_request(void) {
    dc_node_t air;
    capture_t capture;
    dc_frame_t hello, ack, request;
    memset(&capture, 0, sizeof(capture));
    dc_node_init(&air, DC_AIR, 0x22u, 0, capture_send, &capture);
    hello = hello_to_air(0xaaaa0001u, 0x11u);
    ack = ack_to_air(0x22u, 0xaaaa0001u, 0x11u, 0xaaaa0002u);
    ack.seq = 10;
    dc_node_receive(&air, &hello, 1);
    dc_node_receive(&air, &ack, 2);
    request = frame(DC_HUB, DC_AIR, DC_TEST_REQUEST, 0xaaaa0001u, 11);
    request.len = 1; request.payload[0] = 0x55;
    dc_node_receive(&air, &request, 3);
    CHECK(air.test_rx == 1);
    dc_node_tick(&air, 1503);
    CHECK(!air.ready);
    hello = hello_to_air(0xaaaa0001u, 0x33u);
    ack = ack_to_air(0x22u, 0xaaaa0001u, 0x33u, 0xbbbb0002u);
    ack.seq = 20;
    dc_node_receive(&air, &hello, 1504);
    dc_node_receive(&air, &ack, 1505);
    CHECK(air.ready && air.session == 0xbbbb0002u);
    dc_node_receive(&air, &request, 1506);
    CHECK(air.test_rx == 1 && !air.cached_valid);
    request.seq = 21;
    dc_node_receive(&air, &request, 1507);
    CHECK(air.test_rx == 2 && air.cached_valid);
}

static void test_timeout_cannot_be_revived_by_old_handshake(void) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    dc_frame_t old_ack, old_hello;
    CHECK(bind_pair(&hub, &air, &hub_capture, &air_capture));
    old_ack = hub_capture.frames[hub_capture.count - 1u];
    old_hello = hello_to_air(hub.session, air.challenge);
    dc_node_tick(&air, 1503);
    CHECK(!air.ready && air.session == 0);
    dc_node_receive(&air, &old_ack, 1504);
    CHECK(!air.ready && air.session == 0 && !air.peers[DC_HUB].ready);
    dc_node_receive(&air, &old_hello, 1505);
    dc_node_receive(&air, &old_ack, 1506);
    CHECK(!air.ready && air.session == 0 && !air.peers[DC_HUB].ready);
}

static void test_congestion_and_telemetry_freshness(void) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    dc_frame_t incoming;
    CHECK(bind_pair(&hub, &air, &hub_capture, &air_capture));
    hub_capture.refuse = true;
    CHECK(!dc_node_request(&hub, DC_TELEMETRY_QUERY, NULL, 0, 10));
    CHECK(!hub.request_pending && hub.tx_dropped == 1);
    hub_capture.refuse = false;
    CHECK(dc_node_request(&hub, DC_TELEMETRY_QUERY, NULL, 0, 11));
    CHECK(!dc_node_request(&hub, DC_TEST_REQUEST, NULL, 0, 12));
    incoming = frame(DC_AIR, DC_HUB, DC_TELEMETRY, air.session, 10);
    incoming.len = 16;
    dc_put_u16(incoming.payload, hub.request_seq);
    dc_node_receive(&hub, &incoming, 20);
    CHECK(hub.telemetry_rx == 1 && hub.telemetry_valid);
    CHECK(hub.last_telemetry == 20 && !hub.request_pending);
    incoming = frame(DC_AIR, DC_HUB, DC_HEARTBEAT, air.session, 11);
    dc_node_receive(&hub, &incoming, 500);
    CHECK(hub.last_telemetry == 20);
    incoming.seq = 12;
    dc_node_receive(&hub, &incoming, 1200);
    dc_node_tick(&hub, 1520);
    CHECK(!hub.telemetry_valid && dc_node_peer_healthy(&hub, DC_AIR, 1520));
    CHECK(dc_node_request(&hub, DC_TELEMETRY_QUERY, NULL, 0, 1521));
    dc_node_tick(&hub, 1721);
    CHECK(!hub.request_pending && hub.request_timeouts == 1);
    incoming = frame(DC_AIR, DC_HUB, DC_TELEMETRY, air.session, 13);
    incoming.len = 16;
    dc_put_u16(incoming.payload, hub.request_seq);
    dc_node_receive(&hub, &incoming, 1722);
    CHECK(hub.telemetry_rx == 1 && !hub.telemetry_valid);
    CHECK(hub.last_telemetry == 20);
}

static void test_air_duplicate_request_is_idempotent(void) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    dc_frame_t incoming, first_reply, repeated_reply;
    CHECK(bind_pair(&hub, &air, &hub_capture, &air_capture));
    incoming = frame(DC_HUB, DC_AIR, DC_TEST_REQUEST, hub.session, 100);
    incoming.len = 3;
    incoming.payload[0] = 0; incoming.payload[1] = 0x55; incoming.payload[2] = 0xff;
    air_capture.count = 0;
    dc_node_receive(&air, &incoming, 10);
    CHECK(air_capture.count == 1 && air.test_rx == 1);
    first_reply = air_capture.frames[0];
    dc_node_receive(&air, &incoming, 11);
    CHECK(air_capture.count == 2 && air.test_rx == 1);
    repeated_reply = air_capture.frames[1];
    CHECK(first_reply.type == DC_TEST_RESULT && first_reply.len == 5);
    CHECK((uint16_t)(repeated_reply.seq - first_reply.seq) > 0 &&
          (uint16_t)(repeated_reply.seq - first_reply.seq) < 0x8000u &&
          repeated_reply.session == first_reply.session &&
          memcmp(repeated_reply.payload, first_reply.payload, first_reply.len) == 0);
    CHECK(air.peers[DC_HUB].last_seen == 10);
}

static void test_retry_after_newer_heartbeat_and_error(void) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    dc_frame_t request, discarded_reply, incoming, retry_reply;
    const uint8_t payload[] = {0, 0x55, 0xff};
    CHECK(bind_pair(&hub, &air, &hub_capture, &air_capture));
    hub_capture.count = 0; air_capture.count = 0;
    CHECK(dc_node_request(&hub, DC_TEST_REQUEST, payload, sizeof(payload), 10));
    CHECK(hub_capture.count == 1);
    request = hub_capture.frames[0];
    dc_node_receive(&air, &request, 11);
    CHECK(air_capture.count == 1);
    discarded_reply = air_capture.frames[0];
    /* Drop the first reply; later transport traffic overtakes it. */
    /* AIR normally avoids unsolicited RF traffic. Inject a valid later
     * heartbeat explicitly to exercise transport ordering independently. */
    incoming = frame(DC_AIR, DC_HUB, DC_HEARTBEAT, air.session,
                     air.tx_seq[DC_HUB]++);
    dc_node_receive(&hub, &incoming, 21);
    CHECK(hub.request_pending);
    incoming = frame(DC_HUB, DC_AIR, DC_CONTROL_CMD, hub.session,
                     (uint16_t)(request.seq + 1u));
    air_capture.count = 0;
    dc_node_receive(&air, &incoming, 22);
    CHECK(air_capture.count == 1 && air_capture.frames[0].type == DC_ERROR);
    dc_node_receive(&hub, &air_capture.frames[0], 23);
    CHECK(hub.request_pending);
    air_capture.count = 0;
    dc_node_receive(&air, &request, 24);
    CHECK(air_capture.count == 1 && air.test_rx == 1);
    retry_reply = air_capture.frames[0];
    CHECK((uint16_t)(retry_reply.seq - discarded_reply.seq) > 0 &&
          (uint16_t)(retry_reply.seq - discarded_reply.seq) < 0x8000u);
    CHECK(retry_reply.len == discarded_reply.len &&
          memcmp(retry_reply.payload, discarded_reply.payload, retry_reply.len) == 0);
    dc_node_receive(&hub, &retry_reply, 25);
    CHECK(!hub.request_pending && hub.test_rx == 1 && air.test_rx == 1);
}

static void handshake_loss_case(uint32_t base, unsigned loss_ms) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    unsigned elapsed, i, rounds;
    memset(&hub_capture, 0, sizeof(hub_capture));
    memset(&air_capture, 0, sizeof(air_capture));
    dc_node_init(&hub, DC_HUB, 0x11u, 0x12345678u, capture_send, &hub_capture);
    dc_node_init(&air, DC_AIR, 0x22u, 0, capture_send, &air_capture);
    for (elapsed = 0; elapsed <= loss_ms + 4000u; elapsed += 10u) {
        uint32_t now = base + elapsed;
        dc_node_tick(&hub, now);
        dc_node_tick(&air, now);
        for (rounds = 0; rounds < 10 && (hub_capture.count || air_capture.count); rounds++) {
            dc_frame_t sent[64];
            unsigned count = hub_capture.count;
            memcpy(sent, hub_capture.frames, count * sizeof(sent[0]));
            hub_capture.count = 0;
            for (i = 0; i < count; i++)
                if (sent[i].dst == DC_AIR) dc_node_receive(&air, &sent[i], now);
            count = air_capture.count;
            memcpy(sent, air_capture.frames, count * sizeof(sent[0]));
            air_capture.count = 0;
            /* Lose the initial challenge responses for longer than one
             * binding window, then restore the complete bidirectional link. */
            if (elapsed >= loss_ms)
                for (i = 0; i < count; i++) dc_node_receive(&hub, &sent[i], now);
        }
        CHECK(rounds < 10);
    }
    CHECK(air.ready && hub.peers[DC_AIR].ready);
    CHECK(air.session == hub.peers[DC_AIR].session);
    CHECK(hub.telemetry_valid && hub.telemetry_rx > 10u);
}

static void test_handshake_loss_recovery_and_absolute_expiry(void) {
    dc_node_t hub, air;
    capture_t hub_capture, air_capture;
    dc_frame_t old_probe, old_response, new_probe, new_response;
    unsigned i;
    bool found = false;
    handshake_loss_case(0, 2000);
    handshake_loss_case(0, 12000);
    handshake_loss_case(0xfffffc18u, 2000); /* binding window crosses timer wrap */
    memset(&hub_capture, 0, sizeof(hub_capture));
    memset(&air_capture, 0, sizeof(air_capture));
    dc_node_init(&hub, DC_HUB, 0x11u, 0x12345678u, capture_send, &hub_capture);
    dc_node_init(&air, DC_AIR, 0x22u, 0, capture_send, &air_capture);
    hub.poll_enabled = false;
    dc_node_tick(&hub, 0);
    memset(&old_probe, 0, sizeof(old_probe));
    for (i = 0; i < hub_capture.count; i++)
        if (hub_capture.frames[i].dst == DC_AIR) {
            old_probe = hub_capture.frames[i]; found = true; break;
        }
    CHECK(found);
    dc_node_receive(&air, &old_probe, 0);
    CHECK(air_capture.count > 0);
    old_response = air_capture.frames[air_capture.count - 1u];
    dc_node_tick(&hub, 1000);
    dc_node_receive(&air, &old_probe, 1000);
    CHECK(hub.peers[DC_AIR].handshake_started == 0 && air.binding_at == 0);
    hub_capture.count = 0;
    /* A response arriving at expiry, before HUB tick, must cause a fresh
     * challenge rather than bind the old attempt or refresh its lifetime. */
    dc_node_receive(&hub, &old_response, DC_LINK_TIMEOUT_MS);
    CHECK(!hub.peers[DC_AIR].ready && hub.peers[DC_AIR].pending);
    CHECK(hub_capture.count == 1 && hub_capture.frames[0].type == DC_HELLO);
    new_probe = hub_capture.frames[0];
    CHECK(dc_get_u32(new_probe.payload) != dc_get_u32(old_probe.payload));
    dc_node_tick(&air, DC_LINK_TIMEOUT_MS);
    dc_node_receive(&air, &old_probe, DC_LINK_TIMEOUT_MS);
    CHECK(!air.binding_pending && !air.ready);
    air_capture.count = 0;
    dc_node_receive(&air, &new_probe, 1501);
    CHECK(air.binding_pending && air.binding_at == 1501 && air_capture.count == 1);
    new_response = air_capture.frames[0];
    hub_capture.count = 0;
    dc_node_receive(&hub, &old_response, 1501);
    CHECK(!hub.peers[DC_AIR].ready && hub_capture.count == 0);
    dc_node_receive(&hub, &new_response, 1502);
    CHECK(hub.peers[DC_AIR].ready && hub_capture.count == 1);
    dc_node_receive(&air, &hub_capture.frames[0], 1503);
    CHECK(air.ready && air.session == hub.peers[DC_AIR].session);
    dc_node_receive(&air, &old_probe, 1504);
    CHECK(air.ready && air.challenge == dc_get_u32(new_probe.payload));
}

int main(void) {
    test_protocol_boundaries();
    test_unsolicited_ack_and_zero_challenge();
    test_duplicate_ack_and_old_business();
    test_retired_hub_cannot_rebind_air();
    test_retired_challenge_cannot_rebind_air();
    test_replayed_discovery_preserves_bound_peer();
    test_rebind_ack_rejects_older_queued_request();
    test_timeout_cannot_be_revived_by_old_handshake();
    test_congestion_and_telemetry_freshness();
    test_air_duplicate_request_is_idempotent();
    test_retry_after_newer_heartbeat_and_error();
    test_handshake_loss_recovery_and_absolute_expiry();
    printf("Independent review: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
