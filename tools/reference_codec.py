"""Independent PC codec. CRC uses Python's C-implemented binascii.hqx.

Wire format is explicitly packed; no shared C serializer or generated codec.
All frames use version 1 and a maximum 32-byte payload.
"""
import binascii
import struct

HEADER = struct.Struct('<BBBBIHH')

def raw_frame(msg_type, src, dst, session, seq, payload=b''):
    if not (1 <= src <= 4 and 1 <= dst <= 4 and len(payload) <= 32):
        raise ValueError('address or payload')
    body = HEADER.pack(1, msg_type, src, dst, session, seq, len(payload)) + payload
    return body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))

def decode_raw(data):
    if not 14 <= len(data) <= 46:
        raise ValueError('length')
    version, kind, src, dst, session, seq, size = HEADER.unpack(data[:12])
    if version != 1 or not (1 <= src <= 4 and 1 <= dst <= 4) or size > 32 or len(data) != size + 14:
        raise ValueError('header')
    if binascii.crc_hqx(data[:-2], 0xffff) != int.from_bytes(data[-2:], 'little'):
        raise ValueError('crc')
    return dict(type=kind, src=src, dst=dst, session=session, seq=seq, payload=data[12:-2])

def uart_frame(raw):
    decode_raw(raw)
    # Each raw frame is below 254 bytes, so no extended COBS block is needed.
    blocks = raw.split(b'\0')
    return b''.join(bytes([len(block) + 1]) + block for block in blocks) + b'\0'

def decode_uart(data):
    if not data or data[-1] != 0 or len(data) > 48:
        raise ValueError('delimiter/length')
    encoded = data[:-1]
    if not encoded or 0 in encoded:
        raise ValueError('embedded delimiter')
    blocks, index = [], 0
    while index < len(encoded):
        count = encoded[index]
        if index + count > len(encoded):
            raise ValueError('COBS block')
        blocks.append(encoded[index + 1:index + count])
        index += count
    raw = b'\0'.join(blocks)
    decode_raw(raw)
    return raw
