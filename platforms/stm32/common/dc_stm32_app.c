#include "dc_stm32_app.h"
static void apply_radio_binding(dc_stm32_app_t *a) {
    if(a->radio.state==DC_RADIO_RX || a->radio.state==DC_RADIO_TX || a->radio.state==DC_RADIO_RX_START)
        dc_radio_set_reply_session(&a->radio,a->air_expected_reply_session,HAL_GetTick());
}
static void clear_queues(dc_stm32_app_t *a) {
    unsigned i;
    for(i=0;i<a->port_count;i++) dc_stm32_uart_clear_tx(&a->ports[i]);
    dc_queue_init(&a->rf_tx);
    if(a->has_radio) dc_radio_cancel(&a->radio,HAL_GetTick());
}
static bool emit(void *ctx,const dc_frame_t *f) {
    dc_stm32_app_t *a=ctx; uint8_t idx;
    dc_frame_t diagnostics;
    if(a->node.id==DC_RADIO_GROUND && f->src==DC_RADIO_GROUND && f->type==DC_RADIO_STATE) {
        uint32_t age=HAL_GetTick()-a->radio.last_rx_ms;
        unsigned i; diagnostics=*f; diagnostics.len=DC_PAYLOAD_MAX;
        for(i=0;i<DC_PAYLOAD_MAX;i++) diagnostics.payload[i]=0;
        diagnostics.payload[0]=0; /* real driver diagnostics, not PC/sensor simulation */
        diagnostics.payload[1]=(uint8_t)a->radio.state;
        diagnostics.payload[2]=a->radio.last_lqi; diagnostics.payload[3]=a->radio.rssi_valid?1u:0u;
        dc_put_u16(diagnostics.payload+4,(uint16_t)a->radio.last_rssi_x2_dbm);
        dc_put_u16(diagnostics.payload+6,(uint16_t)((!a->radio.rssi_valid || age>65535u)?65535u:age));
        dc_put_u32(diagnostics.payload+8,a->radio.stats.rx_valid);
        dc_put_u32(diagnostics.payload+12,a->radio.stats.rx_hw_crc+a->radio.stats.rx_app_crc+
            a->radio.stats.rx_invalid+a->radio.stats.rx_overflow);
        dc_put_u32(diagnostics.payload+16,a->radio.stats.retries);
        dc_put_u32(diagnostics.payload+20,a->radio.stats.tx_ok);
        dc_put_u32(diagnostics.payload+24,a->radio.stats.transaction_timeout);
        dc_put_u32(diagnostics.payload+28,a->radio.stats.recoveries+a->rf_reinit_count);
        f=&diagnostics;
    }
    /* receive() updates the session before it emits ACK; clear before enqueue. */
    if(a->node.session!=a->local_session) {
        clear_queues(a); a->local_session=a->node.session;
    }
    if(a->has_radio && (a->node.id==DC_AIR || f->dst==DC_AIR)) {
        if(a->node.id==DC_RADIO_GROUND && f->src==DC_HUB) {
            if(f->type==DC_HELLO) {
                uint32_t challenge; unsigned i;
                if(f->len!=4 || !f->session || !dc_get_u32(f->payload)) {
                    a->binding_replays++; return false;
                }
                challenge=dc_get_u32(f->payload);
                for(i=0;i<8;i++) {
                    if(a->air_retired_hubs[i]==f->session || a->node.retired_hubs[i]==f->session) {
                        a->binding_replays++; return false;
                    }
                }
                if(a->air_hub_session==f->session && a->air_challenge && challenge!=a->air_challenge &&
                   (uint32_t)(challenge-a->air_challenge)>=0x80000000u) {
                    a->binding_replays++; return false;
                }
                if(a->air_hub_session!=f->session || a->air_challenge!=challenge) {
                    if(a->air_hub_session && a->air_hub_session!=f->session) {
                        a->air_retired_hubs[a->air_retired_next]=a->air_hub_session;
                        a->air_retired_next=(uint8_t)((a->air_retired_next+1u)%8u);
                    }
                    dc_queue_init(&a->rf_tx);
                    a->air_expected_reply_session=0; apply_radio_binding(a);
                    a->air_hub_session=f->session; a->air_challenge=challenge;
                    a->air_binding_pending=true; a->air_binding_at=HAL_GetTick();
                }
            } else if(f->type==DC_HELLO_ACK) {
                uint32_t assigned=f->len==12?dc_get_u32(f->payload+8):0;
                if(f->len!=12 || !assigned || f->session!=a->air_hub_session ||
                   dc_get_u32(f->payload+4)!=a->air_challenge ||
                   (!a->air_binding_pending && assigned!=a->air_expected_reply_session) ||
                   (a->air_binding_pending && (uint32_t)(HAL_GetTick()-a->air_binding_at)>=DC_LINK_TIMEOUT_MS)) {
                    a->binding_replays++; return false;
                }
                a->air_expected_reply_session=assigned; apply_radio_binding(a);
                a->air_binding_pending=false;
            }
        }
        idx=(uint8_t)((a->rf_tx.head+a->rf_tx.count)%DC_QUEUE_CAPACITY);
        if(!dc_queue_push(&a->rf_tx,f)) return false;
        a->rf_enqueued[idx]=HAL_GetTick(); return true;
    }
    if(!a->port_count) return false;
    if(a->node.id==DC_HUB && f->dst==DC_ZYNQ)
        return dc_stm32_uart_send(&a->ports[1],f);
    return dc_stm32_uart_send(&a->ports[0],f);
}
static void radio_frame(void *ctx,const dc_frame_t *f) {
    dc_stm32_app_t *a=ctx;
    /* A boot discovery (including delayed duplicates) is forwarded to HUB.
     * Only a new HUB challenge arbitrates resetting the AIR radio binding. */
    dc_node_receive(&a->node,f,HAL_GetTick());
}
static void radio_result(void *ctx,dc_radio_completion_t result,
                          const dc_frame_t *request,const dc_frame_t *reply) {
    /* on_frame already delivers successful replies. Statistics remain driver-owned. */
    (void)ctx; (void)result; (void)request; (void)reply;
}
bool dc_stm32_app_init(dc_stm32_app_t *a,uint8_t id,uint32_t nonce,uint32_t seed,
                      UART_HandleTypeDef *p0,UART_HandleTypeDef *p1,
                      const dc_radio_port_t *rf) {
    uint32_t now=HAL_GetTick(); unsigned i;
    if(!a || (id!=DC_HUB && id!=DC_RADIO_GROUND && id!=DC_AIR)) return false;
    if(id==DC_HUB && (!seed || !p0 || !p1 || rf)) return false;
    if(id==DC_RADIO_GROUND && (!p0 || p1 || !rf)) return false;
    if(id==DC_AIR && (p0 || p1 || !rf)) return false;
    if(rf && (!rf->read_reg || !rf->write_reg || !rf->read_burst || !rf->write_burst || !rf->strobe)) return false;
    a->port_count=p1?2u:(p0?1u:0u); a->has_radio=rf!=0;
    a->local_session=0; a->air_hub_session=0; a->air_challenge=0;
    for(i=0;i<8;i++) a->air_retired_hubs[i]=0;
    a->air_retired_next=0; a->air_binding_pending=false; a->air_binding_at=0; a->binding_replays=0;
    a->air_expected_reply_session=0; a->rf_reinit_at=now; a->rf_reinit_count=0; a->rf_reinit_failures=0;
    a->rf_expired=0; dc_queue_init(&a->rf_tx);
    for(i=0;i<a->port_count;i++) {
        dc_parser_init(&a->parsers[i]);
        if(!dc_stm32_uart_init(&a->ports[i],i?p1:p0)) return false;
    }
    if(rf) {
        a->rf_port=*rf; a->rf_local_address=id==DC_AIR?DC_RF_AIR_ADDRESS:DC_RF_GROUND_ADDRESS;
        if(!dc_radio_init(&a->radio,&a->rf_port,a->rf_local_address,radio_frame,radio_result,a,now)) {
            a->radio.state=DC_RADIO_FAULT; a->rf_reinit_failures++;
        }
    }
    dc_node_init(&a->node,id,nonce,seed,emit,a); return true;
}
void dc_stm32_app_poll(dc_stm32_app_t *a) {
    uint32_t now=HAL_GetTick(); unsigned i,budget; uint8_t byte; dc_frame_t f; int rc;
    if(a->has_radio) {
        if(a->radio.state==DC_RADIO_FAULT && (uint32_t)(now-a->rf_reinit_at)>=500u) {
            a->rf_reinit_at=now; dc_queue_init(&a->rf_tx);
            a->air_expected_reply_session=0; a->air_binding_pending=false;
            if(!dc_radio_init(&a->radio,&a->rf_port,a->rf_local_address,radio_frame,radio_result,a,now)) {
                a->radio.state=DC_RADIO_FAULT; a->rf_reinit_failures++;
            } else a->rf_reinit_count++;
        }
        dc_radio_tick(&a->radio,now);
        if(a->node.id==DC_RADIO_GROUND) apply_radio_binding(a);
    }
    for(i=0;i<a->port_count;i++) {
        for(budget=0;budget<128u;budget++) {
            rc=dc_stm32_uart_read(&a->ports[i],&byte);
            if(rc<0) { dc_parser_init(&a->parsers[i]); a->parsers[i].dropping=true; break; }
            if(!rc) break;
            if(dc_parser_feed(&a->parsers[i],byte,&f)) dc_node_receive(&a->node,&f,now);
        }
    }
    dc_node_tick(&a->node,now);
    if(a->has_radio && dc_radio_ready(&a->radio) && !dc_radio_transaction_active(&a->radio)
                         && a->rf_tx.count) {
        uint32_t enqueued=a->rf_enqueued[a->rf_tx.head];
        bool tracked;
        f=a->rf_tx.items[a->rf_tx.head];
        tracked=a->node.id==DC_RADIO_GROUND &&
                 (f.type==DC_TELEMETRY_QUERY || f.type==DC_TEST_REQUEST);
        if((uint32_t)(now-enqueued)>=(tracked?200u:1500u)) {
            (void)dc_queue_pop(&a->rf_tx,&f); a->rf_expired++;
        } else if(tracked ? dc_radio_request(&a->radio,&f,DC_RF_AIR_ADDRESS,now)
                          : dc_radio_send_untracked(&a->radio,&f,
                              a->node.id==DC_AIR?DC_RF_GROUND_ADDRESS:DC_RF_AIR_ADDRESS,now)) {
            if(tracked) a->radio.transaction_deadline=enqueued+200u;
            (void)dc_queue_pop(&a->rf_tx,&f);
        }
    }
    for(i=0;i<a->port_count;i++) dc_stm32_uart_service(&a->ports[i],now);
}
void dc_stm32_app_rx_irq(dc_stm32_app_t *a,UART_HandleTypeDef *u) {
    unsigned i; for(i=0;i<a->port_count;i++) if(a->ports[i].uart==u) dc_stm32_uart_rx_irq(&a->ports[i]);
}
void dc_stm32_app_tx_irq(dc_stm32_app_t *a,UART_HandleTypeDef *u) {
    unsigned i; for(i=0;i<a->port_count;i++) if(a->ports[i].uart==u) dc_stm32_uart_tx_irq(&a->ports[i]);
}
void dc_stm32_app_uart_error_irq(dc_stm32_app_t *a,UART_HandleTypeDef *u) {
    unsigned i; for(i=0;i<a->port_count;i++) if(a->ports[i].uart==u) dc_stm32_uart_error_irq(&a->ports[i]);
}
