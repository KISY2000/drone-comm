#include "dc_radio.h"
#include <string.h>

enum {
    REG_IOCFG0=0x02, REG_PKTCTRL1=0x07, REG_PKTCTRL0=0x08, REG_ADDR=0x09,
    REG_MARCSTATE=0x35, REG_TXBYTES=0x3a, REG_RXBYTES=0x3b,
    REG_PARTNUM=0x30, REG_VERSION=0x31, REG_PATABLE=0x3e, REG_FIFO=0x3f,
    CMD_SRES=0x30, CMD_SCAL=0x33, CMD_SRX=0x34, CMD_STX=0x35,
    CMD_SIDLE=0x36, CMD_SFRX=0x3a, CMD_SFTX=0x3b,
    MARC_IDLE=1, MARC_RX=13, MARC_RX_OVERFLOW=17, MARC_TX_UNDERFLOW=22
};

/* 26 MHz CC1101. BW=101.5625 kHz, rate=38.3835 kbps, deviation=20.6299 kHz.
 * MDMCFG2: GFSK and 30/32 sync detection; SYNC appears as D3 91 D3 91.
 * MCSM1: return to IDLE after either packet; main context explicitly rearms RX.
 * PKTCTRL1 is generated below: APPEND_STATUS + exact address, no broadcast. */
static const uint8_t profile[][2] = {
    {0x00,0x2e},{0x01,0x2e},{0x02,0x06},{0x03,0x47},
    {0x04,0xd3},{0x05,0x91},{0x06,DC_CC1101_PKTLEN},
    {0x07,0x05},{0x08,0x45},{0x0a,0x00},{0x0b,0x06},{0x0c,0x00},
    {0x0d,0x10},{0x0e,0xa7},{0x0f,0x62},{0x10,0xca},{0x11,0x83},
    {0x12,0x13},{0x13,0x22},{0x14,0xf8},{0x15,0x35},
    {0x16,0x07},{0x17,0x00},{0x18,0x18},{0x19,0x16},{0x1a,0x6c},
    {0x1b,0x43},{0x1c,0x40},{0x1d,0x91},{0x1e,0x87},{0x1f,0x6b},
    {0x20,0xfb},{0x21,0x56},{0x22,0x10},{0x23,0xe9},{0x24,0x2a},
    {0x25,0x00},{0x26,0x1f},{0x27,0x41},{0x28,0x00},
    {0x29,0x59},{0x2a,0x7f},{0x2b,0x3f},{0x2c,0x81},{0x2d,0x35},{0x2e,0x09}
};
typedef char dc_radio_fifo_capacity_check[(1 + DC_CC1101_PKTLEN + 2 <= 64) ? 1 : -1];

static bool expired(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
static bool read_reg(dc_radio_t *r, uint8_t address, uint8_t *value) {
    return r->port.read_reg(r->port.ctx,address,value);
}
typedef enum { STATUS_ERROR=-1, STATUS_WAIT=0, STATUS_OK=1 } status_result_t;
static void watch_status(dc_radio_t *r, uint32_t now) {
    if (!r->status_pending) {
        r->status_pending=true;
        r->status_deadline=now+DC_RF_TX_TIMEOUT_MS;
    }
}
/* CC1101 status registers may change while being sampled over SPI. Require two
 * consecutive equal samples, at most four reads. Unstable status defers work to
 * another tick; independent timers still expire, so this cannot spin forever. */
static status_result_t read_status(dc_radio_t *r, uint8_t address, uint8_t *value, uint32_t now) {
    uint8_t previous,current;
    unsigned i;
    if (!read_reg(r,address,&previous)) return STATUS_ERROR;
    for (i=0;i<3;i++) {
        if (!read_reg(r,address,&current)) return STATUS_ERROR;
        if (current == previous) { *value=current; return STATUS_OK; }
        previous=current;
    }
    r->stats.status_unstable++;
    watch_status(r,now);
    return STATUS_WAIT;
}
static bool rx_sync_high(const dc_radio_t *r) {
    uint32_t event=r->gdo_level_event;
    return (event & 1u) != 0 && event != r->gdo_ignored_event;
}
static bool strobe(dc_radio_t *r, uint8_t command) {
    uint8_t status;
    return r->port.strobe(r->port.ctx,command,&status);
}
static void complete(dc_radio_t *r, dc_radio_completion_t result, const dc_frame_t *reply) {
    dc_frame_t request;
    if (r->transaction == DC_RF_TXN_IDLE) return;
    request = r->request;
    r->transaction = DC_RF_TXN_IDLE;
    r->tracked_tx = false;
    if (result == DC_RF_COMPLETE) r->stats.transaction_ok++;
    else if (result == DC_RF_TIMEOUT) r->stats.transaction_timeout++;
    if (r->on_transaction) r->on_transaction(r->user,result,&request,reply);
}
static void fault(dc_radio_t *r) {
    r->stats.io_errors++;
    r->state = DC_RADIO_FAULT;
    r->rx_length = 0;
    r->rx_fifo_seen=false;
    r->status_pending=false;
    complete(r,DC_RF_IO_ERROR,NULL);
}
static void timed_fault(dc_radio_t *r) {
    r->state=DC_RADIO_FAULT;
    r->rx_length=0;
    r->rx_sync_pending=false;
    r->rx_fifo_seen=false;
    r->status_pending=false;
    complete(r,DC_RF_TIMEOUT,NULL);
}
static bool receive_mode(dc_radio_t *r, uint32_t now, bool flush) {
    if (!strobe(r,CMD_SIDLE) ||
        (flush && (!strobe(r,CMD_SFRX) || !strobe(r,CMD_SFTX)))) {
        fault(r);
        return false;
    }
    /* Ignore the generation aborted by SIDLE, before RX can raise a new one.
     * Never clear the ISR-owned word: a later interrupt remains observable. */
    r->gdo_ignored_event=r->gdo_level_event;
    if (!strobe(r,CMD_SRX)) { fault(r); return false; }
    r->rx_length = 0;
    r->rx_sync_pending=false;
    r->rx_fifo_seen=false;
    r->status_pending=false;
    r->state = DC_RADIO_RX_START;
    r->state_deadline = now + DC_RF_TX_TIMEOUT_MS;
    return true;
}
/* Reconfigure hardware without clearing software accounting or the original
 * transaction. SRES also clears the modulator state after an aborted TX:
 * SIDLE/SFTX alone is not sufficient for TI SWRZ020E p.10. */
static bool configure_hardware(dc_radio_t *r, uint32_t now) {
    uint8_t part,version,value,power=0xc0;
    size_t i;
    r->rx_length=0; r->rx_sync_pending=false; r->rx_fifo_seen=false; r->status_pending=false;
    if (!strobe(r,CMD_SRES) || !read_reg(r,REG_PARTNUM,&part) ||
        !read_reg(r,REG_VERSION,&version) || part != 0 || version == 0 || version == 0xff) {
        fault(r); return false;
    }
    for (i=0;i<sizeof(profile)/sizeof(profile[0]);i++) {
        if (!r->port.write_reg(r->port.ctx,profile[i][0],profile[i][1]) ||
            !read_reg(r,profile[i][0],&value) || value != profile[i][1]) {
            fault(r); return false;
        }
    }
    if (!r->port.write_reg(r->port.ctx,REG_ADDR,r->local_address) ||
        !read_reg(r,REG_ADDR,&value) || value != r->local_address ||
        !r->port.write_burst(r->port.ctx,REG_PATABLE,&power,1) ||
        !r->port.read_burst(r->port.ctx,REG_PATABLE,&value,1) || value != power ||
        !strobe(r,CMD_SCAL)) {
        fault(r); return false;
    }
    r->state=DC_RADIO_CALIBRATING;
    r->state_deadline=now+DC_RF_TX_TIMEOUT_MS;
    return true;
}
static bool recover(dc_radio_t *r, uint32_t now) {
    if (r->state == DC_RADIO_TX || r->status_pending) {
        if (!configure_hardware(r,now)) return false;
    } else if (!receive_mode(r,now,true)) return false;
    r->stats.recoveries++;
    return true;
}
bool dc_radio_init(dc_radio_t *r, const dc_radio_port_t *port, uint8_t address,
                   dc_radio_receive_fn on_frame, dc_radio_transaction_fn on_transaction,
                   void *user, uint32_t now) {
    if (!r || !port || !port->read_reg || !port->write_reg || !port->read_burst ||
        !port->write_burst || !port->strobe ||
        (address != DC_RF_GROUND_ADDRESS && address != DC_RF_AIR_ADDRESS)) return false;
    memset(r,0,sizeof(*r));
    r->port=*port; r->local_address=address; r->on_frame=on_frame;
    r->on_transaction=on_transaction; r->user=user;
    return configure_hardware(r,now);
}
void dc_radio_gdo_event(dc_radio_t *r, bool high) {
    if (!r) return;
    r->gdo_level_event=((r->gdo_level_event+2u)&~1u)|(high ? 1u : 0u);
    if (!high) r->gdo_falling_events++;
}
bool dc_radio_ready(const dc_radio_t *r) {
    return r && r->state == DC_RADIO_RX && r->rx_length == 0 &&
        !r->rx_sync_pending && !r->status_pending && !rx_sync_high(r);
}
bool dc_radio_transaction_active(const dc_radio_t *r) {
    return r && r->transaction != DC_RF_TXN_IDLE;
}
void dc_radio_set_reply_session(dc_radio_t *r, uint32_t session, uint32_t now) {
    if (!r || r->reply_session == session) return;
    r->reply_session=session;
    dc_radio_cancel(r,now);
}

static bool start_tx(dc_radio_t *r, const dc_frame_t *frame, uint8_t peer,
                     bool tracked, uint32_t now) {
    uint8_t packet[1+DC_CC1101_PKTLEN],rxbytes;
    size_t raw;
    status_result_t status;
    if (!dc_radio_ready(r) ||
        (peer != DC_RF_GROUND_ADDRESS && peer != DC_RF_AIR_ADDRESS) ||
        peer == r->local_address) return false;
    raw=dc_encode_raw(frame,packet+2,sizeof(packet)-2);
    if (!raw) return false;
    /* Do not abort a packet already arriving in RX. Polling tick drains it. */
    status=read_status(r,REG_RXBYTES,&rxbytes,now);
    if (status == STATUS_ERROR) { fault(r); return false; }
    if (status != STATUS_OK) return false;
    if (rxbytes & 0x7f) {
        r->rx_fifo_seen=true;
        if (!r->rx_sync_pending) {
            r->rx_sync_pending=true; r->partial_deadline=now+DC_RF_TX_TIMEOUT_MS;
        }
    }
    if (rxbytes || rx_sync_high(r)) return false;
    packet[0]=(uint8_t)(raw+1); packet[1]=peer;
    if (!strobe(r,CMD_SIDLE) || !strobe(r,CMD_SFTX) ||
        !r->port.write_burst(r->port.ctx,REG_FIFO,packet,raw+2)) {
        fault(r); return false;
    }
    r->tx_event_start=r->gdo_falling_events;
    if (!strobe(r,CMD_STX)) { fault(r); return false; }
    r->tracked_tx=tracked;
    r->state=DC_RADIO_TX;
    r->state_deadline=now+DC_RF_TX_TIMEOUT_MS;
    return true;
}
bool dc_radio_send_untracked(dc_radio_t *r, const dc_frame_t *frame, uint8_t peer, uint32_t now) {
    if (!r || !frame || dc_radio_transaction_active(r)) return false;
    return start_tx(r,frame,peer,false,now);
}
bool dc_radio_request(dc_radio_t *r, const dc_frame_t *frame, uint8_t peer, uint32_t now) {
    if (!r || !frame || !r->reply_session || dc_radio_transaction_active(r) ||
        (frame->type != DC_TELEMETRY_QUERY && frame->type != DC_TEST_REQUEST)) return false;
    if (!start_tx(r,frame,peer,true,now)) return false;
    r->request=*frame; r->peer_address=peer; r->attempts=1;
    r->transaction=DC_RF_TXN_TX;
    r->transaction_deadline=now+DC_RF_DEADLINE_MS;
    return true;
}
void dc_radio_cancel(dc_radio_t *r, uint32_t now) {
    if (!r) return;
    r->transaction=DC_RF_TXN_IDLE; r->tracked_tx=false;
    memset(&r->request,0,sizeof(r->request));
    r->rx_length=0;
    r->rx_sync_pending=false;
    r->rx_fifo_seen=false;
    if (r->state == DC_RADIO_TX || r->status_pending)
        (void)configure_hardware(r,now);
    else if (r->state != DC_RADIO_OFF && r->state != DC_RADIO_FAULT)
        (void)receive_mode(r,now,true);
}
static void retry_or_finish(dc_radio_t *r, uint32_t now) {
    r->tracked_tx=false;
    if (r->transaction == DC_RF_TXN_IDLE) return;
    if (r->attempts >= 3 || expired(now,r->transaction_deadline)) {
        complete(r,DC_RF_TIMEOUT,NULL);
    } else {
        r->transaction=DC_RF_TXN_BACKOFF;
        r->retry_at=now+(r->attempts == 1 ? 5u : 10u);
    }
}
static bool matches(const dc_radio_t *r, const dc_frame_t *frame) {
    uint8_t reply_type = r->request.type == DC_TELEMETRY_QUERY ? DC_TELEMETRY : DC_TEST_RESULT;
    return frame->type == reply_type && frame->src == r->request.dst &&
        frame->dst == r->request.src && frame->session == r->reply_session &&
        frame->len >= 2 && dc_get_u16(frame->payload) == r->request.seq;
}

static void receive_packet(dc_radio_t *r, uint32_t now, uint8_t marc) {
    uint8_t count,packet[DC_CC1101_PKTLEN+2];
    dc_frame_t frame;
    dc_result_t result;
    status_result_t status=read_status(r,REG_RXBYTES,&count,now);
    if (status == STATUS_ERROR) { fault(r); return; }
    if (status != STATUS_OK) return;
    /* Only a complete RX observation (legal MARCSTATE plus stable RXBYTES)
     * retires this watchdog. One good register must not hide the other's noise. */
    r->status_pending=false;
    if ((count & 0x80) || marc == MARC_RX_OVERFLOW ||
        (count & 0x7f) > 1u+DC_CC1101_PKTLEN+2u) {
        r->stats.rx_overflow++; (void)recover(r,now); return;
    }
    count &= 0x7f;
    if (count) {
        r->rx_fifo_seen=true;
        if (!r->rx_sync_pending) {
            r->partial_deadline=now+DC_RF_TX_TIMEOUT_MS;
            r->rx_sync_pending=true;
        }
    }
    if (!r->rx_length && count) {
        /* TI SWRZ020E p.2: never read the last FIFO byte while RX may still
         * append another byte. Keep the length until at least one byte follows.
         * The watchdog above also covers a lone length with a missed GDO IRQ. */
        if (count < 2) return;
        if (!r->port.read_burst(r->port.ctx,REG_FIFO,&r->rx_length,1)) { fault(r); return; }
        count--;
        if (r->rx_length < 15 || r->rx_length > DC_CC1101_PKTLEN) {
            r->stats.rx_invalid++; (void)recover(r,now); return;
        }
    }
    if (!r->rx_length) {
        if (marc == MARC_IDLE && !rx_sync_high(r)) (void)receive_mode(r,now,false);
        else if (!rx_sync_high(r)) r->rx_sync_pending=false;
        return;
    }
    if (count < r->rx_length+2u) {
        if (expired(now,r->partial_deadline)) {
            r->stats.rx_partial_timeout++; (void)recover(r,now);
        }
        return;
    }
    if (!r->port.read_burst(r->port.ctx,REG_FIFO,packet,r->rx_length+2u)) { fault(r); return; }
    if (!(packet[r->rx_length+1u] & 0x80)) {
        r->stats.rx_hw_crc++; (void)recover(r,now); return;
    }
    if (packet[0] != r->local_address) {
        r->stats.rx_invalid++; (void)recover(r,now); return;
    }
    result=dc_decode_raw(packet+1,r->rx_length-1u,&frame);
    if (result != DC_OK) {
        if (result == DC_BAD_CRC) r->stats.rx_app_crc++;
        else r->stats.rx_invalid++;
        (void)recover(r,now); return;
    }
    r->stats.rx_valid++;
    r->last_rx_ms=now;
    r->last_rssi_x2_dbm=(int16_t)((int16_t)(int8_t)packet[r->rx_length]-148);
    r->last_lqi=(uint8_t)(packet[r->rx_length+1u]&0x7fu);
    r->rssi_valid=true;
    /* RXOFF_MODE=IDLE prevents another packet while FIFO is drained. */
    if (!receive_mode(r,now,false)) return;
    /* A delayed response can still satisfy the same transaction in backoff. */
    if (r->transaction != DC_RF_TXN_IDLE && matches(r,&frame))
        complete(r,DC_RF_COMPLETE,&frame);
    if (r->on_frame) r->on_frame(r->user,&frame);
}

void dc_radio_tick(dc_radio_t *r, uint32_t now) {
    uint8_t marc,txbytes;
    bool tracked;
    status_result_t status;
    if (!r || r->state == DC_RADIO_OFF || r->state == DC_RADIO_FAULT) return;
    if (r->transaction != DC_RF_TXN_IDLE && expired(now,r->transaction_deadline)) {
        tracked=r->state == DC_RADIO_TX;
        if (tracked && expired(now,r->state_deadline)) r->stats.tx_timeout++;
        complete(r,DC_RF_TIMEOUT,NULL);
        if (tracked && !recover(r,now)) return;
    }
    /* Reply expiry is independent of SPI status stability and RX progress.
     * A late valid reply may still complete the same transaction in backoff. */
    if (r->transaction == DC_RF_TXN_WAIT_REPLY && expired(now,r->reply_deadline))
        retry_or_finish(r,now);
    /* Timers are checked before status samples: changing/garbled status must
     * never postpone a watchdog. At the exact deadline, timeout takes priority. */
    if (r->state == DC_RADIO_TX && expired(now,r->state_deadline)) {
        r->stats.tx_timeout++; retry_or_finish(r,now); (void)recover(r,now); return;
    }
    if (r->state == DC_RADIO_CALIBRATING && expired(now,r->state_deadline)) {
        r->stats.calibration_timeout++; timed_fault(r); return;
    }
    if (r->state == DC_RADIO_RX_START && expired(now,r->state_deadline)) {
        r->stats.rx_start_timeout++; timed_fault(r); return;
    }
    if (r->state == DC_RADIO_RX && rx_sync_high(r) && !r->rx_sync_pending) {
        r->rx_sync_pending=true; r->partial_deadline=now+DC_RF_TX_TIMEOUT_MS;
    }
    if (r->state == DC_RADIO_RX && r->rx_sync_pending && expired(now,r->partial_deadline)) {
        if (r->rx_length || r->rx_fifo_seen) r->stats.rx_partial_timeout++;
        else r->stats.rx_sync_timeout++;
        (void)recover(r,now); return;
    }
    if (r->state == DC_RADIO_RX && r->status_pending && expired(now,r->status_deadline)) {
        r->stats.status_timeout++;
        /* With no trustworthy state, RX flush alone could unknowingly abort
         * TX. Use the full reset path so modulator residue cannot survive. */
        if (configure_hardware(r,now)) r->stats.recoveries++;
        return;
    }
    status=read_status(r,REG_MARCSTATE,&marc,now);
    if (status == STATUS_ERROR) { fault(r); return; }
    if (status != STATUS_OK) return;
    marc &= 0x1f;
    if (r->state == DC_RADIO_CALIBRATING) {
        if (marc == MARC_IDLE) (void)receive_mode(r,now,true);
        return;
    }
    if (r->state == DC_RADIO_RX_START) {
        uint8_t rxbytes=0;
        /* IDLE may mean a packet arrived between polls, but must not mask an
         * RX strobe that never settled. That failure still has a deadline. */
        if (marc == MARC_IDLE) {
            status=read_status(r,REG_RXBYTES,&rxbytes,now);
            if (status == STATUS_ERROR) { fault(r); return; }
            if (status != STATUS_OK) return;
        }
        if (marc == MARC_RX || (marc == MARC_IDLE && (rxbytes & 0x7f))) r->state=DC_RADIO_RX;
        else if (marc == MARC_RX_OVERFLOW) {
            r->stats.rx_overflow++; (void)recover(r,now); return;
        }
        else return;
    }
    if (r->state == DC_RADIO_TX) {
        status=read_status(r,REG_TXBYTES,&txbytes,now);
        if (status == STATUS_ERROR) { fault(r); return; }
        if (status != STATUS_OK) return;
        if ((txbytes & 0x80) || marc == MARC_TX_UNDERFLOW) {
            r->stats.tx_underflow++; retry_or_finish(r,now);
            (void)recover(r,now); return;
        }
        if ((r->gdo_falling_events != r->tx_event_start || marc == MARC_IDLE || marc == MARC_RX) &&
            (txbytes & 0x7f) == 0 && (marc == MARC_IDLE || marc == MARC_RX)) {
            tracked=r->tracked_tx;
            r->stats.tx_ok++;
            if (!receive_mode(r,now,false)) return;
            r->tracked_tx=false;
            if (tracked && r->transaction != DC_RF_TXN_IDLE) {
                r->transaction=DC_RF_TXN_WAIT_REPLY;
                r->reply_deadline=now+DC_RF_REPLY_TIMEOUT_MS;
            }
        }
        return;
    }
    if (marc != MARC_RX && marc != MARC_IDLE && marc != MARC_RX_OVERFLOW) {
        watch_status(r,now);
        return;
    }
    receive_packet(r,now,marc);
    if (r->state == DC_RADIO_FAULT) return;
    if (r->transaction == DC_RF_TXN_BACKOFF && expired(now,r->retry_at) && dc_radio_ready(r)) {
        if (start_tx(r,&r->request,r->peer_address,true,now)) {
            r->attempts++; r->stats.retries++; r->transaction=DC_RF_TXN_TX;
        }
    }
}
