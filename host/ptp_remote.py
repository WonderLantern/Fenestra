"""Camera-side FXT1 stdio endpoint. Launch explicitly through authenticated SSH.

Imports are inert; --stdio opens one native WPD/libusb session on OPEN. stdout
is exclusively binary protocol. No socket listener, SSH setup or device reset.
"""
import argparse
import json
import struct
import sys
from ptp_client import PtpError
from ptp_pipe_protocol import (OPEN, READ, WRITE, CLOSE, REPLY, PTP_ERROR, FATAL,
                               read_frame, write_frame, unpack_transfer)


def serve(incoming, outgoing, factory):
    transport = None
    seq = 0
    try:
        while True:
            frame = read_frame(incoming, eof=True)
            if frame is None:
                return 0
            kind, number, data = frame
            if number != seq + 1:
                raise ValueError('PTP pipe sequence')
            seq = number
            try:
                if kind == OPEN:
                    if transport is not None or data:
                        raise ValueError('PTP pipe open state')
                    candidate = factory()
                    transport = candidate.__enter__()
                    # Only public model/version; never export serial or WPD path.
                    identity = dict(profile=transport.profile,
                                    model=transport.identity['model'],
                                    version=transport.identity['version'])
                    result = json.dumps(identity, separators=(',', ':')).encode('utf-8')
                elif kind in (READ, WRITE):
                    if transport is None:
                        raise ValueError('PTP pipe session not open')
                    opcode, params, payload, maximum = unpack_transfer(kind, data)
                    if kind == READ:
                        result = transport.read(opcode, params, maximum)
                        if not isinstance(result, bytes) or len(result) > maximum:
                            raise ValueError('native PTP read exceeds request')
                    else:
                        transport.write(opcode, params, payload)
                        result = b''
                elif kind == CLOSE:
                    if transport is None or data:
                        raise ValueError('PTP pipe close state')
                    closing, transport = transport, None
                    closing.close()
                    write_frame(outgoing, kind | REPLY, seq)
                    return 0
                else:
                    raise ValueError('PTP pipe operation')
                write_frame(outgoing, kind | REPLY, seq, result)
            except PtpError as exc:
                if transport is None:
                    raise
                write_frame(outgoing, PTP_ERROR, seq, struct.pack('<I', exc.code))
    except Exception as exc:
        # Do not echo command arguments, device paths, payloads or native errors.
        if seq:
            try:
                write_frame(outgoing, FATAL, seq, type(exc).__name__.encode('ascii'))
            except (OSError, ValueError):
                pass
        return 1
    finally:
        if transport is not None:
            try:
                transport.close()
            except Exception:
                pass


def main():
    from ptp_contract import PROFILES
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stdio', action='store_true', required=True)
    parser.add_argument('--profile', choices=('auto', *PROFILES), default='auto')
    args = parser.parse_args()
    if sys.platform == 'win32':
        import msvcrt
        import os
        for stream in (sys.stdin, sys.stdout):
            msvcrt.setmode(stream.fileno(), os.O_BINARY)
    from ptp_transport import open_native_transport
    return serve(sys.stdin.buffer, sys.stdout.buffer,
                 lambda: open_native_transport(args.profile))


if __name__ == '__main__':
    raise SystemExit(main())
