#include "dc_protocol.h"
#include <string.h>
uint16_t dc_get_u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
uint32_t dc_get_u32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
void dc_put_u16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
void dc_put_u32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
uint16_t dc_crc16(const uint8_t *data,size_t len) {
 uint16_t crc=0xffffu; size_t i; unsigned b;
 for(i=0;i<len;i++) { crc^=(uint16_t)((uint16_t)data[i]<<8); for(b=0;b<8;b++) crc=(uint16_t)((crc&0x8000u)?((uint32_t)crc<<1)^0x1021u:((uint32_t)crc<<1)); }
 return crc;
}
size_t dc_encode_raw(const dc_frame_t *f,uint8_t *out,size_t cap) {
 size_t n;
 if(!f||!out||f->len>DC_PAYLOAD_MAX||f->src<1||f->src>=DC_NODE_COUNT||f->dst<1||f->dst>=DC_NODE_COUNT) return 0;
 n=14u+f->len; if(cap<n) return 0;
 out[0]=DC_VERSION; out[1]=f->type; out[2]=f->src; out[3]=f->dst;
 dc_put_u32(out+4,f->session); dc_put_u16(out+8,f->seq); dc_put_u16(out+10,f->len);
 memcpy(out+12,f->payload,f->len); dc_put_u16(out+12+f->len,dc_crc16(out,12u+f->len)); return n;
}
dc_result_t dc_decode_raw(const uint8_t *data,size_t len,dc_frame_t *out) {
 uint16_t payload;
 if(!data||!out||len<14u||len>DC_RAW_MAX) return DC_BAD_LENGTH;
 if(data[0]!=DC_VERSION) return DC_BAD_VERSION;
 payload=dc_get_u16(data+10); if(payload>DC_PAYLOAD_MAX||len!=14u+payload) return DC_BAD_LENGTH;
 if(dc_crc16(data,len-2)!=dc_get_u16(data+len-2)) return DC_BAD_CRC;
 if(data[2]<1||data[2]>=DC_NODE_COUNT||data[3]<1||data[3]>=DC_NODE_COUNT) return DC_BAD_ADDRESS;
 memset(out,0,sizeof(*out)); out->type=data[1];out->src=data[2];out->dst=data[3];
 out->session=dc_get_u32(data+4);out->seq=dc_get_u16(data+8);out->len=payload;memcpy(out->payload,data+12,payload);return DC_OK;
}
size_t dc_encode_uart(const dc_frame_t *f,uint8_t *out,size_t cap) {
 uint8_t raw[DC_RAW_MAX]; size_t n,i,code_pos=0,pos=1;uint8_t code=1;
 n=dc_encode_raw(f,raw,sizeof(raw)); if(!n||!out||cap<n+2u) return 0;
 for(i=0;i<n;i++) { if(raw[i]==0) { out[code_pos]=code;code_pos=pos++;code=1; } else { out[pos++]=raw[i];++code; } }
 out[code_pos]=code;out[pos++]=0;return pos;
}
void dc_parser_init(dc_parser_t *p) { memset(p,0,sizeof(*p)); }
bool dc_parser_feed(dc_parser_t *p,uint8_t byte,dc_frame_t *out) {
 uint8_t raw[DC_RAW_MAX];size_t i=0,j=0,k;uint8_t code;dc_result_t result;
 if(byte) { if(p->dropping) return false;
  if(p->used>=DC_UART_MAX-1u) { p->dropping=true;p->used=0;++p->oversize;return false; }
  p->data[p->used++]=byte;return false;
 }
 if(p->dropping) {p->dropping=false;p->used=0;return false;}
 if(!p->used) return false;
 while(i<p->used) { code=p->data[i++];
  if(code==0||(size_t)(code-1)>p->used-i) goto malformed;
  for(k=1;k<code;k++) {if(j>=sizeof(raw)) goto malformed;raw[j++]=p->data[i++];}
  if(code!=255&&i<p->used) {if(j>=sizeof(raw)) goto malformed;raw[j++]=0;}
 }
 p->used=0;result=dc_decode_raw(raw,j,out);
 if(result!=DC_OK) {++p->invalid;if(result==DC_BAD_CRC)++p->crc_errors;return false;}
 ++p->accepted;return true;
 malformed: p->used=0;++p->invalid;return false;
}
void dc_seq_reset(dc_seq_t *s) { memset(s,0,sizeof(*s)); }
bool dc_seq_accept(dc_seq_t *s,uint16_t seq) {
 uint16_t diff;
 if(!s->initialized) {s->initialized=true;s->last=seq;return true;}
 diff=(uint16_t)(seq-s->last);
 if(!diff) {++s->duplicates;return false;}
 if(diff>=0x8000u) {++s->old;return false;}
 s->gaps+=(uint32_t)diff-1u;s->last=seq;return true;
}
void dc_queue_init(dc_queue_t *q) { memset(q,0,sizeof(*q)); }
bool dc_queue_push(dc_queue_t *q,const dc_frame_t *f) {
 if(q->count>=DC_QUEUE_CAPACITY) {++q->overflow;return false;}
 q->items[(q->head+q->count)%DC_QUEUE_CAPACITY]=*f;++q->count;return true;
}
bool dc_queue_pop(dc_queue_t *q,dc_frame_t *out) {
 if(!q->count)return false;
 *out=q->items[q->head];q->head=(uint8_t)((q->head+1u)%DC_QUEUE_CAPACITY);--q->count;return true;
}
