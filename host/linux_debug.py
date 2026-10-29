"""Binary Linux debug protocol; transport supplied by the Fenestra PTP client.

Commands execute with the agent's actual UID/GID. This is privileged debugging,
not a Flash-write prevention boundary. Importing this module opens no device.
"""
import errno
import struct

MAX_DATA = 992
# Four bytes are consumed by the outer bridge operation inside FXP2.
MAX_REQUEST = 1020


def _uint(value, bits, minimum=0):
    if not isinstance(value, int) or not minimum <= value < 1 << bits:
        raise ValueError(f'integer in {minimum}..{(1 << bits)-1} required')
    return value


def _range(offset, count):
    _uint(offset, 63)
    if not 1 <= count <= MAX_DATA or offset + count >= 1 << 63:
        raise ValueError('nonempty range, at most 992 bytes, within signed 64-bit offsets required')


def _path(path):
    if not isinstance(path, str) or not path.startswith('/') or '\0' in path:
        raise ValueError('absolute NUL-free Linux path required')
    return path.encode('utf-8')


def _timeout(value):
    if not isinstance(value, int) or not 100 <= value <= 300000:
        raise ValueError('timeout must be 100..300000 ms')
    return value


class LinuxError(OSError):
    def __init__(self, code, operation, raw):
        super().__init__(code, errno.errorcode.get(code, 'Linux backend error'))
        self.operation = operation
        self.raw = raw


class LinuxDebug:
    def __init__(self, exchange):
        self.exchange = exchange

    def request(self, op, data=b''):
        packet = struct.pack('<I', op) + data
        if len(packet) > MAX_REQUEST:
            raise ValueError('Linux request exceeds 1020-byte bridge payload')
        raw = self.exchange(packet)
        if not 8 <= len(raw) <= 1024:
            raise ValueError('Linux response length')
        error, echoed = struct.unpack_from('<II', raw)
        if echoed != op:
            raise ValueError('Linux response operation differs')
        if error:
            raise LinuxError(error, op, raw)
        return raw[8:]

    def info(self):
        raw = self.request(1)
        if len(raw) < 41 or raw[:4] != b'FXL1' or struct.unpack_from('<I', raw, 4)[0] != 1 or raw[-1]:
            raise ValueError('Linux info schema')
        result = dict(zip(('pid', 'uid', 'gid', 'jobs', 'files', 'ring_bytes', 'operations',
                           'flash_write_prevention'), struct.unpack_from('<8I', raw, 8)))
        result['cwd'] = raw[40:-1].decode('utf-8', 'surrogateescape')
        return result

    def execute(self, argv, timeout_ms=30000):
        if not 1 <= len(argv) <= 32 or not argv[0].startswith('/') or any('\0' in v for v in argv):
            raise ValueError('absolute executable and 1..32 NUL-free arguments required')
        raw = self.request(2, struct.pack('<II', _timeout(timeout_ms), len(argv)) +
                           b''.join(v.encode('utf-8') + b'\0' for v in argv))
        return dict(zip(('id', 'pid', 'pty'), struct.unpack('<III', raw)))

    def shell(self, timeout_ms=300000):
        return dict(zip(('id', 'pid', 'pty'), struct.unpack('<III', self.request(3, struct.pack('<I', _timeout(timeout_ms))))))

    def status(self, job):
        raw = self.request(4, struct.pack('<I', job))
        return dict(zip(('id', 'done', 'pid', 'exit_code', 'signal', 'timed_out', 'stdout_end', 'stderr_end'),
                        struct.unpack('<6I2Q', raw)))

    def cancel(self, job):
        self.request(5, struct.pack('<I', job))

    def close_job(self, job):
        self.request(6, struct.pack('<I', job))

    def read(self, job, stream=0, offset=0, count=992):
        _uint(job, 32, 1)
        _uint(offset, 64)
        if stream not in (0, 1) or not 1 <= count <= MAX_DATA:
            raise ValueError('stream 0/1 and count 1..992 required')
        raw = self.request(7, struct.pack('<IIQI', job, stream, offset, count))
        if len(raw) < 24:
            raise ValueError('short stream response')
        base, end, done, size = struct.unpack_from('<QQII', raw)
        if len(raw) != 24 + size or size > count or not base <= offset <= offset + size <= end:
            raise ValueError('stream response bounds')
        return dict(base=base, end=end, done=done, data=raw[24:])

    def write(self, job, data):
        data = bytes(data)
        if not 1 <= len(data) <= MAX_DATA:
            raise ValueError('stdin chunk must contain 1..992 bytes')
        return self._written(self.request(8, struct.pack('<I', job) + data), len(data))

    def close_stdin(self, job):
        """Pipe EOF for EXEC_ARGV; PTYs explicitly return ENOTSUP."""
        self.request(18, struct.pack('<I', _uint(job, 32, 1)))

    def resize(self, job, rows, columns):
        if not 1 <= rows <= 4096 or not 1 <= columns <= 4096:
            raise ValueError('terminal size must be 1..4096')
        self.request(9, struct.pack('<III', job, rows, columns))

    def file_open(self, path, mode=0):
        if mode not in (0, 1, 2):
            raise ValueError('file mode 0=read, 1=existing R/W, 2=exclusive create')
        return struct.unpack('<IQ', self.request(10, struct.pack('<I', mode) + _path(path)))

    def file_read(self, handle, offset, count):
        _range(offset, count)
        raw = self.request(11, struct.pack('<IQI', handle, offset, count))
        return self._data(raw, count)

    def file_write(self, handle, offset, data):
        data = bytes(data)
        _range(offset, len(data))
        return self._written(self.request(12, struct.pack('<IQI', handle, offset, len(data)) + data), len(data))

    def file_close(self, handle):
        self.request(13, struct.pack('<I', handle))

    def file_sync(self, handle):
        """Reports fdatasync result; this is not a hardware power-loss test."""
        self.request(19, struct.pack('<I', _uint(handle, 32, 1)))

    def stat(self, path):
        raw = self.request(14, _path(path))
        mode, uid, gid, reserved, size = struct.unpack('<4IQ', raw)
        if reserved:
            raise ValueError('stat reserved field')
        return dict(mode=mode, uid=uid, gid=gid, size=size)

    @staticmethod
    def _data(raw, count):
        if len(raw) < 4 or struct.unpack_from('<I', raw)[0] != len(raw)-4 or len(raw)-4 > count:
            raise ValueError('Linux read result length')
        return raw[4:]

    def memory_read(self, pid, address, count):
        _uint(pid, 31, 1)
        _range(address, count)
        return self._data(self.request(15, struct.pack('<IQI', pid, address, count)), count)

    def memory_write(self, pid, address, data):
        _uint(pid, 31, 1)
        data = bytes(data)
        _range(address, len(data))
        return self._written(self.request(16, struct.pack('<IQI', pid, address, len(data)) + data), len(data))

    def maps(self, pid, offset=0, count=992):
        _uint(pid, 31, 1)
        _range(offset, count)
        return self._data(self.request(17, struct.pack('<IQI', pid, offset, count)), count)

    @staticmethod
    def _written(raw, requested):
        written = struct.unpack('<I', raw)[0]
        if written > requested:
            raise ValueError('Linux write count exceeds request')
        return written
