#include "dc_radio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* FIFO bytes are consumed exactly like SPI reads, including length, RF address,
 * RSSI and CRC_OK/LQI. Strobes implement the IDLE/RX/TX state changes. */
typedef struct {
    uint8_t reg[64],rx[64],tx[64],power;
    size_t rxcount,txcount;
    bool failed_io,stuck_cal,stuck_rx,bad_readback,bad_version;
    bool failed_reset,tx_abort_residue;
    bool alternating_status;
    bool errata_rx_collision;
    uint8_t errata_arriving_byte;
    uint8_t status_address,status_script[8];
    size_t status_script_used,status_script_count;
    unsigned status_samples;
    unsigned spi_calls,transmissions,received,completed;
    unsigned resets,corrupted_transmissions;
    unsigned errata_duplications;
    dc_radio_completion_t completion;
    dc_frame_t last_receive,tx_log[8];
} mock_t;
static bool mock_read(void *ctx,uint8_t a,uint8_t *v) {
    mock_t *m=ctx; m->spi_calls++;
    if (m->failed_io) return false;
    if (a==0x3b) *v=(uint8_t)m->rxcount | (m->reg[a]&0x80);
    else if (a==0x3a) *v=(uint8_t)m->txcount | (m->reg[a]&0x80);
    else *v=m->reg[a];
    if (a==m->status_address) {
        if (m->status_script_used<m->status_script_count)
            *v=m->status_script[m->status_script_used++];
        if (m->alternating_status) *v^=(uint8_t)(m->status_samples++&1u);
    }
    if (m->bad_readback && a==0x10) *v^=1;
    return true;
}
static bool mock_write(void *ctx,uint8_t a,uint8_t v) {
    mock_t *m=ctx; m->spi_calls++;
    if (m->failed_io) return false;
    m->reg[a]=v; return true;
}
static bool mock_read_burst(void *ctx,uint8_t a,uint8_t *data,size_t n) {
    mock_t *m=ctx; m->spi_calls++;
    bool collision;
    if (m->failed_io) return false;
    if (a==0x3e && n==1) { *data=m->power; return true; }
    if (a!=0x3f || n>m->rxcount) return false;
    /* Model SWRZ020E p.2: one new RF byte arrives while SPI reads the last
     * currently buffered byte, so the read byte is duplicated in the FIFO. */
    collision=m->errata_rx_collision && m->reg[0x35]==13 && n==m->rxcount;
    memcpy(data,m->rx,n); m->rxcount-=n; memmove(m->rx,m->rx+n,m->rxcount);
    if (collision) {
        assert(n>0);
        m->rx[0]=data[n-1]; m->rx[1]=m->errata_arriving_byte; m->rxcount=2;
        m->errata_duplications++;
    }
    return true;
}
static bool mock_write_burst(void *ctx,uint8_t a,const uint8_t *data,size_t n) {
    mock_t *m=ctx; m->spi_calls++;
    if (m->failed_io) return false;
    if (a==0x3e && n==1) { m->power=*data; return true; }
    if (a!=0x3f || n>64-m->txcount) return false;
    memcpy(m->tx+m->txcount,data,n); m->txcount+=n; return true;
}
static bool mock_strobe(void *ctx,uint8_t c,uint8_t *status) {
    mock_t *m=ctx; m->spi_calls++;
    if (m->failed_io) return false;
    *status=0;
    switch(c) {
    case 0x30:
        if (m->failed_reset) return false;
        m->resets++; m->tx_abort_residue=false;
        memset(m->reg,0,sizeof(m->reg)); m->reg[0x31]=m->bad_version ? 0xff : 0x14;
        m->reg[0x35]=1; m->rxcount=m->txcount=0; break;
    case 0x33: m->reg[0x35]=m->stuck_cal ? 8 : 1; break;
    case 0x34: m->reg[0x35]=m->stuck_rx ? 1 : 13; break;
    case 0x35:
        if (m->tx_abort_residue) m->corrupted_transmissions++;
        assert(m->txcount>2 && m->txcount==m->tx[0]+1u);
        assert(m->tx[0]<=47 && m->tx[1]!=0 && m->tx[1]!=255);
        assert(m->transmissions<8);
        assert(dc_decode_raw(m->tx+2,m->tx[0]-1u,&m->tx_log[m->transmissions])==DC_OK);
        m->transmissions++; m->reg[0x35]=19; break;
    case 0x36:
        if (m->reg[0x35]==19) m->tx_abort_residue=true;
        m->reg[0x35]=1; break;
    case 0x3a: m->rxcount=0; m->reg[0x3b]=0; break;
    case 0x3b: m->txcount=0; m->reg[0x3a]=0; break;
    default: assert(false);
    }
    return true;
}
static void on_receive(void *ctx,const dc_frame_t *f) {
    mock_t *m=ctx; m->received++; m->last_receive=*f;
}
static void on_complete(void *ctx,dc_radio_completion_t c,const dc_frame_t *req,const dc_frame_t *reply) {
    mock_t *m=ctx; (void)req;
    m->completed++; m->completion=c;
    assert((c==DC_RF_COMPLETE)==(reply!=NULL));
}
static dc_radio_port_t mock_port(mock_t *m) {
    dc_radio_port_t p={m,mock_read,mock_write,mock_read_burst,mock_write_burst,mock_strobe};
    return p;
}
static void initialize(dc_radio_t *r,mock_t *m,uint32_t now) {
    dc_radio_port_t p;
    memset(m,0,sizeof(*m)); p=mock_port(m);
    assert(dc_radio_init(r,&p,DC_RF_GROUND_ADDRESS,on_receive,on_complete,m,now));
    assert(!dc_radio_ready(r));
    dc_radio_tick(r,now); dc_radio_tick(r,now+1);
    assert(dc_radio_ready(r));
    assert(r->stats.recoveries==0);
    dc_radio_set_reply_session(r,0xabcdef09u,now+1);
    dc_radio_tick(r,now+1); assert(dc_radio_ready(r));
}
static dc_frame_t request(uint8_t type) {
    dc_frame_t f;
    memset(&f,0,sizeof(f)); f.type=type; f.src=DC_HUB; f.dst=DC_AIR;
    f.session=0x12345678; f.seq=0x2345; f.len=DC_PAYLOAD_MAX;
    memset(f.payload,0xa5,f.len); return f;
}
static dc_frame_t response(const dc_frame_t *q) {
    dc_frame_t f=*q; f.src=q->dst; f.dst=q->src; f.seq=0x4567;
    f.session=0xabcdef09u; /* AIR binding differs from the HUB session. */
    f.type=q->type==DC_TEST_REQUEST ? DC_TEST_RESULT : DC_TELEMETRY;
    dc_put_u16(f.payload,q->seq); return f;
}
static void inject(mock_t *m,const dc_frame_t *f,uint8_t address,bool hw_crc,bool app_crc) {
    size_t n=dc_encode_raw(f,m->rx+2,46);
    assert(n && m->rxcount==0);
    m->rx[0]=(uint8_t)(n+1); m->rx[1]=address;
    if (!app_crc) m->rx[n+1]^=1;
    m->rx[n+2]=0x80; m->rx[n+3]=(hw_crc ? 0x80 : 0)|0x35;
    m->rxcount=n+4; assert(m->rxcount<=50); m->reg[0x35]=1;
}
static void tx_complete(dc_radio_t *r,mock_t *m,uint32_t now,bool irq) {
    m->txcount=0; m->reg[0x35]=1;
    if (irq) dc_radio_gdo_event(r,false);
    dc_radio_tick(r,now); dc_radio_tick(r,now+1);
}

static void test_profile_and_initialization(void) {
    dc_radio_t r; mock_t m; dc_radio_port_t p;
    initialize(&r,&m,0);
    assert(m.reg[0x04]==0xd3 && m.reg[0x05]==0x91 && m.reg[0x06]==47);
    assert(m.reg[0x07]==0x05 && m.reg[0x08]==0x45 && m.reg[0x09]==1);
    assert(m.reg[0x0d]==0x10 && m.reg[0x0e]==0xa7 && m.reg[0x0f]==0x62);
    assert(m.reg[0x10]==0xca && m.reg[0x11]==0x83 && m.reg[0x12]==0x13);
    assert(m.reg[0x13]==0x22 && m.reg[0x15]==0x35 && m.power==0xc0);
    memset(&m,0,sizeof(m)); p=mock_port(&m); m.bad_version=true;
    assert(!dc_radio_init(&r,&p,1,on_receive,on_complete,&m,0));
    assert(r.state==DC_RADIO_FAULT);
    memset(&m,0,sizeof(m)); m.bad_readback=true;
    assert(!dc_radio_init(&r,&p,1,on_receive,on_complete,&m,0));
    memset(&m,0,sizeof(m)); m.stuck_cal=true;
    assert(dc_radio_init(&r,&p,1,on_receive,on_complete,&m,0));
    dc_radio_tick(&r,20); assert(r.state==DC_RADIO_FAULT);
    memset(&m,0,sizeof(m)); m.stuck_rx=true;
    assert(dc_radio_init(&r,&p,1,on_receive,on_complete,&m,0));
    dc_radio_tick(&r,0); dc_radio_tick(&r,21); assert(r.state==DC_RADIO_FAULT);
    memset(&m,0,sizeof(m)); m.failed_io=true;
    assert(!dc_radio_init(&r,&p,1,on_receive,on_complete,&m,0));
    puts("radio: profile, identity/readback, bounded calibration/RX and SPI failure OK");
}
static void test_receive_integrity_and_recovery(void) {
    dc_radio_t r; mock_t m; dc_frame_t f=request(DC_TEST_REQUEST); unsigned calls;
    initialize(&r,&m,0);
    calls=m.spi_calls; dc_radio_gdo_event(&r,true); dc_radio_gdo_event(&r,false);
    assert(m.spi_calls==calls);
    inject(&m,&f,1,true,true); dc_radio_tick(&r,2);
    assert(m.received==1 && m.last_receive.seq==f.seq && r.stats.rx_valid==1);
    assert(r.stats.recoveries==0); /* normal reception is not fault recovery */
    dc_radio_tick(&r,3); inject(&m,&f,1,false,true); dc_radio_tick(&r,4);
    assert(m.received==1 && r.stats.rx_hw_crc==1);
    dc_radio_tick(&r,5); inject(&m,&f,1,true,false); dc_radio_tick(&r,6);
    assert(m.received==1 && r.stats.rx_app_crc==1);
    dc_radio_tick(&r,7); inject(&m,&f,0,true,true); dc_radio_tick(&r,8);
    assert(m.received==1 && r.stats.rx_invalid==1);
    dc_radio_tick(&r,9); m.rx[0]=48; m.rx[1]=1; m.rxcount=2; dc_radio_tick(&r,10);
    assert(r.stats.rx_invalid==2 && m.rxcount==0);
    dc_radio_tick(&r,11); m.reg[0x3b]=0x80; m.reg[0x35]=17; dc_radio_tick(&r,12);
    assert(r.stats.rx_overflow==1 && m.reg[0x3b]==0);
    dc_radio_tick(&r,13); m.rx[0]=47; m.rxcount=1; dc_radio_tick(&r,14);
    dc_radio_tick(&r,34); assert(r.stats.rx_partial_timeout==1 && m.rxcount==0);
    dc_radio_tick(&r,35); inject(&m,&f,1,true,true); dc_radio_tick(&r,36);
    assert(m.received==2);
    puts("radio: maximum FIFO packet, IRQ-only marker, hardware/application CRC, address and FIFO recovery OK");
}
static void test_transaction_success_and_matching(void) {
    dc_radio_t r; mock_t m; dc_frame_t q=request(DC_TELEMETRY_QUERY),f=response(&q),wrong=f;
    initialize(&r,&m,0);
    assert(dc_radio_request(&r,&q,2,2));
    assert(!dc_radio_request(&r,&q,2,3) && !dc_radio_send_untracked(&r,&q,2,3));
    tx_complete(&r,&m,10,true);
    assert(r.transaction==DC_RF_TXN_WAIT_REPLY && r.reply_deadline==40);
    wrong.payload[0]^=1; inject(&m,&wrong,1,true,true); dc_radio_tick(&r,12);
    assert(m.completed==0 && m.received==1);
    dc_radio_tick(&r,13); inject(&m,&f,1,true,true); dc_radio_tick(&r,14);
    assert(m.completed==1 && m.completion==DC_RF_COMPLETE && m.received==2);
    assert(!dc_radio_transaction_active(&r) && r.stats.transaction_ok==1);
    dc_radio_tick(&r,15); inject(&m,&f,1,true,true); dc_radio_tick(&r,16);
    assert(m.completed==1 && m.received==3); /* duplicates intentionally reach node cache */
    puts("radio: one in-flight request, reply reference/session routing and duplicate delivery OK");
}
static void test_retries_deadline_and_untracked_watchdog(void) {
    dc_radio_t r; mock_t m; dc_frame_t q=request(DC_TEST_REQUEST),f=response(&q);
    unsigned i;
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    tx_complete(&r,&m,8,false); /* MARCSTATE/FIFO completes even if IRQ was missed */
    dc_radio_tick(&r,38); assert(r.transaction==DC_RF_TXN_BACKOFF && r.retry_at==43);
    dc_radio_tick(&r,42); assert(m.transmissions==1);
    dc_radio_tick(&r,43); assert(m.transmissions==2 && r.transaction_deadline==202);
    tx_complete(&r,&m,48,true);
    dc_radio_tick(&r,78); assert(r.retry_at==88);
    dc_radio_tick(&r,88); assert(m.transmissions==3 && r.transaction_deadline==202);
    tx_complete(&r,&m,93,true); dc_radio_tick(&r,123);
    assert(m.completed==1 && m.completion==DC_RF_TIMEOUT && r.stats.retries==2);
    for (i=0;i<3;i++) {
        assert(m.tx_log[i].seq==q.seq && m.tx_log[i].session==q.session);
        assert(memcmp(m.tx_log[i].payload,q.payload,q.len)==0);
    }
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    tx_complete(&r,&m,8,true); dc_radio_tick(&r,38);
    inject(&m,&f,1,true,true); dc_radio_tick(&r,40);
    assert(m.completed==1 && m.completion==DC_RF_COMPLETE && m.transmissions==1);
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    dc_radio_tick(&r,202);
    assert(m.completed==1 && m.completion==DC_RF_TIMEOUT && !dc_radio_transaction_active(&r));
    assert(r.stats.tx_timeout==1 && r.stats.transaction_timeout==1);
    dc_radio_tick(&r,203); dc_radio_tick(&r,204); assert(dc_radio_ready(&r));
    assert(dc_radio_send_untracked(&r,&q,2,204)); dc_radio_tick(&r,224);
    assert(r.stats.tx_timeout==2); dc_radio_tick(&r,225); dc_radio_tick(&r,226); assert(dc_radio_ready(&r));
    assert(dc_radio_send_untracked(&r,&q,2,226));
    m.reg[0x3a]=0x80; m.reg[0x35]=22; dc_radio_tick(&r,227);
    assert(r.stats.tx_underflow==1); dc_radio_tick(&r,228); dc_radio_tick(&r,229); assert(dc_radio_ready(&r));
    puts("radio: 5/10 ms backoff, two retries, unchanged transaction/deadline, late reply, TX watchdog/underflow OK");
}
static void test_cancel_wrap_and_partial_receive(void) {
    dc_radio_t r; mock_t m; dc_frame_t q=request(DC_TEST_REQUEST),f=response(&q);
    uint8_t saved[64]; size_t n; unsigned i;
    initialize(&r,&m,0xfffffff0u);
    assert(dc_radio_request(&r,&q,2,0xfffffff2u));
    dc_radio_tick(&r,5); assert(r.stats.tx_timeout==0);
    dc_radio_tick(&r,6); assert(r.stats.tx_timeout==1 && r.transaction==DC_RF_TXN_BACKOFF);
    dc_radio_cancel(&r,7); dc_radio_tick(&r,8);
    assert(!dc_radio_transaction_active(&r) && dc_radio_ready(&r) && m.completed==0);
    inject(&m,&f,1,true,true); n=m.rxcount; memcpy(saved,m.rx,n);
    m.rxcount=1; m.reg[0x35]=13; dc_radio_tick(&r,9);
    assert(m.received==0 && !dc_radio_ready(&r));
    for (i=1;i<n;i++) {
        m.rx[m.rxcount++]=saved[i]; dc_radio_tick(&r,10);
    }
    assert(m.received==1 && m.rxcount==0);
    dc_radio_tick(&r,11); assert(dc_radio_ready(&r));
    puts("radio: time counter wrap, cancel/reset, incremental RX FIFO and return-to-RX OK");
}
static void test_binding_and_io_fault(void) {
    dc_radio_t r; mock_t m; dc_frame_t q=request(DC_TEST_REQUEST),f=response(&q);
    initialize(&r,&m,0);
    dc_radio_set_reply_session(&r,0,2); dc_radio_tick(&r,3);
    assert(!dc_radio_request(&r,&q,2,4));
    dc_radio_set_reply_session(&r,f.session,5); dc_radio_tick(&r,6);
    assert(dc_radio_request(&r,&q,2,7)); tx_complete(&r,&m,10,true);
    f.session=q.session; inject(&m,&f,1,true,true); dc_radio_tick(&r,12);
    assert(m.completed==0 && m.received==1);
    dc_radio_set_reply_session(&r,0,13); dc_radio_tick(&r,14);
    assert(!dc_radio_transaction_active(&r) && m.completed==0);
    dc_radio_set_reply_session(&r,0xabcdef09u,15); dc_radio_tick(&r,16);
    assert(dc_radio_request(&r,&q,2,17)); m.failed_io=true; dc_radio_tick(&r,18);
    assert(r.state==DC_RADIO_FAULT && m.completed==1 && m.completion==DC_RF_IO_ERROR);
    dc_radio_tick(&r,19); assert(m.completed==1);
    puts("radio: independent AIR session binding, old-session rejection, binding reset and in-flight SPI fault OK");
}
static void test_sync_before_length_and_recovery_count(void) {
    dc_radio_t r; mock_t m; dc_frame_t q=request(DC_TEST_REQUEST);
    unsigned calls;
    initialize(&r,&m,0);
    calls=m.spi_calls;
    dc_radio_gdo_event(&r,true); /* sync detected, length not yet in FIFO */
    assert(m.spi_calls==calls && !dc_radio_ready(&r));
    assert(!dc_radio_send_untracked(&r,&q,2,2));
    assert(!dc_radio_request(&r,&q,2,2) && m.spi_calls==calls && m.transmissions==0);
    dc_radio_tick(&r,2);
    assert(r.rx_sync_pending && r.partial_deadline==22);
    dc_radio_tick(&r,21); assert(r.stats.rx_sync_timeout==0);
    dc_radio_tick(&r,22);
    assert(r.stats.rx_sync_timeout==1 && r.stats.rx_partial_timeout==0);
    assert(r.stats.recoveries==1 && r.gdo_level_event==r.gdo_ignored_event);
    dc_radio_tick(&r,23); assert(dc_radio_ready(&r));
    /* A new rising event after reset must be visible. Main never overwrites the
     * ISR word; the abandoned generation alone is ignored. */
    dc_radio_gdo_event(&r,false); dc_radio_gdo_event(&r,true);
    assert(!dc_radio_ready(&r)); dc_radio_tick(&r,24);
    inject(&m,&q,1,true,true); dc_radio_gdo_event(&r,false); dc_radio_tick(&r,25);
    assert(m.received==1 && r.stats.recoveries==1);
    dc_radio_tick(&r,26); assert(dc_radio_ready(&r));
    inject(&m,&q,1,true,true); dc_radio_tick(&r,27);
    assert(m.received==2 && r.stats.recoveries==1);
    puts("radio: SYNC-before-length blocks TX, bounded stuck-SYNC recovery, new IRQ generation and correct recovery count OK");
}
static void status_script(mock_t *m,uint8_t address,uint8_t first,uint8_t second,uint8_t third) {
    m->status_address=address; m->status_script[0]=first; m->status_script[1]=second;
    m->status_script[2]=third; m->status_script_count=3; m->status_script_used=0;
}
static void test_status_stability_and_timeout_boundaries(void) {
    dc_radio_t r; mock_t m; dc_radio_port_t p; dc_frame_t q=request(DC_TEST_REQUEST);
    unsigned calls;
    initialize(&r,&m,0);
    status_script(&m,0x35,17,13,13); dc_radio_tick(&r,2);
    assert(r.stats.rx_overflow==0 && r.stats.status_unstable==0);
    inject(&m,&q,1,true,true);
    status_script(&m,0x3b,0x80,50,50); dc_radio_tick(&r,3);
    assert(m.received==1 && r.stats.rx_overflow==0 && r.stats.recoveries==0);
    dc_radio_tick(&r,4); assert(dc_radio_send_untracked(&r,&q,2,5));
    status_script(&m,0x3a,0x80,0,0); tx_complete(&r,&m,10,true);
    assert(r.stats.tx_ok==1 && r.stats.tx_underflow==0);
    assert(dc_radio_send_untracked(&r,&q,2,12));
    m.status_address=0x35; m.alternating_status=true; calls=m.spi_calls;
    dc_radio_tick(&r,13);
    assert(m.spi_calls-calls==4 && r.stats.status_unstable==1 && r.state==DC_RADIO_TX);
    dc_radio_tick(&r,32); /* unstable MARCSTATE cannot postpone TX watchdog */
    assert(r.stats.tx_timeout==1 && r.stats.io_errors==0 && r.stats.recoveries==1);
    memset(&m,0,sizeof(m)); p=mock_port(&m); m.stuck_cal=true;
    assert(dc_radio_init(&r,&p,1,on_receive,on_complete,&m,0));
    m.status_address=0x35; m.alternating_status=true;
    dc_radio_tick(&r,19); dc_radio_tick(&r,20);
    assert(r.state==DC_RADIO_FAULT && r.stats.calibration_timeout==1 && r.stats.io_errors==0);
    memset(&m,0,sizeof(m)); m.stuck_rx=true;
    assert(dc_radio_init(&r,&p,1,on_receive,on_complete,&m,0)); dc_radio_tick(&r,0);
    m.status_address=0x35; m.alternating_status=true;
    dc_radio_tick(&r,19); dc_radio_tick(&r,20);
    assert(r.state==DC_RADIO_FAULT && r.stats.rx_start_timeout==1 && r.stats.io_errors==0);
    initialize(&r,&m,0); assert(dc_radio_send_untracked(&r,&q,2,2));
    m.txcount=0; m.reg[0x35]=1; dc_radio_tick(&r,22);
    assert(r.stats.tx_timeout==1 && r.stats.tx_ok==0); /* exact 20 ms boundary */
    initialize(&r,&m,0); assert(dc_radio_send_untracked(&r,&q,2,2));
    tx_complete(&r,&m,21,false);
    assert(r.stats.tx_timeout==0 && r.stats.tx_ok==1 && r.stats.recoveries==0);
    puts("radio: bounded consistent status reads, transient bit rejection, independent watchdogs and exact deadline statistics OK");
}
static void test_received_signal_diagnostics(void) {
    dc_radio_t r; mock_t m; dc_frame_t q=request(DC_TEST_REQUEST);
    initialize(&r,&m,0); assert(!r.rssi_valid);
    inject(&m,&q,1,true,true);
    m.rx[m.rxcount-2u]=0x4b; m.rx[m.rxcount-1u]=0xe7; dc_radio_tick(&r,2);
    assert(r.rssi_valid && r.last_rx_ms==2 && r.last_rssi_x2_dbm==-73 && r.last_lqi==0x67);
    dc_radio_tick(&r,3); inject(&m,&q,1,true,true);
    m.rx[m.rxcount-2u]=0xa5; m.rx[m.rxcount-1u]=0xff; dc_radio_tick(&r,4);
    assert(r.last_rx_ms==4 && r.last_rssi_x2_dbm==-239 && r.last_lqi==0x7f);
    dc_radio_tick(&r,5); inject(&m,&q,1,false,true);
    m.rx[m.rxcount-2u]=0x20; dc_radio_tick(&r,6);
    assert(r.last_rx_ms==4 && r.last_rssi_x2_dbm==-239 && r.last_lqi==0x7f);
    dc_radio_tick(&r,7); inject(&m,&q,1,true,false); dc_radio_tick(&r,8);
    assert(r.last_rx_ms==4 && r.last_rssi_x2_dbm==-239 && r.last_lqi==0x7f);
    puts("radio: good-frame RSSI signed conversion, half-dBm precision, CRC_OK-masked LQI and invalid-frame freshness OK");
}
static void test_reply_timer_independent_of_status(void) {
    dc_radio_t r;
    mock_t m;
    dc_frame_t q=request(DC_TEST_REQUEST), f=response(&q);
    initialize(&r,&m,0);
    assert(dc_radio_request(&r,&q,2,2));
    tx_complete(&r,&m,8,true);
    assert(r.reply_deadline==38);
    m.status_address=0x35; m.alternating_status=true;
    dc_radio_tick(&r,37);
    assert(r.transaction==DC_RF_TXN_WAIT_REPLY);
    dc_radio_tick(&r,38);
    assert(r.transaction==DC_RF_TXN_BACKOFF && r.retry_at==43);
    dc_radio_tick(&r,42);
    assert(r.transaction==DC_RF_TXN_BACKOFF && m.transmissions==1);
    m.alternating_status=false;
    dc_radio_tick(&r,43);
    assert(r.transaction==DC_RF_TXN_TX && m.transmissions==2 && r.transaction_deadline==202);
    tx_complete(&r,&m,48,true);
    m.alternating_status=true;
    dc_radio_tick(&r,78);
    assert(r.transaction==DC_RF_TXN_BACKOFF && r.retry_at==88);
    m.alternating_status=false;
    dc_radio_tick(&r,88);
    assert(r.transaction==DC_RF_TXN_TX && m.transmissions==3);
    tx_complete(&r,&m,93,true);
    m.alternating_status=true;
    dc_radio_tick(&r,123);
    assert(m.completed==1 && m.completion==DC_RF_TIMEOUT && r.transaction==DC_RF_TXN_IDLE);
    assert(r.stats.retries==2 && r.stats.transaction_timeout==1);
    /* At a nonfinal reply deadline, expiry creates backoff first, while a
     * complete valid FIFO reply may still satisfy that original transaction. */
    initialize(&r,&m,0);
    assert(dc_radio_request(&r,&q,2,2)); tx_complete(&r,&m,8,true);
    inject(&m,&f,1,true,true); dc_radio_tick(&r,38);
    assert(m.completed==1 && m.completion==DC_RF_COMPLETE && m.transmissions==1);
    /* The same timeout arithmetic remains valid over uint32_t wrap. */
    initialize(&r,&m,0xffffffe0u);
    assert(dc_radio_request(&r,&q,2,0xffffffe2u));
    tx_complete(&r,&m,0xffffffe8u,true);
    assert(r.reply_deadline==6);
    m.status_address=0x35; m.alternating_status=true;
    dc_radio_tick(&r,5); assert(r.transaction==DC_RF_TXN_WAIT_REPLY);
    dc_radio_tick(&r,6); assert(r.transaction==DC_RF_TXN_BACKOFF && r.retry_at==11);
    puts("radio: reply timer survives unstable status, exact expiry, retry limits and timer wrap OK");
}
static void test_rx_fifo_silicon_errata(void) {
    dc_radio_t r;
    mock_t m, demonstration;
    dc_frame_t f=request(DC_TEST_REQUEST);
    uint8_t saved[64], length;
    size_t size;
    initialize(&r,&m,0);
    inject(&m,&f,1,true,true); size=m.rxcount; memcpy(saved,m.rx,size);
    m.rxcount=1; m.reg[0x35]=13; m.errata_rx_collision=true;
    m.errata_arriving_byte=saved[1];
    /* Prove the fixture exhibits the silicon failure for the old read-last-byte
     * operation rather than merely asserting a particular implementation. */
    demonstration=m;
    assert(mock_read_burst(&demonstration,0x3f,&length,1));
    assert(demonstration.errata_duplications==1 && demonstration.rxcount==2);
    assert(demonstration.rx[0]==length && demonstration.rx[1]==saved[1]);
    dc_radio_tick(&r,2);
    assert(m.rxcount==1 && r.rx_length==0 && m.errata_duplications==0);
    assert(r.rx_sync_pending && r.rx_fifo_seen && r.partial_deadline==22);
    assert(!dc_radio_ready(&r) && !dc_radio_send_untracked(&r,&f,2,2));
    m.rx[m.rxcount++]=saved[1]; dc_radio_tick(&r,3);
    assert(r.rx_length==saved[0] && m.rxcount==1 && m.rx[0]==saved[1]);
    assert(m.errata_duplications==0 && m.received==0);
    memcpy(m.rx+m.rxcount,saved+2,size-3); m.rxcount+=size-3;
    dc_radio_tick(&r,4); /* Last status byte has not arrived: do not empty FIFO. */
    assert(m.received==0 && m.rxcount==size-2 && m.errata_duplications==0);
    m.rx[m.rxcount++]=saved[size-1]; m.reg[0x35]=1; dc_radio_tick(&r,5);
    assert(m.received==1 && m.rxcount==0 && m.errata_duplications==0);
    assert(m.last_receive.seq==f.seq && m.last_receive.len==f.len);
    /* Missing GDO plus an isolated length must still start a bounded timeout,
     * even if subsequent status samples are unstable. */
    initialize(&r,&m,0); m.rx[0]=47; m.rxcount=1;
    dc_radio_tick(&r,2);
    assert(r.rx_sync_pending && r.rx_fifo_seen && r.partial_deadline==22 && m.rxcount==1);
    m.status_address=0x35; m.alternating_status=true;
    dc_radio_tick(&r,21); assert(r.stats.rx_partial_timeout==0);
    dc_radio_tick(&r,22);
    assert(r.stats.rx_partial_timeout==1 && r.stats.rx_sync_timeout==0 && m.rxcount==0);
    m.alternating_status=false; dc_radio_tick(&r,23); dc_radio_tick(&r,24); assert(dc_radio_ready(&r));
    /* SWRZ020E pp.8-9: overflow need not set MARCSTATE or the overflow flag.
     * Our PKTLEN=47 bounds a normal FIFO to 50; reject impossible larger counts. */
    m.rxcount=65; m.reg[0x35]=13; m.reg[0x3b]=0; dc_radio_tick(&r,24);
    assert(r.stats.rx_overflow==1 && m.rxcount==0 && r.state==DC_RADIO_RX_START);
    dc_radio_tick(&r,25); assert(dc_radio_ready(&r));
    puts("radio: silicon RX last-byte duplication avoided, lone length watchdog and hidden overflow recovery OK");
}
static void test_tx_abort_hardware_reset(void) {
    dc_radio_t r;
    mock_t m, demonstration;
    dc_frame_t q=request(DC_TEST_REQUEST);
    uint8_t status, packet[64];
    size_t size;
    initialize(&r,&m,0); assert(m.resets==1);
    assert(dc_radio_request(&r,&q,2,2));
    /* SWRZ020E p.10 fixture: SIDLE during TX leaves modulator state that a
     * FIFO flush does not clear; the next packet repeats its first byte. */
    demonstration=m; size=m.txcount; memcpy(packet,m.tx,size);
    assert(mock_strobe(&demonstration,0x36,&status));
    assert(mock_strobe(&demonstration,0x3b,&status) && demonstration.tx_abort_residue);
    assert(mock_write_burst(&demonstration,0x3f,packet,size));
    assert(mock_strobe(&demonstration,0x35,&status));
    assert(demonstration.corrupted_transmissions==1);
    assert(mock_strobe(&demonstration,0x30,&status) && !demonstration.tx_abort_residue);
    dc_radio_tick(&r,22);
    assert(m.resets==2 && r.state==DC_RADIO_CALIBRATING);
    assert(r.stats.tx_timeout==1 && r.stats.recoveries==1 && r.stats.retries==0);
    assert(r.transaction==DC_RF_TXN_BACKOFF && r.retry_at==27 && r.attempts==1);
    assert(r.transaction_deadline==202 && r.request.seq==q.seq && r.request.session==q.session);
    assert(r.reply_session==0xabcdef09u && m.reg[0x09]==1 && m.reg[0x08]==0x45);
    dc_radio_tick(&r,23); dc_radio_tick(&r,24);
    assert(dc_radio_ready(&r) && m.transmissions==1);
    dc_radio_tick(&r,27);
    assert(m.transmissions==2 && m.corrupted_transmissions==0 && r.attempts==2);
    assert(r.stats.retries==1 && r.transaction_deadline==202);
    assert(m.tx_log[1].seq==q.seq && m.tx_log[1].session==q.session);
    assert(m.tx_log[1].len==q.len && memcmp(m.tx_log[1].payload,q.payload,q.len)==0);
    tx_complete(&r,&m,32,true);
    assert(m.resets==2 && r.stats.tx_ok==1); /* Natural completion must not reset. */
    dc_radio_tick(&r,62); assert(r.retry_at==72);
    dc_radio_tick(&r,72);
    assert(m.transmissions==3 && m.resets==2 && r.transaction_deadline==202);
    tx_complete(&r,&m,77,true); dc_radio_tick(&r,107);
    assert(m.completed==1 && m.completion==DC_RF_TIMEOUT && r.attempts==3);
    assert(r.stats.retries==2 && m.corrupted_transmissions==0);
    /* Cancellation clears the transaction before resetting active TX. Neither
     * calibration nor a later tick may revive it or issue a completion callback. */
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    dc_radio_cancel(&r,5);
    assert(m.resets==2 && r.state==DC_RADIO_CALIBRATING && !dc_radio_transaction_active(&r));
    assert(r.request.seq==0 && r.stats.recoveries==0);
    dc_radio_tick(&r,6); dc_radio_tick(&r,7); dc_radio_tick(&r,202);
    assert(dc_radio_ready(&r) && m.transmissions==1 && m.completed==0);
    assert(dc_radio_send_untracked(&r,&q,2,203)); tx_complete(&r,&m,209,true);
    assert(m.resets==2 && m.corrupted_transmissions==0);
    /* An existing absolute deadline is not extended by the new calibration. */
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    r.transaction_deadline=25; m.stuck_cal=true; dc_radio_tick(&r,22);
    assert(r.state==DC_RADIO_CALIBRATING && r.transaction_deadline==25);
    dc_radio_tick(&r,25);
    assert(m.completed==1 && m.completion==DC_RF_TIMEOUT && m.transmissions==1);
    dc_radio_tick(&r,42);
    assert(r.state==DC_RADIO_FAULT && r.stats.calibration_timeout==1 && m.completed==1);
    /* SRES and profile-readback failures stop recovery without restarting TX. */
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    m.failed_reset=true; dc_radio_tick(&r,22);
    assert(r.state==DC_RADIO_FAULT && m.completed==1 && m.completion==DC_RF_IO_ERROR);
    assert(r.stats.io_errors==1 && r.stats.tx_timeout==1 && r.stats.recoveries==0);
    dc_radio_tick(&r,202); assert(m.completed==1 && m.transmissions==1);
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    m.bad_readback=true; dc_radio_tick(&r,22);
    assert(r.state==DC_RADIO_FAULT && m.resets==2 && m.completed==1 && m.completion==DC_RF_IO_ERROR);
    assert(r.stats.io_errors==1 && r.stats.recoveries==0);
    puts("radio: TX abort SRES clears modulator residue, preserves retries/deadline, cancel and reset failures OK");
}
static void test_rx_unobservable_status_timeout(void) {
    dc_radio_t r;
    mock_t m;
    dc_frame_t q=request(DC_TEST_REQUEST);
    initialize(&r,&m,0);
    m.rx[0]=47; m.rxcount=1; /* No GDO event and no prior FIFO observation. */
    m.status_address=0x35; m.alternating_status=true;
    dc_radio_tick(&r,2);
    assert(r.status_pending && r.status_deadline==22 && !dc_radio_ready(&r));
    assert(!r.rx_sync_pending && !r.rx_fifo_seen && m.received==0);
    /* Stable MARCSTATE alone cannot restart the window while RXBYTES keeps
     * changing. Neither alternating register may postpone the original limit. */
    m.status_address=0x3b; dc_radio_tick(&r,10);
    assert(r.status_pending && r.status_deadline==22 && !r.rx_fifo_seen);
    m.status_address=0x35; dc_radio_tick(&r,21);
    assert(r.stats.status_timeout==0 && r.status_deadline==22);
    dc_radio_tick(&r,22);
    assert(r.stats.status_timeout==1 && r.stats.rx_partial_timeout==0);
    assert(r.stats.recoveries==1 && r.state==DC_RADIO_CALIBRATING && m.resets==2);
    assert(m.rxcount==0 && m.received==0);
    m.alternating_status=false; dc_radio_tick(&r,23); dc_radio_tick(&r,24);
    assert(dc_radio_ready(&r));
    inject(&m,&q,1,true,true); dc_radio_tick(&r,25); assert(m.received==1);
    /* The readiness check during a send also starts watchdogs; callers need
     * not wait for a successful MARCSTATE sample to discover a stalled FIFO. */
    initialize(&r,&m,0); m.rx[0]=47; m.rxcount=1;
    m.status_address=0x3b; m.alternating_status=true;
    assert(!dc_radio_send_untracked(&r,&q,2,2));
    assert(r.status_pending && r.status_deadline==22 && !r.rx_fifo_seen);
    dc_radio_tick(&r,22); assert(r.stats.status_timeout==1 && m.transmissions==0);
    initialize(&r,&m,0); m.rx[0]=47; m.rxcount=1;
    assert(!dc_radio_send_untracked(&r,&q,2,2));
    assert(r.rx_fifo_seen && r.rx_sync_pending && r.partial_deadline==22);
    m.status_address=0x35; m.alternating_status=true; dc_radio_tick(&r,3);
    dc_radio_tick(&r,22);
    assert(r.stats.rx_partial_timeout==1 && r.stats.status_timeout==0 && m.rxcount==0);
    assert(m.resets==2 && r.state==DC_RADIO_CALIBRATING);
    /* A stable but impossible hardware state during software RX is equally
     * bounded; a short transition is allowed to recover without resetting. */
    initialize(&r,&m,0); m.reg[0x35]=8; dc_radio_tick(&r,2);
    assert(r.status_pending && !dc_radio_ready(&r));
    dc_radio_tick(&r,21); dc_radio_tick(&r,22);
    assert(r.stats.status_timeout==1 && r.stats.status_unstable==0 && m.resets==2);
    initialize(&r,&m,0); m.reg[0x35]=8; dc_radio_tick(&r,2);
    m.reg[0x35]=13; dc_radio_tick(&r,10); dc_radio_tick(&r,22);
    assert(!r.status_pending && dc_radio_ready(&r) && m.resets==1);
    /* This recovery neither completes nor extends an otherwise active request. */
    initialize(&r,&m,0); assert(dc_radio_request(&r,&q,2,2));
    tx_complete(&r,&m,8,true);
    m.rx[0]=47; m.rxcount=1; m.status_address=0x35; m.alternating_status=true;
    dc_radio_tick(&r,10); dc_radio_tick(&r,30);
    assert(r.stats.status_timeout==1 && r.transaction==DC_RF_TXN_WAIT_REPLY);
    assert(r.transaction_deadline==202 && r.reply_deadline==38 && r.attempts==1 && m.completed==0);
    m.alternating_status=false; dc_radio_tick(&r,31); dc_radio_tick(&r,32);
    dc_radio_tick(&r,38); dc_radio_tick(&r,43);
    assert(m.transmissions==2 && r.transaction_deadline==202 && r.stats.retries==1);
    assert(m.tx_log[1].seq==q.seq && m.tx_log[1].session==q.session);
    initialize(&r,&m,0xfffffff0u); m.reg[0x35]=8;
    dc_radio_tick(&r,0xfffffff2u); assert(r.status_deadline==6);
    dc_radio_tick(&r,5); assert(r.stats.status_timeout==0);
    dc_radio_tick(&r,6); assert(r.stats.status_timeout==1 && m.resets==2);
    initialize(&r,&m,0); m.reg[0x35]=19; dc_radio_tick(&r,2);
    assert(r.status_pending && !dc_radio_ready(&r));
    dc_radio_cancel(&r,3);
    assert(m.resets==2 && !m.tx_abort_residue && r.state==DC_RADIO_CALIBRATING);
    puts("radio: first-sample status noise/invalid MARC state bounded without GDO, FIFO visibility or deadline extension OK");
}
int main(void) {
    test_profile_and_initialization();
    test_receive_integrity_and_recovery();
    test_transaction_success_and_matching();
    test_retries_deadline_and_untracked_watchdog();
    test_cancel_wrap_and_partial_receive();
    test_binding_and_io_fault();
    test_sync_before_length_and_recovery_count();
    test_status_stability_and_timeout_boundaries();
    test_received_signal_diagnostics();
    test_reply_timer_independent_of_status();
    test_rx_fifo_silicon_errata();
    test_tx_abort_hardware_reset();
    test_rx_unobservable_status_timeout();
    puts("radio: all mock-register/FIFO tests passed (PC only)");
    return 0;
}
