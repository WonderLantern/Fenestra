"""Linux agent bootstrap and debug commands over Fenestra's normal PTP endpoint.

No external shell tool is used. Bootstrap writes only a new private directory
on a /tmp mount verified as tmpfs. Privileged commands run at the native UID;
they are not a sandbox or a guarantee against user-requested Flash writes.
"""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import re
import secrets
import struct
import sys
import time
from linux_debug import LinuxDebug
from ptp_client import Client, BackendError


class LinuxBridge:
    def __init__(self, client):
        self.client=client
        self.nonce=secrets.randbits(64) or 1
        self.serial=0
        self.pending=None
        self.call_pending=None
        self.owns_allocation=False

    def call(self, op, data=b''):
        identity=(op,bytes(data))
        if self.call_pending is not None:
            if self.call_pending!=identity:
                raise RuntimeError('resolve retained bridge call before issuing another operation')
            # Resume only its retained FXP2 response, never submit a second job.
            try:result=self.client.fetch()
            except BaseException:
                if getattr(self.client,'pending',None) is None:self.call_pending=None
                raise
            self.call_pending=None
            return result
        self.call_pending=identity
        try:
            result=self.client.query(6, struct.pack('<I', op)+data)
        except BaseException:
            if getattr(self.client,'pending',None) is None:self.call_pending=None
            raise
        self.call_pending=None
        return result

    def status(self):
        raw=self.call(0)
        if len(raw)!=48 or raw[:4]!=b'FXB1':raise ValueError('bridge schema')
        version=struct.unpack_from('<I',raw,4)[0]
        if version not in (1,2):raise ValueError('bridge version')
        hook,ready,address,nonce,running,completions,dropped,sequence=struct.unpack_from('<IIQQ4I',raw,8)
        return dict(version=version,hooked=hook,ready=ready,address=address,nonce=nonce,running=running,
                    completions=completions,dropped=dropped,sequence=sequence)

    def command(self, command, timeout=10, *, stage=None, chunk=b''):
        # The marker correlates our command across the original untagged text
        # channel. A completion event alone is never taken as proof of success.
        self.serial+=1
        marker=f'FXB{self.nonce:016x}{self.serial:08x}='
        wrapped='{\n'+command+'\n}; r=$?; printf "'+marker+'%d\\n" "$r"'
        if len(wrapped.encode('ascii'))>900:raise ValueError('bootstrap command too long')
        if stage is None:self.call(2,wrapped.encode('ascii'))
        else:self.call(7,struct.pack('<II',stage,self.serial)+chunk)
        output=b'';offset=0;end=time.monotonic()+timeout
        while time.monotonic()<end:
            raw=self.call(3,struct.pack('<Q',offset))
            if len(raw)<32:raise ValueError('short bootstrap output')
            base,last,running,dropped,count=struct.unpack_from('<QQIII',raw)
            if dropped or base>offset or len(raw)!=32+count:raise ValueError('bootstrap output lost')
            output+=raw[32:];offset+=count
            if len(output)>8192:raise ValueError('bootstrap output too large')
            # The camera's normal text IPC strips line endings. Accept an
            # end-of-output marker only once completion and all bytes arrived;
            # the echoed command contains %d and cannot satisfy this pattern.
            complete=not running and offset==last
            suffix=rb'(\d+)(?:\r?\n|$)' if complete else rb'(\d+)\r?\n'
            match=re.search(re.escape(marker.encode())+suffix,output)
            if match and not running and offset==last:
                code=int(match[1])
                if code:raise RuntimeError(f'bootstrap stage {self.serial} failed: {code}; {output!r}')
                return output[:match.start()]
            time.sleep(.025)
        raise TimeoutError('bootstrap completion marker missing; do not replay the command')

    def bootstrap(self, executable, progress=None):
        raw=Path(executable).read_bytes()
        if not 4096<=len(raw)<=2000000 or raw[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',raw,18)[0]!=183:
            raise ValueError('bounded little-endian AArch64 ELF required')
        self.call(1,struct.pack('<Q',self.nonce));status=self.status()
        if status['nonce']!=self.nonce or not status['address']:raise ValueError('shared allocation differs')
        self.owns_allocation=True
        typed=status.get('version',1)>=2
        directory=f'/tmp/fenestra-{self.nonce:016x}'
        # Refuse rootfs-backed /tmp, symlink, existing directory, absent applets.
        self.command('ok=0; while read a b c d; do '
            '[ "$b:$c" = /tmp:tmpfs ] && ok=1; done < /proc/mounts; '
            '[ "$ok" = 1 ] && [ ! -L /tmp ] && base64 -d </dev/null >/dev/null && '
            '(gzip -c </dev/null | gzip -dc >/dev/null) && (umask 077; mkdir '+directory+')',stage=0 if typed else None)
        encoded=base64.b64encode(gzip.compress(raw,mtime=0)).decode('ascii')
        stride=512 if typed else 640
        for offset in range(0,len(encoded),stride):
            chunk=encoded[offset:offset+stride]
            self.command("printf '%s' '"+chunk+"' >> "+directory+'/agent.b64',
                         stage=1 if typed else None,chunk=chunk.encode('ascii'))
            if progress:progress(min(offset+stride,len(encoded)),len(encoded))
        # gzip validates the uploaded stream's length and CRC before launching.
        # SHA256 below identifies the host ELF; it is not a remote SHA assertion.
        self.command('base64 -d '+directory+'/agent.b64 | gzip -dc > '+directory+'/agent',stage=2 if typed else None)
        self.command('chmod 700 '+directory+'/agent && '+directory+
            f'/agent {status["address"]:x} {self.nonce:x} C1.10 </dev/null >/dev/null 2>&1 &',stage=3 if typed else None)
        end=time.monotonic()+10
        while time.monotonic()<end:
            if self.status()['ready']:
                info=LinuxDebug(self.exchange).info()
                return dict(agent_sha256=hashlib.sha256(raw).hexdigest(),agent_bytes=len(raw),
                            directory=directory,remote_upload_check='gzip CRC32 and length',linux=info)
            time.sleep(.1)
        raise TimeoutError('Linux agent did not complete shared-memory handshake')

    def exchange(self, request):
        if self.pending is not None:raise RuntimeError('resolve retained Linux request first')
        if len(request)>1020:raise ValueError('PTP Linux request limit is1020 bytes')
        sequence=struct.unpack('<I',self.call(4,request))[0]
        self.pending=sequence
        return self.fetch()

    def fetch(self, timeout=5):
        if self.pending is None:raise RuntimeError('no retained Linux request')
        end=time.monotonic()+timeout
        while True:
            try:result=self.call(5,struct.pack('<I',self.pending))
            except BackendError as exc:
                if exc.code!=4:raise
                if time.monotonic()>=end:raise TimeoutError('Linux request retained; use fetch, never replay') from exc
                time.sleep(.025);continue
            self.pending=None
            return result

    def stop(self):
        if self.pending is not None or self.call_pending is not None:
            raise RuntimeError('Linux/FXP2 result still pending; resolve it or use local USB Off/restart')
        # A failed allocation (e.g. BUSY because another agent still owns it)
        # must not cancel somebody else's session during error cleanup.
        if not self.owns_allocation:return
        if self.status()['nonce']!=self.nonce:
            self.owns_allocation=False
            raise RuntimeError('bridge ownership changed; foreign agent was not stopped')
        self.call(6)
        self.owns_allocation=False


def argument_parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--agent',type=Path,required=True)
    sub=p.add_subparsers(dest='command',required=True)
    sub.add_parser('info')
    execute=sub.add_parser('exec',help='stream stdout/stderr and propagate exit status')
    execute.add_argument('--timeout-ms',type=int,default=30000)
    execute.add_argument('--stdin',type=Path,help='binary input file; default is immediate pipe EOF')
    execute.add_argument('argv',nargs=argparse.REMAINDER)
    shell=sub.add_parser('shell',help='interactive PTY; Ctrl-] exits, Ctrl-C goes to camera shell')
    shell.add_argument('--timeout-ms',type=int,default=300000)
    stat=sub.add_parser('stat');stat.add_argument('remote')
    get=sub.add_parser('get',help='download regular file into a new local file')
    get.add_argument('remote');get.add_argument('output',type=Path)
    put=sub.add_parser('put',help='exclusive remote create, fdatasync and SHA-256 readback')
    put.add_argument('input',type=Path);put.add_argument('remote')
    for cmd in (get,put):cmd.add_argument('--max-bytes',type=int,default=64*1024*1024)
    maps=sub.add_parser('maps');maps.add_argument('pid',type=int)
    maps.add_argument('--output',type=Path)
    read=sub.add_parser('memory-read',help='bounded process memory dump, not an atomic snapshot')
    read.add_argument('pid',type=int);read.add_argument('address',type=lambda x:int(x,0))
    read.add_argument('length',type=lambda x:int(x,0));read.add_argument('output',type=Path)
    write=sub.add_parser('memory-write',help='explicit privileged process memory modification')
    write.add_argument('pid',type=int);write.add_argument('address',type=lambda x:int(x,0))
    source=write.add_mutually_exclusive_group(required=True)
    source.add_argument('--hex');source.add_argument('--input',type=Path)
    return p


def parse_arguments(argv=None):
    from linux_debug import _path, _timeout, _uint, MAX_REQUEST
    from linux_session import transfer_limit
    p=argument_parser();a=p.parse_args(argv)
    try:
        if not a.agent.is_file():raise ValueError('agent file does not exist')
        if hasattr(a,'remote'):_path(a.remote)
        if hasattr(a,'remote') and len(a.remote.encode('utf-8'))>MAX_REQUEST-8:
            raise ValueError('remote path exceeds bridge request limit')
        if hasattr(a,'timeout_ms'):_timeout(a.timeout_ms)
        if hasattr(a,'pid'):_uint(a.pid,31,1)
        if hasattr(a,'max_bytes'):transfer_limit(a.max_bytes)
        output=getattr(a,'output',None)
        if output is not None:
            if output.exists() or output.is_symlink():raise ValueError('output already exists')
            if not output.parent.is_dir():raise ValueError('output parent does not exist')
        for name in ('input','stdin'):
            source=getattr(a,name,None)
            if source is not None:
                if not source.is_file():raise ValueError(name+' must be a regular input file')
                transfer_limit(source.stat().st_size,getattr(a,'max_bytes',64*1024*1024))
        if a.command=='exec':
            if a.argv and a.argv[0]=='--':a.argv=a.argv[1:]
            if not 1<=len(a.argv)<=32 or not a.argv[0].startswith('/') or any('\0' in v for v in a.argv):
                raise ValueError('exec requires an absolute executable and 1..32 NUL-free arguments')
            if 12+sum(len(v.encode('utf-8'))+1 for v in a.argv)>MAX_REQUEST:
                raise ValueError('argv exceeds bridge request limit')
        if a.command=='shell':
            from debug_terminal import DebugTerminal
            DebugTerminal.check()
        if a.command=='memory-write':
            a.data=bytes.fromhex(a.hex) if a.hex is not None else a.input.read_bytes()
            a.length=len(a.data)
        if a.command in ('memory-read','memory-write'):
            transfer_limit(a.length)
            if not a.length or not 0<=a.address<(1<<63)-a.length:
                raise ValueError('nonempty process address range within signed 64-bit required')
    except (ValueError,OSError) as exc:p.error(str(exc))
    return a


def execute_command(a,debug,info):
    from contextlib import ExitStack
    from debug_terminal import DebugTerminal
    from linux_session import (require_operations,download,upload,memory_dump,memory_write,
                               read_maps,run_exec,run_shell)
    operations={'info':(1,), 'exec':(2,4,5,6,7,8,18), 'shell':(3,4,5,6,7,8,9),
                'get':(10,11,13), 'put':(10,11,12,13,19), 'stat':(14,),
                'maps':(17,), 'memory-read':(15,), 'memory-write':(16,)}
    require_operations(info,*operations[a.command])
    if a.command=='info':return info,0
    if a.command=='exec':
        with ExitStack() as stack:
            source=stack.enter_context(a.stdin.open('rb')) if a.stdin is not None else None
            result=run_exec(debug,a.argv,DebugTerminal.emit,source,a.timeout_ms)
        code=124 if result['timed_out'] else (128+result['signal'] if result['signal'] else result['exit_code'])
        return result,code
    if a.command=='shell':
        with DebugTerminal() as terminal:result=run_shell(debug,terminal,a.timeout_ms)
        code=130 if result.get('cancelled_locally') else (124 if result['timed_out'] else
             128+result['signal'] if result['signal'] else result['exit_code'])
        return result,code
    if a.command=='get':return download(debug,a.remote,a.output,a.max_bytes),0
    if a.command=='put':return upload(debug,a.input,a.remote,a.max_bytes),0
    if a.command=='stat':return debug.stat(a.remote),0
    if a.command=='memory-read':return memory_dump(debug,a.pid,a.address,a.length,a.output),0
    if a.command=='memory-write':return memory_write(debug,a.pid,a.address,a.data),0
    if a.command=='maps':
        data=read_maps(debug,a.pid)
        if a.output:
            with a.output.open('xb') as f:f.write(data)
        else:DebugTerminal.emit(0,data)
        return dict(pid=a.pid,bytes=len(data),atomic_snapshot=False),0
    raise ValueError('unknown command')


def main(argv=None):
    a=parse_arguments(argv)
    from ptp_transport import open_transport
    with open_transport() as transport:
        client=Client(transport);client.hello();bridge=LinuxBridge(client)
        error=None
        try:
            report=bridge.bootstrap(a.agent);print(json.dumps(report,indent=2),file=sys.stderr)
            debug=LinuxDebug(bridge.exchange)
            result,code=execute_command(a,debug,report['linux'])
            print(json.dumps(result,indent=2),file=sys.stderr)
            return code
        except BaseException as exc:
            error=exc
            if client.pending is not None or bridge.pending is not None:
                exc.add_note(f'retained FXP2 request={client.pending[0] if client.pending else None}, '
                             f'Linux sequence={bridge.pending}; no automatic write/execute replay')
            raise
        finally:
            try:bridge.stop()
            except Exception as cleanup:
                if error is None:raise
                error.add_note(f'agent stop not confirmed: {cleanup}; use local USB Off/restart')


if __name__=='__main__':sys.exit(main())
