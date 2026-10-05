/* Behavioral test with explicit HAL/radio transport mocks. This does not prove
 * SDK/HAL compilation; that separate proof uses official headers and sources. */
#include "dc_stm32_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t now;
static unsigned init_calls;
static bool init_fails;
static bool mock_read(void *ctx,uint8_t reg,uint8_t *value) { (void)ctx;(void)reg;*value=0;return true; }
static bool mock_write(void *ctx,uint8_t reg,uint8_t value) { (void)ctx;(void)reg;(void)value;return true; }
static bool mock_read_burst(void *ctx,uint8_t reg,uint8_t *buf,size_t count) { (void)ctx;(void)reg;(void)buf;(void)count;return true; }
static bool mock_write_burst(void *ctx,uint8_t reg,const uint8_t *buf,size_t count) { (void)ctx;(void)reg;(void)buf;(void)count;return true; }
uint32_t HAL_GetTick(void) { return now; }
bool dc_stm32_uart_init(dc_stm32_uart_t *p,UART_HandleTypeDef *u) {
    p->uart=u; dc_queue_init(&p->tx); return true;
}
bool dc_stm32_uart_send(dc_stm32_uart_t *p,const dc_frame_t *f) { return dc_queue_push(&p->tx,f); }
void dc_stm32_uart_service(dc_stm32_uart_t *p,uint32_t ms) { (void)p;(void)ms; }
int dc_stm32_uart_read(dc_stm32_uart_t *p,uint8_t *b) { (void)p;(void)b;return 0; }
void dc_stm32_uart_clear_tx(dc_stm32_uart_t *p) { dc_queue_init(&p->tx); }
void dc_stm32_uart_rx_irq(dc_stm32_uart_t *p) { (void)p; }
void dc_stm32_uart_tx_irq(dc_stm32_uart_t *p) { (void)p; }
void dc_stm32_uart_error_irq(dc_stm32_uart_t *p) { (void)p; }
bool dc_radio_init(dc_radio_t *r,const dc_radio_port_t *p,uint8_t address,
                  dc_radio_receive_fn receive,dc_radio_transaction_fn done,void *user,uint32_t ms) {
    (void)done;init_calls++; memset(r,0,sizeof(*r)); r->port=*p;r->local_address=address;
    r->on_frame=receive;r->user=user;
    if(init_fails) { r->state=DC_RADIO_FAULT;return false; }
    r->state=init_calls==1?DC_RADIO_RX:DC_RADIO_CALIBRATING;r->state_deadline=ms+20;return true;
}
void dc_radio_tick(dc_radio_t *r,uint32_t ms) {
    if(r->state==DC_RADIO_CALIBRATING && ms>=r->state_deadline) r->state=DC_RADIO_RX;
}
void dc_radio_cancel(dc_radio_t *r,uint32_t ms) { (void)ms;r->transaction=DC_RF_TXN_IDLE; }
void dc_radio_set_reply_session(dc_radio_t *r,uint32_t session,uint32_t ms) {
    if(r->reply_session!=session) { r->reply_session=session;dc_radio_cancel(r,ms); }
}
bool dc_radio_ready(const dc_radio_t *r) { return r->state==DC_RADIO_RX; }
bool dc_radio_transaction_active(const dc_radio_t *r) { return r->transaction!=DC_RF_TXN_IDLE; }
bool dc_radio_request(dc_radio_t *r,const dc_frame_t *f,uint8_t addr,uint32_t ms) {
    (void)f;(void)addr;(void)ms; if(!r->reply_session) return false;
    r->transaction=DC_RF_TXN_TX; return true;
}
bool dc_radio_send_untracked(dc_radio_t *r,const dc_frame_t *f,uint8_t addr,uint32_t ms) {
    (void)r;(void)f;(void)addr;(void)ms;return true;
}
static dc_frame_t probe(uint32_t hub,uint32_t challenge) {
    dc_frame_t f; memset(&f,0,sizeof f); f.src=DC_HUB;f.dst=DC_AIR;
    f.type=DC_HELLO;f.session=hub;f.len=4;dc_put_u32(f.payload,challenge);return f;
}
static dc_frame_t ack(uint32_t hub,uint32_t challenge,uint32_t assigned) {
    dc_frame_t f=probe(hub,challenge);f.type=DC_HELLO_ACK;f.len=12;
    dc_put_u32(f.payload,44);dc_put_u32(f.payload+4,challenge);dc_put_u32(f.payload+8,assigned);return f;
}
int main(void) {
    dc_stm32_app_t a; UART_HandleTypeDef u={0};
    dc_radio_port_t bus={0,mock_read,mock_write,mock_read_burst,mock_write_burst,mock_read}; dc_frame_t f;
    uint8_t count; uint32_t rejected;
    assert(!dc_stm32_app_init(&a,0,0,0,&u,0,&bus));
    assert(!dc_stm32_app_init(&a,DC_AIR,0,0,0,&u,&bus));
    assert(!dc_stm32_app_init(&a,DC_HUB,0,1,&u,&u,&bus));
    assert(dc_stm32_app_init(&a,DC_RADIO_GROUND,44,0,&u,0,&bus));
    now=10;f=probe(100,10);dc_node_receive(&a.node,&f,now);
    assert(a.air_binding_pending&&a.air_challenge==10);
    now=20;f=ack(100,10,55);dc_node_receive(&a.node,&f,now);
    assert(a.radio.reply_session==55&&!a.air_binding_pending);
    a.radio.transaction=DC_RF_TXN_WAIT_REPLY;count=a.rf_tx.count;
    /* Delayed AIR discovery cannot clear a healthy radio binding or request. */
    f=probe(0,44);f.src=DC_AIR;f.dst=DC_HUB;
    a.radio.on_frame(a.radio.user,&f);
    assert(a.radio.reply_session==55&&a.radio.transaction==DC_RF_TXN_WAIT_REPLY);
    assert(a.rf_tx.count==count);
    /* Reject old same-epoch challenge, and ACK with a changed assignment. */
    rejected=a.binding_replays;f=probe(100,9);dc_node_receive(&a.node,&f,now);
    assert(a.binding_replays==rejected+1&&a.rf_tx.count==count&&a.radio.reply_session==55);
    f=ack(100,10,56);dc_node_receive(&a.node,&f,now);assert(a.radio.reply_session==55);
    /* New HUB boot clears queue/transaction/binding. Retired HUB stays rejected. */
    now=30;f=probe(200,1);dc_node_receive(&a.node,&f,now);
    assert(a.radio.reply_session==0&&a.radio.transaction==DC_RF_TXN_IDLE&&a.rf_tx.count==1);
    f=probe(100,20);dc_node_receive(&a.node,&f,now);assert(a.air_hub_session==200&&a.rf_tx.count==1);
    f=ack(100,20,99);dc_node_receive(&a.node,&f,now);assert(a.radio.reply_session==0);
    f=ack(200,1,88);dc_node_receive(&a.node,&f,now);assert(a.radio.reply_session==88);
    f=probe(200,2);dc_node_receive(&a.node,&f,now);assert(a.radio.reply_session==0);
    now+=1500;f=ack(200,2,89);dc_node_receive(&a.node,&f,now);assert(a.radio.reply_session==0);
    /* Real-driver status encoding retains transport identity and precision. */
    dc_queue_init(&a.ports[0].tx); memset(&f,0,sizeof f);
    f.src=DC_RADIO_GROUND;f.dst=DC_HUB;f.type=DC_RADIO_STATE;f.session=999;f.seq=77;
    a.radio.rssi_valid=true;a.radio.last_rx_ms=now-50;a.radio.last_rssi_x2_dbm=-161;a.radio.last_lqi=66;
    a.radio.stats.rx_valid=9;a.radio.stats.rx_hw_crc=2;a.radio.stats.rx_invalid=3;
    a.radio.stats.retries=7;a.radio.stats.tx_ok=8;a.radio.stats.transaction_timeout=4;
    a.radio.stats.recoveries=2;
    assert(a.node.send(a.node.user,&f));assert(dc_queue_pop(&a.ports[0].tx,&f));
    assert(f.len==32&&f.src==DC_RADIO_GROUND&&f.session==999&&f.seq==77);
    assert(f.payload[0]==0&&f.payload[2]==66&&f.payload[3]==1);
    assert((int16_t)dc_get_u16(f.payload+4)==-161&&dc_get_u16(f.payload+6)==50);
    assert(dc_get_u32(f.payload+8)==9&&dc_get_u32(f.payload+12)==5);
    /* Main retries FAULT at most once each 500 ms; UART remains independent. */
    a.radio.state=DC_RADIO_FAULT;a.rf_reinit_at=now;dc_queue_init(&a.ports[0].tx);
    now+=499;dc_stm32_app_poll(&a);assert(init_calls==1&&a.radio.state==DC_RADIO_FAULT);
    assert(a.ports[0].tx.count>0);
    now++;dc_stm32_app_poll(&a);
    assert(init_calls==2&&a.rf_reinit_count==1&&a.radio.state==DC_RADIO_CALIBRATING);
    assert(a.radio.reply_session==0&&a.rf_tx.count==0);
    now+=20;dc_stm32_app_poll(&a);assert(a.radio.state==DC_RADIO_RX);
    a.radio.state=DC_RADIO_FAULT;a.rf_reinit_at=now;init_fails=true;now+=500;
    dc_stm32_app_poll(&a);assert(init_calls==3&&a.rf_reinit_failures==1);
    now++;dc_stm32_app_poll(&a);assert(init_calls==3&&a.radio.state==DC_RADIO_FAULT);
    puts("mock HAL app: binding replay, status metadata, FAULT/backoff/recovery and UART continuity PASS");
    return 0;
}
