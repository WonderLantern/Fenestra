"""PTP over an explicitly configured child process (normally OpenSSH stdio).

No shell on the local host, no automatic retries/reconnect, one in-flight call.
Lost/invalid replies poison this transport; Client.pending remains untouched.
"""
import json
import math
import os
from pathlib import Path
import queue
import struct
import subprocess
import threading
from ptp_client import PtpError
from ptp_contract import check_profile, identify
from ptp_pipe_protocol import (OPEN, READ, WRITE, CLOSE, REPLY, PTP_ERROR, FATAL,
                               read_frame, write_frame, pack_transfer)


class ProcessPtp:
    def __init__(self, argv, timeout_seconds=30, profile='auto'):
        if (not isinstance(argv, list) or not argv or len(argv) > 64 or
                any(not isinstance(x, str) or not x or '\0' in x for x in argv)):
            raise ValueError('PTP process requires an explicit argv array')
        if (type(timeout_seconds) not in (int, float) or
                not math.isfinite(timeout_seconds) or not 0.1 <= timeout_seconds <= 120):
            raise ValueError('PTP process timeout 0.1..120 seconds')
        self.argv = list(argv)
        self.timeout = timeout_seconds
        self.expected_profile = check_profile(profile)
        self.process = None
        self.tainted = False
        self.sequence = 0
        self.lock = threading.Lock()
        self.started = False
        self.io_worker = None

    @classmethod
    def from_config(cls, path, profile='auto'):
        with Path(path).open('rb') as stream:
            data = stream.read(16385)
        if len(data) > 16384:
            raise ValueError('PTP process configuration exceeds 16 KiB')
        config = json.loads(data)
        if not isinstance(config, dict) or set(config) - {'argv', 'timeout_seconds'}:
            raise ValueError('PTP process configuration keys')
        return cls(config['argv'], config.get('timeout_seconds', 30), profile)

    def __enter__(self):
        if self.started:
            raise RuntimeError('PTP process object cannot be reopened')
        self.started = True
        # An inherited selector must not recursively open this same proxy.
        env = dict(os.environ)
        env.pop('FENESTRA_PTP_PROCESS_CONFIG', None)
        self.process = subprocess.Popen(self.argv, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=None, bufsize=0, shell=False, env=env,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            identity = json.loads(self._request(OPEN))
            if not isinstance(identity, dict) or set(identity) != {'profile', 'model', 'version'}:
                raise ValueError('PTP process identity fields')
            self.profile = identify(identity['model'], identity['version'], self.expected_profile)
            if self.profile != identity['profile']:
                raise ValueError('PTP process identity profile differs')
            self.identity = identity
            return self
        except BaseException:
            self.tainted = True
            self._stop()
            raise

    def _stop(self):
        process, self.process = self.process, None
        if process is None:
            return
        if process.poll() is None:
            process.kill()  # only the child created by this instance
        try:
            process.wait(timeout=3)
        finally:
            for stream in (process.stdin, process.stdout):
                stream.close()
            if self.io_worker is not None:
                self.io_worker.join(timeout=1)

    def _request(self, kind, data=b''):
        if self.process is None or self.tainted:
            raise RuntimeError('PTP process closed or uncertain; no replay')
        if not self.lock.acquire(blocking=False):
            raise RuntimeError('PTP process already has an in-flight transaction')
        try:
            self.sequence += 1
            if self.sequence >= 1 << 32:
                raise RuntimeError('PTP process sequence exhausted; reopen')
            result = queue.Queue(maxsize=1)
            process = self.process
            def exchange():
                try:
                    write_frame(process.stdin, kind, self.sequence, data)
                    result.put((True, read_frame(process.stdout)))
                except BaseException as exc:
                    result.put((False, exc))
            worker = threading.Thread(target=exchange, daemon=True)
            self.io_worker = worker
            worker.start()
            try:
                okay, value = result.get(timeout=self.timeout)
            except queue.Empty:
                raise TimeoutError('PTP process reply deadline; transaction may have executed') from None
            if not okay:
                raise value
            reply, seq, payload = value
            if seq != self.sequence:
                raise ValueError('PTP process response sequence differs')
            if reply == PTP_ERROR and len(payload) == 4:
                code = struct.unpack('<I', payload)[0]
                if not 0x2000 <= code <= 0xffff:
                    raise ValueError('PTP process response code')
                raise PtpError(code)
            if reply == FATAL:
                raise OSError('remote PTP endpoint ended; transaction may have executed')
            if reply != kind | REPLY:
                raise ValueError('PTP process response operation differs')
            return payload
        except PtpError:
            raise
        except BaseException:
            self.tainted = True
            self._stop()
            raise
        finally:
            self.lock.release()

    def read(self, opcode, params, maximum):
        raw = self._request(READ, pack_transfer(opcode, params, None, maximum))
        if len(raw) > maximum:
            self.tainted = True
            self._stop()
            raise ValueError('PTP process read exceeds requested size')
        return raw

    def write(self, opcode, params, payload):
        raw = self._request(WRITE, pack_transfer(opcode, params, payload, 0))
        if raw:
            self.tainted = True
            self._stop()
            raise ValueError('PTP process write reply has unexpected data')

    def close(self):
        try:
            if self.process is not None and not self.tainted:
                if self._request(CLOSE):
                    raise ValueError('PTP process close reply has unexpected data')
                self.process.wait(timeout=3)
                if self.process.returncode:
                    raise OSError('PTP endpoint close failed')
        finally:
            self._stop()

    def __exit__(self, kind, *args):
        try:
            self.close()
        except Exception:
            if kind is None:
                raise
