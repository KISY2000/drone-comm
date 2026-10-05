#include "dc_node.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct bus bus_t;
typedef struct {bus_t *bus;uint8_t owner;} endpoint_t;
struct bus {
 dc_node_t n[DC_NODE_COUNT];endpoint_t e[DC_NODE_COUNT];dc_queue_t q[DC_NODE_COUNT];
 dc_parser_t parser[DC_NODE_COUNT];uint32_t now,uart_frames,rf_frames;bool video_connected,radio_connected;
 dc_frame_t last_air,last_request,last_result;
};
static bool send_frame(void *ctx,const dc_frame_t *f) {
 endpoint_t *e=(endpoint_t*)ctx;bus_t *b=e->bus;uint8_t dest;bool wireless=false;
 if(e->owner==DC_HUB)dest=f->dst==DC_AIR?DC_RADIO_GROUND:f->dst;
 else if(e->owner==DC_RADIO_GROUND) {dest=f->dst==DC_AIR?DC_AIR:DC_HUB;wireless=dest==DC_AIR;}
 else if(e->owner==DC_AIR) {dest=DC_RADIO_GROUND;wireless=true;}
 else dest=DC_HUB;
 if((dest==DC_ZYNQ||e->owner==DC_ZYNQ)&&!b->video_connected)return true;
 if(wireless&&!b->radio_connected)return true;
 if(wireless) {uint8_t raw[DC_RAW_MAX];dc_frame_t decoded;size_t size=dc_encode_raw(f,raw,sizeof(raw));assert(size&&dc_decode_raw(raw,size,&decoded)==DC_OK);++b->rf_frames;}
 else {uint8_t bytes[DC_UART_MAX];size_t i,size=dc_encode_uart(f,bytes,sizeof(bytes));dc_frame_t decoded;unsigned count=0;assert(size);
  for(i=0;i<size;i++)if(dc_parser_feed(&b->parser[dest],bytes[i],&decoded))++count;assert(count==1);++b->uart_frames;}
 if(e->owner==DC_AIR&&f->type==DC_TELEMETRY)b->last_air=*f;
 if(e->owner==DC_HUB&&(f->type==DC_TEST_REQUEST||f->type==DC_TELEMETRY_QUERY))b->last_request=*f;
 if(e->owner==DC_AIR&&f->type==DC_TEST_RESULT)b->last_result=*f;
 return dc_queue_push(&b->q[dest],f);
}
static void drain(bus_t *b) {
 uint32_t rounds=0;bool did;uint8_t i;dc_frame_t f;
 do {did=false;for(i=1;i<DC_NODE_COUNT;i++)if(dc_queue_pop(&b->q[i],&f)) {did=true;dc_node_receive(&b->n[i],&f,b->now);}assert(++rounds<1000);}while(did);
}
static void run(bus_t *b,uint32_t duration) {
 uint32_t end=b->now+duration;uint8_t i;
 while(b->now<end) {for(i=1;i<DC_NODE_COUNT;i++) {dc_node_tick(&b->n[i],b->now);drain(b);}b->now+=10;}
}
static void init(bus_t *b) {
 uint8_t i;memset(b,0,sizeof(*b));b->video_connected=true;b->radio_connected=true;
 for(i=1;i<DC_NODE_COUNT;i++) {b->e[i].bus=b;b->e[i].owner=i;dc_queue_init(&b->q[i]);dc_parser_init(&b->parser[i]);dc_node_init(&b->n[i],i,0x100u+i,i==DC_HUB?0x12abcdefu:0,send_frame,&b->e[i]);}
}
int main(void) {
 bus_t b;uint32_t telem,before;dc_frame_t old,cmd,query;uint8_t test[30];unsigned i;
 init(&b);run(&b,1000);
 for(i=1;i<DC_NODE_COUNT;i++)assert(b.n[i].ready);
 assert(b.n[DC_HUB].telemetry_rx>=8&&b.n[DC_HUB].video_rx>=1);
 assert(b.n[DC_RADIO_GROUND].forwarded>10);
 old=b.last_air;
 b.n[DC_HUB].poll_enabled=false;drain(&b);for(i=0;i<30;i++)test[i]=(uint8_t)(i%3?i:0);
 assert(dc_node_request(&b.n[DC_HUB],DC_TEST_REQUEST,test,30,b.now));query=b.last_request;drain(&b);
 assert(b.n[DC_HUB].test_rx==1&&b.n[DC_AIR].test_rx==1);
 assert(b.last_result.len==32&&memcmp(b.last_result.payload+2,test,30)==0);
 dc_node_receive(&b.n[DC_AIR],&query,b.now);drain(&b);assert(b.n[DC_AIR].test_rx==1&&b.n[DC_HUB].test_rx==1);
 cmd=query;cmd.type=DC_CONTROL_CMD;cmd.seq=b.n[DC_HUB].tx_seq[DC_AIR]++;cmd.len=0;
 dc_node_receive(&b.n[DC_AIR],&cmd,b.now);drain(&b);assert(b.n[DC_AIR].unsupported==1);
 b.n[DC_HUB].poll_enabled=true;telem=b.n[DC_HUB].telemetry_rx;b.video_connected=false;run(&b,2200);
 assert(!dc_node_peer_healthy(&b.n[DC_HUB],DC_ZYNQ,b.now)&&!b.n[DC_HUB].video_valid);
 assert(b.n[DC_HUB].telemetry_rx>telem+15);
 b.video_connected=true;run(&b,1200);assert(b.n[DC_ZYNQ].ready&&b.n[DC_HUB].video_valid);
 before=b.n[DC_HUB].rejected;dc_node_init(&b.n[DC_AIR],DC_AIR,0x777,0,send_frame,&b.e[DC_AIR]);run(&b,1000);
 dc_node_receive(&b.n[DC_HUB],&old,b.now);assert(b.n[DC_HUB].rejected==before+1);
 assert(b.n[DC_AIR].ready&&dc_node_peer_healthy(&b.n[DC_HUB],DC_AIR,b.now));
 dc_node_init(&b.n[DC_HUB],DC_HUB,0x888,0xdead4321,send_frame,&b.e[DC_HUB]);run(&b,1500);
 assert(b.n[DC_AIR].ready&&b.n[DC_ZYNQ].ready&&b.n[DC_HUB].telemetry_rx>5);
 telem=b.n[DC_HUB].telemetry_rx;b.radio_connected=false;run(&b,2200);
 assert(!b.n[DC_HUB].telemetry_valid&&!dc_node_peer_healthy(&b.n[DC_HUB],DC_AIR,b.now));
 assert(b.n[DC_HUB].telemetry_rx==telem&&b.n[DC_HUB].video_valid);
 b.radio_connected=true;run(&b,1500);assert(b.n[DC_HUB].telemetry_valid);
 printf("{\"four_nodes\":\"PASS\",\"uart_frames\":%lu,\"rf_frames\":%lu,\"telemetry_rx\":%lu,\"video_rx\":%lu,\"motor_output\":false}\n",(unsigned long)b.uart_frames,(unsigned long)b.rf_frames,(unsigned long)b.n[DC_HUB].telemetry_rx,(unsigned long)b.n[DC_HUB].video_rx);
 return 0;
}
