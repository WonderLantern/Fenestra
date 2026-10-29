"""FXT1 framed PTP transactions over a binary child-process pipe, not USB/IP."""
import struct
from ptp_contract import validate_transfer

HEADER = struct.Struct('<4sHHII')
TRANSFER = struct.Struct('<HHI')
MAX_FRAME = 4096
OPEN, READ, WRITE, CLOSE = 1, 2, 3, 4
REPLY, PTP_ERROR, FATAL = 0x8000, 0xfffe, 0xffff


def read_exact(stream, size, eof=False):
    parts = bytearray()
    while len(parts) < size:
        part = stream.read(size - len(parts))
        if not part:
            if eof and not parts:
                return None
            raise EOFError('PTP pipe ended inside a frame; transaction may have executed')
        parts.extend(part)
    return bytes(parts)


def read_frame(stream, eof=False):
    raw = read_exact(stream, HEADER.size, eof)
    if raw is None:
        return None
    magic, version, kind, seq, size = HEADER.unpack(raw)
    if magic != b'FXT1' or version != 1 or not seq or size > MAX_FRAME:
        raise ValueError('PTP pipe frame identity/length')
    return kind, seq, read_exact(stream, size)


def write_frame(stream, kind, seq, data=b''):
    if not 0 < seq < 1 << 32 or len(data) > MAX_FRAME:
        raise ValueError('PTP pipe frame bounds')
    raw = HEADER.pack(b'FXT1', 1, kind, seq, len(data)) + data
    offset = 0
    while offset < len(raw):
        count = stream.write(raw[offset:])
        if count is None or count <= 0 or count > len(raw) - offset:
            raise OSError('PTP pipe short write; no replay')
        offset += count
    stream.flush()


def pack_transfer(opcode, params, payload, maximum):
    params = validate_transfer(opcode, params, payload, maximum)
    return (TRANSFER.pack(opcode, len(params), maximum) +
            struct.pack('<' + 'I' * len(params), *params) +
            (bytes(payload) if payload is not None else b''))


def unpack_transfer(kind, data):
    if len(data) < TRANSFER.size or kind not in (READ, WRITE):
        raise ValueError('PTP pipe transfer header')
    opcode, count, maximum = TRANSFER.unpack_from(data)
    end = TRANSFER.size + count * 4
    if count > 5 or end > len(data) or (kind == READ and end != len(data)):
        raise ValueError('PTP pipe transfer parameters')
    params = struct.unpack_from('<' + 'I' * count, data, TRANSFER.size)
    payload = data[end:] if kind == WRITE else None
    validate_transfer(opcode, params, payload, maximum)
    return opcode, params, payload, maximum
