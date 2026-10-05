#include "dc_node.h"
#include <string.h>
static uint32_t age(uint32_t now,uint32_t then) { return now-then; }
static uint32_t token(dc_node_t *n) {
 uint32_t v;do { v=n->session^(++n->nonce_state*0x9e3779b9u); } while(!v);return v;
}
static dc_frame_t make(dc_node_t *n,uint8_t dst,uint8_t type) {
 dc_frame_t f;memset(&f,0,sizeof(f));f.src=n->id;f.dst=dst;f.type=type;f.session=n->session;f.seq=n->tx_seq[dst]++;return f;
}
static bool emit(dc_node_t *n,const dc_frame_t *f) {
 if(n->send&&n->send(n->user,f))return true;
 ++n->tx_dropped;return false;
}
static void probe(dc_node_t *n,uint8_t dst,uint32_t now) {
 dc_peer_t *p=&n->peers[dst];dc_frame_t f=make(n,dst,DC_HELLO);
 if(!p->pending) {p->challenge=++n->nonce_state;if(!p->challenge)p->challenge=++n->nonce_state;p->session=0;p->ready=false;p->pending=true;dc_seq_reset(&p->rx);}
 p->handshake_at=now;f.len=4;dc_put_u32(f.payload,p->challenge);emit(n,&f);
 if(dst==DC_AIR) {n->request_pending=false;n->telemetry_valid=false;}
 if(dst==DC_ZYNQ)n->video_valid=false;
}
void dc_node_init(dc_node_t *n,uint8_t id,uint32_t boot_nonce,uint32_t seed,dc_send_fn send,void *user) {
 memset(n,0,sizeof(*n));n->id=id;n->boot_nonce=boot_nonce;n->send=send;n->user=user;
 n->session=(id==DC_HUB)?seed:0;n->ready=(id==DC_HUB&&seed!=0);n->poll_enabled=true;
 n->last_hello=(uint32_t)-DC_HEARTBEAT_MS;n->last_heartbeat=(uint32_t)-DC_HEARTBEAT_MS;n->last_poll=(uint32_t)-DC_POLL_MS;
}
bool dc_node_peer_healthy(const dc_node_t *n,uint8_t peer,uint32_t now) {
 if(peer<1||peer>=DC_NODE_COUNT)return false;
 return n->peers[peer].ready&&age(now,n->peers[peer].last_seen)<DC_LINK_TIMEOUT_MS;
}
bool dc_node_request(dc_node_t *n,uint8_t type,const uint8_t *payload,uint16_t len,uint32_t now) {
 dc_frame_t f;
 if(n->id!=DC_HUB||!n->ready||n->request_pending||!dc_node_peer_healthy(n,DC_AIR,now))return false;
 if((type!=DC_TELEMETRY_QUERY&&type!=DC_TEST_REQUEST)||len>30u||(!payload&&len))return false;
 f=make(n,DC_AIR,type);f.len=len;if(len)memcpy(f.payload,payload,len);
 if(!emit(n,&f))return false;
 n->request_seq=f.seq;n->request_type=type;n->request_at=now;n->request_pending=true;return true;
}
static void error_reply(dc_node_t *n,const dc_frame_t *in,uint8_t err) {
 dc_frame_t out=make(n,in->src,DC_ERROR);out.len=4;dc_put_u16(out.payload,in->seq);out.payload[2]=in->type;out.payload[3]=err;emit(n,&out);
}
static void hello(dc_node_t *n,const dc_frame_t *f,uint32_t now) {
 dc_frame_t out;dc_peer_t *p;
 if(n->id==DC_HUB) {
  if(f->src==DC_HUB||f->session!=0)return;
  p=&n->peers[f->src];
  if(f->len==4) {
   /* A delayed boot-discovery frame cannot tear down a healthy binding.
    * If an F103 startup token repeats, its reboot is detected by link timeout. */
   if(p->ready&&p->boot_nonce==dc_get_u32(f->payload)&&age(now,p->last_seen)<DC_LINK_TIMEOUT_MS)return;
   probe(n,f->src,now);return;
  }
  if(f->len!=8||!p->challenge||dc_get_u32(f->payload+4)!=p->challenge||age(now,p->handshake_at)>1000u) {++n->rejected;return;}
  if(!p->pending&&(!p->ready||dc_get_u32(f->payload)!=p->boot_nonce)) {++n->rejected;return;}
  if(p->pending) {if(!p->session)p->session=token(n);p->boot_nonce=dc_get_u32(f->payload);dc_seq_reset(&p->rx);}
  out=make(n,f->src,DC_HELLO_ACK);out.len=12;
  dc_put_u32(out.payload,p->boot_nonce);dc_put_u32(out.payload+4,p->challenge);dc_put_u32(out.payload+8,p->session);
  if(emit(n,&out)&&p->pending) {
   p->ready=true;p->pending=false;p->last_seen=now;
   /* A restarted transparent bridge lost its learned AIR binding. Rebuild it
    * explicitly instead of waiting for telemetry timeout or trusting a sniffed
    * data frame. Repeated ACKs do not re-trigger this. */
   if(f->src==DC_RADIO_GROUND)probe(n,DC_AIR,now);
  }
 } else {
  uint32_t incoming;uint8_t i;
  if(f->src!=DC_HUB||!f->session||f->len!=4)return;
  incoming=dc_get_u32(f->payload);
  if(!incoming) {++n->rejected;return;}
  for(i=0;i<8;i++)if(n->retired_hubs[i]==f->session) {++n->rejected;return;}
  if(n->hub_session==f->session&&n->challenge&&incoming!=n->challenge&&(uint32_t)(incoming-n->challenge)>=0x80000000u) {++n->rejected;return;}
  if(n->hub_session==f->session&&n->challenge==incoming&&n->ready)return;
  if(n->hub_session==f->session&&n->challenge==incoming&&!n->binding_pending) {++n->rejected;return;}
  if(n->hub_session&&n->hub_session!=f->session) {n->retired_hubs[n->retired_next]=n->hub_session;n->retired_next=(uint8_t)((n->retired_next+1u)%8u);}
  if(n->challenge!=dc_get_u32(f->payload)||n->hub_session!=f->session) {
   n->ready=false;n->cached_valid=false;n->request_pending=false;n->session=0;
   n->binding_pending=true;n->binding_at=now;
   dc_seq_reset(&n->peers[DC_HUB].rx);memset(n->tx_seq,0,sizeof(n->tx_seq));
  }
  n->challenge=dc_get_u32(f->payload);n->hub_session=f->session;
  out=make(n,DC_HUB,DC_HELLO);out.session=0;out.len=8;
  dc_put_u32(out.payload,n->boot_nonce);dc_put_u32(out.payload+4,n->challenge);emit(n,&out);n->last_hello=now;
 }
}
void dc_node_receive(dc_node_t *n,const dc_frame_t *f,uint32_t now) {
 dc_peer_t *p;dc_frame_t out;bool accepted;
 if(!f||f->src<1||f->src>=DC_NODE_COUNT||f->dst<1||f->dst>=DC_NODE_COUNT||f->src==n->id||f->len>DC_PAYLOAD_MAX) {++n->rejected;return;}
 if(f->dst!=n->id) {
  /* Ground radio is an explicit transparent HUB<->AIR bridge. It must not
   * re-number or re-session an aircraft packet. All other routes are denied. */
  if(n->id==DC_RADIO_GROUND&&((f->src==DC_HUB&&f->dst==DC_AIR)||(f->src==DC_AIR&&f->dst==DC_HUB))) {
   if(emit(n,f))++n->forwarded;
  }else ++n->rejected;
  return;
 }
 if(f->type==DC_HELLO) {hello(n,f,now);return;}
 if(f->type==DC_HELLO_ACK&&n->id!=DC_HUB) {
  if(f->src!=DC_HUB||f->len!=12||!n->hub_session||!n->challenge||f->session!=n->hub_session||dc_get_u32(f->payload)!=n->boot_nonce||dc_get_u32(f->payload+4)!=n->challenge||!dc_get_u32(f->payload+8)) {++n->rejected;return;}
  if(n->ready) {if(n->session!=dc_get_u32(f->payload+8))++n->rejected;return;}
  if(!n->binding_pending||age(now,n->binding_at)>=DC_LINK_TIMEOUT_MS) {++n->rejected;return;}
  /* Repeated ACK for the same binding must not reset freshness / RX sequence. */
  if(!n->ready||n->session!=dc_get_u32(f->payload+8)) {
   n->session=dc_get_u32(f->payload+8);dc_seq_reset(&n->peers[DC_HUB].rx);
   (void)dc_seq_accept(&n->peers[DC_HUB].rx,f->seq);n->cached_valid=false;
  }
  n->ready=true;n->binding_pending=false;n->peers[DC_HUB].ready=true;n->peers[DC_HUB].session=f->session;n->peers[DC_HUB].last_seen=now;return;
 }
 p=&n->peers[f->src];
 if(n->id==DC_HUB) {if(!p->ready||p->session!=f->session) {++n->rejected;return;}}
 else if(!n->ready||f->src!=DC_HUB||f->session!=n->hub_session) {++n->rejected;return;}
 accepted=dc_seq_accept(&p->rx,f->seq);
 if(!accepted) {
  if(n->id==DC_AIR&&n->cached_valid&&f->seq==n->cached_request_seq&&f->type==n->request_type&&f->session==n->hub_session) {
   /* Reuse the result, not an obsolete transport sequence. A newer error or
    * status may have overtaken the first lost response at the HUB. */
   n->cached_reply.seq=n->tx_seq[DC_HUB]++;emit(n,&n->cached_reply);
  }
  return;
 }
 p->last_seen=now;
 if(f->type==DC_CONTROL_CMD||f->type==DC_OPERATOR_CMD_REQUEST) {++n->unsupported;error_reply(n,f,DC_ERR_UNSUPPORTED);return;}
 if(n->id==DC_AIR&&(f->type==DC_TELEMETRY_QUERY||f->type==DC_TEST_REQUEST)) {
  out=make(n,DC_HUB,f->type==DC_TELEMETRY_QUERY?DC_TELEMETRY:DC_TEST_RESULT);
  dc_put_u16(out.payload,f->seq);
  if(f->type==DC_TELEMETRY_QUERY) {
   out.len=16;out.payload[2]=1;/* SIMULATED, no real sensor data */dc_put_u32(out.payload+4,now);
   dc_put_u16(out.payload+8,12000);dc_put_u16(out.payload+14,2000);
  }else {if(f->len>30) {error_reply(n,f,DC_ERR_UNSUPPORTED);return;}out.len=(uint16_t)(f->len+2);memcpy(out.payload+2,f->payload,f->len);++n->test_rx;}
  n->cached_reply=out;n->cached_request_seq=f->seq;n->request_type=f->type;n->cached_valid=true;emit(n,&out);return;
 }
 if(n->id==DC_HUB&&(f->type==DC_TELEMETRY||f->type==DC_TEST_RESULT)) {
  uint8_t expect=n->request_type==DC_TELEMETRY_QUERY?DC_TELEMETRY:DC_TEST_RESULT;
  if(f->src!=DC_AIR||f->len<2||!n->request_pending||f->type!=expect||dc_get_u16(f->payload)!=n->request_seq||age(now,n->request_at)>=DC_TRANSACTION_MS||(f->type==DC_TELEMETRY&&f->len!=16)) {++n->rejected;return;}
  n->request_pending=false;
  if(f->type==DC_TELEMETRY) {++n->telemetry_rx;n->last_telemetry=now;n->telemetry_valid=true;n->latest_telemetry=*f;}else ++n->test_rx;
  return;
 }
 if(n->id==DC_HUB&&f->src==DC_ZYNQ&&f->type==DC_VIDEO_STATUS) {
  if(f->len!=12) {++n->rejected;return;}++n->video_rx;n->last_video=now;n->video_valid=true;n->latest_video_status=*f;return;
 }
 if(n->id==DC_HUB&&f->src==DC_RADIO_GROUND&&f->type==DC_RADIO_STATE) {n->latest_radio_state=*f;return;}
 if(n->id==DC_ZYNQ&&f->type==DC_SYSTEM_STATUS) {n->latest_system_status=*f;return;}
 if(n->id==DC_ZYNQ&&f->type==DC_VIDEO_CONFIG) {error_reply(n,f,DC_ERR_VIDEO_NOT_READY);return;}
 if(f->type==DC_HEARTBEAT||f->type==DC_RADIO_STATE||f->type==DC_FPGA_STATUS||f->type==DC_SYSTEM_STATUS||f->type==DC_ERROR)return;
 ++n->unsupported;error_reply(n,f,DC_ERR_UNSUPPORTED);
}
void dc_node_tick(dc_node_t *n,uint32_t now) {
 uint8_t i;dc_frame_t f;
 if(n->id<1||n->id>=DC_NODE_COUNT)return;
 if(n->id==DC_HUB) {
  if(!n->ready)return;/* zero seed fails closed */
  for(i=2;i<DC_NODE_COUNT;i++) {
   dc_peer_t *p=&n->peers[i];
   if(p->ready&&age(now,p->last_seen)>=DC_LINK_TIMEOUT_MS) {p->ready=false;p->pending=false;}
   if(!p->ready&&(!p->pending||age(now,p->handshake_at)>=DC_HEARTBEAT_MS))probe(n,i,now);
  }
  if(n->request_pending&&age(now,n->request_at)>=DC_TRANSACTION_MS) {n->request_pending=false;++n->request_timeouts;}
  if(n->telemetry_valid&&age(now,n->last_telemetry)>=DC_LINK_TIMEOUT_MS)n->telemetry_valid=false;
  if(n->video_valid&&age(now,n->last_video)>=DC_LINK_TIMEOUT_MS)n->video_valid=false;
  if(n->poll_enabled&&age(now,n->last_poll)>=DC_POLL_MS) {n->last_poll=now;if(!n->request_pending)dc_node_request(n,DC_TELEMETRY_QUERY,0,0,now);}
 }else {
  if(n->ready&&age(now,n->peers[DC_HUB].last_seen)>=DC_LINK_TIMEOUT_MS) {n->ready=false;n->session=0;n->cached_valid=false;n->binding_pending=false;n->peers[DC_HUB].ready=false;}
  if(n->binding_pending&&age(now,n->binding_at)>=DC_LINK_TIMEOUT_MS)n->binding_pending=false;
  if(!n->ready) {if(age(now,n->last_hello)>=DC_HEARTBEAT_MS) {f=make(n,DC_HUB,DC_HELLO);f.session=0;f.len=4;dc_put_u32(f.payload,n->boot_nonce);emit(n,&f);n->last_hello=now;}return;}
 }
 if(age(now,n->last_heartbeat)>=DC_HEARTBEAT_MS) {
  n->last_heartbeat=now;
  if(n->id==DC_HUB) {
   for(i=2;i<DC_NODE_COUNT;i++)if(n->peers[i].ready) {f=make(n,i,DC_HEARTBEAT);emit(n,&f);}
   if(n->peers[DC_ZYNQ].ready) {
    f=make(n,DC_ZYNQ,DC_SYSTEM_STATUS);f.len=16;
    f.payload[0]=(uint8_t)((dc_node_peer_healthy(n,DC_AIR,now)?1u:0u)|(n->telemetry_valid?2u:0u)|(dc_node_peer_healthy(n,DC_RADIO_GROUND,now)?4u:0u)|(n->video_valid?8u:0u));
    f.payload[1]=(uint8_t)(n->telemetry_valid?(n->latest_telemetry.payload[2]&1u):0u);
    dc_put_u32(f.payload+4,n->telemetry_valid?age(now,n->last_telemetry):0xffffffffu);
    dc_put_u32(f.payload+8,n->telemetry_rx);dc_put_u32(f.payload+12,n->video_rx);emit(n,&f);
   }
  }
  else if(n->id!=DC_AIR) {f=make(n,DC_HUB,DC_HEARTBEAT);emit(n,&f);}
  if(n->id==DC_ZYNQ) {f=make(n,DC_HUB,DC_VIDEO_STATUS);f.len=12;f.payload[0]=1;/* SIMULATED */f.payload[1]=0;/* actual video is NOT locked */dc_put_u32(f.payload+4,now/40u);emit(n,&f);}
  if(n->id==DC_RADIO_GROUND) {f=make(n,DC_HUB,DC_RADIO_STATE);f.len=4;f.payload[0]=1;/* communication-test mode */emit(n,&f);}
 }
}
