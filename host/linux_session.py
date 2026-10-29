"""Bounded Linux debug workflows. Every exchange stays on the caller's thread.

Partial acknowledged writes advance their offset; uncertain requests are never
replayed. Local output and remote uploads are created exclusively. These are
developer operations with the agent's UID, not a module storage sandbox.
"""
import errno
import hashlib
import os
from pathlib import Path
import time

from linux_debug import LinuxError, MAX_DATA

MAX_TRANSFER = 64 * 1024 * 1024


def transfer_limit(size, maximum=MAX_TRANSFER):
    if not isinstance(size, int) or not 0 <= size <= maximum <= MAX_TRANSFER:
        raise ValueError('transfer exceeds configured limit (maximum 64 MiB)')


def require_operations(info, *operations):
    missing = [op for op in operations if not info['operations'] & (1 << (op - 1))]
    if missing:
        raise RuntimeError(f'RAM agent lacks operations {missing}; deploy the matching agent')


def _close_preserving_error(close, error=None):
    try:
        close()
    except Exception as cleanup:
        if error is None:
            raise
        error.add_note(f'cleanup also failed: {cleanup}; use local USB Off/restart if transport is lost')


def _copy_read(read, output, size):
    digest = hashlib.sha256()
    offset = 0
    while offset < size:
        data = read(offset, min(MAX_DATA, size - offset))
        if not data:
            raise EOFError(f'early EOF after {offset}/{size} bytes; partial output retained')
        output.write(data)
        digest.update(data)
        offset += len(data)
    return digest.hexdigest()


def download(debug, remote, destination, maximum=MAX_TRANSFER):
    # Open locally first so a pre-existing output cannot cause remote activity.
    with Path(destination).open('xb') as output:
        handle, size = debug.file_open(remote)
        error = None
        try:
            transfer_limit(size, maximum)
            digest = _copy_read(lambda off, n: debug.file_read(handle, off, n), output, size)
            output.flush()
            os.fsync(output.fileno())
        except BaseException as exc:
            error = exc
            raise
        finally:
            _close_preserving_error(lambda: debug.file_close(handle), error)
    return dict(bytes=size, sha256=digest, output=str(Path(destination).resolve()),
                atomic_snapshot=False)


def upload(debug, source, remote, maximum=MAX_TRANSFER):
    with Path(source).open('rb') as stream:
        size = os.fstat(stream.fileno()).st_size
        transfer_limit(size, maximum)
        handle, initial = debug.file_open(remote, 2)
        error = None
        try:
            if initial:
                raise ValueError('exclusive new remote file is not empty')
            digest = hashlib.sha256()
            offset = 0
            while offset < size:
                data = stream.read(min(MAX_DATA, size - offset))
                if not data:
                    raise EOFError(f'source shortened; partial remote file retained at {offset} bytes')
                digest.update(data)
                accepted = 0
                while accepted < len(data):
                    n = debug.file_write(handle, offset + accepted, data[accepted:])
                    if not n:
                        raise IOError(f'zero-progress remote write at {offset + accepted}')
                    accepted += n
                offset += len(data)
            if stream.read(1):
                raise ValueError('source grew during upload; remote file retained')
            debug.file_sync(handle)
            actual = hashlib.sha256()
            offset = 0
            while offset < size:
                data = debug.file_read(handle, offset, min(MAX_DATA, size - offset))
                if not data:
                    raise EOFError('remote readback shortened; file retained')
                actual.update(data)
                offset += len(data)
            if actual.digest() != digest.digest():
                raise ValueError('remote file readback SHA-256 mismatch; file retained')
            if debug.file_read(handle, size, 1):
                raise ValueError('remote file grew during readback; file retained')
        except BaseException as exc:
            error = exc
            raise
        finally:
            _close_preserving_error(lambda: debug.file_close(handle), error)
    return dict(remote=remote, bytes=size, sha256=digest.hexdigest(),
                readback_verified=True, fdatasync_completed=True, power_loss_tested=False)


def memory_dump(debug, pid, address, length, destination):
    transfer_limit(length)
    if not length or not 0 <= address < (1 << 63) - length:
        raise ValueError('nonempty signed 64-bit address range required')
    with Path(destination).open('xb') as output:
        digest = _copy_read(lambda off, n: debug.memory_read(pid, address + off, n), output, length)
    return dict(pid=pid, address=hex(address), bytes=length, sha256=digest,
                output=str(Path(destination).resolve()), atomic_snapshot=False)


def memory_write(debug, pid, address, data):
    # Explicit, bounded privileged write. No implicit retry, readback or rollback
    # of a live process, and no claim that a partial write is atomic.
    transfer_limit(len(data))
    if not data or not 0 <= address < (1 << 63) - len(data):
        raise ValueError('nonempty signed 64-bit address range required')
    offset = 0
    while offset < len(data):
        n = debug.memory_write(pid, address + offset, data[offset:offset + MAX_DATA])
        if not n:
            raise IOError(f'zero-progress process write; {offset} bytes already written')
        offset += n
    return dict(pid=pid, address=hex(address), bytes=offset, atomic=False)


def read_maps(debug, pid, maximum=1024 * 1024):
    transfer_limit(maximum)
    output = bytearray()
    while len(output) < maximum:
        data = debug.maps(pid, len(output), min(MAX_DATA, maximum - len(output)))
        if not data:
            return bytes(output)
        output.extend(data)
    if debug.maps(pid, len(output), 1):
        raise ValueError('process maps exceed configured limit')
    return bytes(output)


def finish_job(debug, job, wait_seconds=2):
    """Cancel and await the owned job before releasing it; never cancel others."""
    status = debug.status(job)
    if not status['done']:
        debug.cancel(job)
        end = time.monotonic() + wait_seconds
        while not status['done']:
            if time.monotonic() >= end:
                raise TimeoutError('owned job stop unconfirmed; job retained')
            time.sleep(.025)
            status = debug.status(job)
    debug.close_job(job)


class JobPump:
    def __init__(self, debug, job, emit):
        self.debug, self.job, self.emit = debug, job, emit
        self.offsets = [0, 0]
        self.drained = [False, False]

    def output(self):
        # Alternate stdout/stderr, bounded work per turn to service input too.
        for _ in range(4):
            progress = False
            for stream in (0, 1):
                chunk = self.debug.read(self.job, stream, self.offsets[stream])
                data = chunk['data']
                if data:
                    self.emit(stream, data)
                    self.offsets[stream] += len(data)
                    progress = True
                self.drained[stream] = bool(chunk['done'] and self.offsets[stream] == chunk['end'])
            if not progress:
                break
        status = self.debug.status(self.job)
        return status if status['done'] and all(self.drained) else None

    def input(self, pending):
        if not pending:
            return pending
        try:
            n = self.debug.write(self.job, pending[:MAX_DATA])
        except LinuxError as exc:
            if exc.errno not in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EINTR):
                raise
            return pending  # Terminal errno confirms zero bytes written.
        return pending[n:]


def run_exec(debug, argv, emit, source=None, timeout_ms=30000):
    job = debug.execute(argv, timeout_ms)['id']
    error = None
    try:
        pump = JobPump(debug, job, emit)
        pending, eof, closed = b'', source is None, False
        end = time.monotonic() + timeout_ms / 1000 + 3
        while True:
            status = pump.output()
            if status is not None:
                return status
            if not eof and not pending:
                pending = source.read(MAX_DATA)
                eof = not pending
            if pending:
                try:
                    pending = pump.input(pending)
                except LinuxError as exc:
                    if exc.errno != errno.EPIPE:
                        raise
                    # Child can deliberately close stdin before it exits.
                    pending, eof, closed = b'', True, True
            if eof and not pending and not closed:
                debug.close_stdin(job)
                closed = True
            if time.monotonic() >= end:
                raise TimeoutError('host deadline exceeded; awaiting owned job cancellation')
            time.sleep(.01)
    except BaseException as exc:
        error = exc
        raise
    finally:
        _close_preserving_error(lambda: finish_job(debug, job), error)


def run_shell(debug, terminal, timeout_ms=300000):
    """Terminal supplies poll()/size()/emit(); transport runs on this thread.

    Ctrl-] cancels locally. Ctrl-C is passed through to the remote PTY. The
    terminal is entered by the caller, and must restore modes on every exit.
    """
    job = debug.shell(timeout_ms)['id']
    error = None
    try:
        pump = JobPump(debug, job, terminal.emit)
        pending, previous_size = b'', None
        end = time.monotonic() + timeout_ms / 1000 + 3
        while True:
            size = terminal.size()
            if size != previous_size:
                debug.resize(job, *size)
                previous_size = size
            status = pump.output()
            if status is not None:
                return status
            if not pending:
                data = terminal.poll()
                if data is None or b'\x1d' in data:
                    return dict(cancelled_locally=True)
                pending = data
            pending = pump.input(pending)
            if time.monotonic() >= end:
                raise TimeoutError('interactive host deadline exceeded')
            time.sleep(.01)
    except BaseException as exc:
        error = exc
        raise
    finally:
        _close_preserving_error(lambda: finish_job(debug, job), error)
