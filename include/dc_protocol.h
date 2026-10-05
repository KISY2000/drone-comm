#ifndef DC_PROTOCOL_H
#define DC_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define DC_VERSION 1u
#define DC_PAYLOAD_MAX 32u
#define DC_RAW_MAX 46u
#define DC_UART_MAX 48u
#define DC_QUEUE_CAPACITY 8u
enum { DC_HUB=1, DC_RADIO_GROUND=2, DC_ZYNQ=3, DC_AIR=4, DC_NODE_COUNT=5 };
enum { DC_HELLO=0x01, DC_HELLO_ACK=0x02, DC_HEARTBEAT=0x03,
 DC_RADIO_STATE=0x04, DC_TELEMETRY_QUERY=0x10, DC_TELEMETRY=0x11,
 DC_TEST_REQUEST=0x12, DC_TEST_RESULT=0x13, DC_VIDEO_STATUS=0x20,
 DC_FPGA_STATUS=0x21, DC_VIDEO_CONFIG=0x22, DC_SYSTEM_STATUS=0x23,
 DC_CONTROL_CMD=0x30, DC_OPERATOR_CMD_REQUEST=0x31, DC_ERROR=0x7f };
enum { DC_ERR_UNSUPPORTED=1, DC_ERR_VIDEO_NOT_READY=2, DC_ERR_BUSY=3 };
typedef enum { DC_OK=0, DC_BAD_LENGTH, DC_BAD_VERSION, DC_BAD_CRC,
 DC_BAD_COBS, DC_BAD_ADDRESS, DC_NO_SPACE } dc_result_t;
typedef struct { uint8_t type,src,dst; uint32_t session; uint16_t seq,len;
 uint8_t payload[DC_PAYLOAD_MAX]; } dc_frame_t;
typedef struct { uint8_t data[DC_UART_MAX]; size_t used; bool dropping;
 uint32_t accepted,invalid,crc_errors,oversize; } dc_parser_t;
typedef struct { bool initialized; uint16_t last; uint32_t gaps,duplicates,old; } dc_seq_t;
typedef struct { dc_frame_t items[DC_QUEUE_CAPACITY]; uint8_t head,count; uint32_t overflow; } dc_queue_t;
uint16_t dc_crc16(const uint8_t *data,size_t len);
uint16_t dc_get_u16(const uint8_t *p);
uint32_t dc_get_u32(const uint8_t *p);
void dc_put_u16(uint8_t *p,uint16_t v);
void dc_put_u32(uint8_t *p,uint32_t v);
size_t dc_encode_raw(const dc_frame_t *f,uint8_t *out,size_t capacity);
dc_result_t dc_decode_raw(const uint8_t *data,size_t len,dc_frame_t *out);
size_t dc_encode_uart(const dc_frame_t *f,uint8_t *out,size_t capacity);
void dc_parser_init(dc_parser_t *p);
/* Returns true only when this byte completes one valid frame. */
bool dc_parser_feed(dc_parser_t *p,uint8_t byte,dc_frame_t *out);
void dc_seq_reset(dc_seq_t *s);
/* Forward gaps accepted; duplicates and backward/ambiguous half-range rejected. */
bool dc_seq_accept(dc_seq_t *s,uint16_t seq);
void dc_queue_init(dc_queue_t *q);
bool dc_queue_push(dc_queue_t *q,const dc_frame_t *f);
bool dc_queue_pop(dc_queue_t *q,dc_frame_t *out);
#endif
