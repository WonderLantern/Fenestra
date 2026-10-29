"""Native RTOS register, translation and DRAM monitor over Fenestra PTP.

Native command and eight-integer-argument call jobs use the target's original
parser and a dedicated task. Cancellation does not kill arbitrary native code. Memory and function-call
addresses are passed directly to the native debugger without region policy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
from ptp_client import Client


class RtosDebug:
    def __init__(self, client):self.client=client
    def request(self, op, data=b''):return self.client.query(7,struct.pack('<I',op)+data)
    def registers(self):
        raw=self.request(0)
        if len(raw)!=96 or raw[:8]!=b'FXT1\x01\0\0\0':raise ValueError('RTOS monitor schema')
        names=('CurrentEL','DAIF','TTBR0_EL1','TTBR1_EL1','TCR_EL1','MAIR_EL1','VBAR_EL1',
               'TPIDRRO_EL0','CNTPCT_EL0','CNTFRQ_EL0')
        return dict(zip(names,(f'0x{x:x}' for x in struct.unpack_from('<10Q',raw,16))))
    def translate(self,address,write=False):
        par=struct.unpack('<Q',self.request(1,struct.pack('<QI',address,int(write))))[0]
        return dict(par=f'0x{par:016x}',fault=bool(par&1),physical=None if par&1 else
                    f'0x{(par&0x0000fffffffff000)|(address&4095):x}',attribute=par>>56)
    def read(self,address,count):
        if not 1<=count<=1008:raise ValueError('read count1..1008')
        if not 0<=address<=(1<<64)-count:raise ValueError('uint64 address range required')
        raw=self.request(2,struct.pack('<QI',address,count))
        if len(raw)!=count:raise ValueError('short native RAM read')
        return raw
    def write(self,address,data):
        if not 1<=len(data)<=1008:raise ValueError('write count1..1008')
        if not 0<=address<=(1<<64)-len(data):raise ValueError('uint64 address range required')
        written=struct.unpack('<I',self.request(3,struct.pack('<QI',address,len(data))+data))[0]
        if written!=len(data):raise ValueError('short native RAM write')
        return written

    def dump(self,address,length,output):
        if not 0<=address<1<<64 or not 0<length<=(1<<64)-address:
            raise ValueError('uint64 address/length required')
        digest=hashlib.sha256();offset=0
        with Path(output).open('xb') as stream:
            while offset<length:
                try:data=self.read(address+offset,min(1008,length-offset))
                except Exception as exc:
                    exc.add_note(f'RTOS dump stopped at +0x{offset:x}; partial output retained')
                    raise
                stream.write(data);digest.update(data);offset+=len(data)
        return dict(address=hex(address),bytes=offset,sha256=digest.hexdigest(),
                    output=str(Path(output).resolve()),atomic_snapshot=False)

    def job_status(self):
        raw=self.request(4)
        if len(raw)!=64 or raw[:8]!=b'FXS1\x01\0\0\0':raise ValueError('RTOS job schema')
        caps,phase,job,task=struct.unpack_from('<4I',raw,8)
        if phase>5:raise ValueError('unknown RTOS job phase')
        result,produced,dropped,session,generation,closed,timeout,kind=struct.unpack_from('<QIIQ4I',raw,24)
        if produced>8192 or kind>1 or closed>1:raise ValueError('RTOS job bounds')
        return dict(capabilities=caps,phase=phase,state=('idle','queued','running','done','expired','revoked')[phase],
                    job=job,task=task,result=result if phase==3 and kind==1 else None,
                    produced=produced,dropped=dropped,session=session,generation=generation,
                    restart_required=bool(closed),timeout_ms=timeout,kind=kind)

    def submit_command(self,line,timeout_ms=5000):
        raw=line.encode('utf-8')
        if not 1<=len(raw)<=511:raise ValueError('command payload maximum511 bytes')
        return self._submit(0,raw,timeout_ms)

    def submit_call(self,address,args=(),timeout_ms=5000):
        args=tuple(args)
        if len(args)>8 or any(not 0<=x<1<<64 for x in args):raise ValueError('up to8 uint64 arguments required')
        return self._submit(1,struct.pack('<9Q',address,*(args+(0,)*(8-len(args)))),timeout_ms)

    def _submit(self,kind,data,timeout_ms):
        if not 0<=timeout_ms<1<<32:raise ValueError('uint32 timeout required')
        # Client.query retains an uncertain request; it must never be replayed
        # with a new FXP2 identity after a transport error.
        raw=self.request(5,struct.pack('<II',kind,timeout_ms)+data)
        if len(raw)!=4 or not (job:=struct.unpack('<I',raw)[0]):raise ValueError('RTOS job identifier')
        return job

    def read_output(self,job,offset,count):
        if not 0<job<1<<32 or not 0<=offset<=8192 or not 1<=count<=1024 or offset+count>8192:
            raise ValueError('RTOS output bounds')
        raw=self.request(6,struct.pack('<III',job,offset,count))
        if len(raw)!=count:raise ValueError('short RTOS output')
        return raw

    def acknowledge(self,job):
        if not 0<job<1<<32:raise ValueError('RTOS job identifier')
        if self.request(7,struct.pack('<I',job)):raise ValueError('RTOS acknowledge schema')

    def cancel(self,job):
        if not 0<job<1<<32:raise ValueError('RTOS job identifier')
        if self.request(8,struct.pack('<I',job)):raise ValueError('RTOS cancel schema')

    def wait_job(self,job,timeout=32.0,stream=None):
        if timeout<0:raise ValueError('negative host wait')
        end=time.monotonic()+timeout if timeout else None;offset=0;output=bytearray()
        while True:
            status=self.job_status()
            job=status['job']
            if status['produced']<offset:raise ValueError('RTOS output cursor regressed')
            while offset<status['produced']:
                data=self.read_output(job,offset,min(1024,status['produced']-offset))
                offset+=len(data);output.extend(data)
                if stream is not None:stream(data)
            if status['phase'] in (3,4,5):
                return dict(**status,output=bytes(output))
            if end is not None and time.monotonic()>=end:
                raise TimeoutError(f'RTOS job{job} wait expired; job/output retained, no replay or force-kill')
            time.sleep(0.02)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['registers','translate','read','write','dump','status','execute','shell','call','ack','cancel'])
    p.add_argument('--address',type=lambda v:int(v,0))
    p.add_argument('--count',type=lambda v:int(v,0),default=256)
    p.add_argument('--hex')
    p.add_argument('--output',type=Path)
    p.add_argument('--write-translation',action='store_true')
    p.add_argument('--line',help='native RTOS command, e.g. echo Fenestra')
    p.add_argument('--arg',type=lambda v:int(v,0),action='append',default=[])
    p.add_argument('--timeout-ms',type=int,default=5000)
    p.add_argument('--job',type=lambda v:int(v,0))
    a=p.parse_args()
    if a.command in ('translate','read','write','dump','call') and (a.address is None or not 0<=a.address<1<<64):p.error('uint64 --address required')
    if a.command=='execute' and not a.line:p.error('execute requires --line')
    if a.command in ('ack','cancel') and (a.job is None or not 0<a.job<1<<32):p.error('ack/cancel requires uint32 --job')
    if not 0<=a.timeout_ms<1<<32:p.error('uint32 timeout required; 0 waits without a host deadline')
    if a.command=='write' and not a.hex:p.error('write requires --hex')
    if a.output and (a.output.exists() or a.output.is_symlink()):p.error('output already exists')
    if a.output and a.command not in ('read','dump'):p.error('--output requires read/dump')
    if a.command=='dump' and a.output is None:p.error('dump requires --output')
    if a.command in ('read','dump'):
        limit=1008 if a.command=='read' else (1<<64)-a.address
        if not 1<=a.count<=limit or a.address+a.count>1<<64:p.error('invalid bounded read range')
    data=b''
    if a.command=='write':
        try:data=bytes.fromhex(a.hex)
        except ValueError:p.error('invalid --hex')
        if not 1<=len(data)<=1008 or a.address+len(data)>1<<64:p.error('invalid bounded write range')
    from ptp_transport import open_transport
    with open_transport() as transport:
        client=Client(transport);client.hello();debug=RtosDebug(client)
        if a.command=='registers':result=debug.registers()
        elif a.command=='status':result=debug.job_status()
        elif a.command=='ack':debug.acknowledge(a.job);result=dict(acknowledged=a.job)
        elif a.command=='cancel':debug.cancel(a.job);result=debug.job_status()
        elif a.command in ('execute','call','shell'):
            def run(line=None):
                job=debug.submit_call(a.address,a.arg,a.timeout_ms) if a.command=='call' else debug.submit_command(line,a.timeout_ms)
                result=debug.wait_job(job,timeout=a.timeout_ms/1000+2 if a.timeout_ms else 0,
                    stream=lambda data:print(data.decode('utf-8',errors='replace'),end='',flush=True))
                result.pop('output')
                if result['phase']==3:debug.acknowledge(job)
                return result
            if a.command=='shell':
                while True:
                    try:line=input('RTOS> ')
                    except (EOFError,KeyboardInterrupt):break
                    if line.strip() in ('exit','quit'):break
                    if not line.strip():continue
                    result=run(line);print('\n'+json.dumps(result))
                    if result['restart_required']:break
                return
            result=run(a.line)
        elif a.command=='translate':result=debug.translate(a.address,a.write_translation)
        elif a.command=='dump':result=debug.dump(a.address,a.count,a.output)
        elif a.command=='read':
            data=debug.read(a.address,a.count)
            if a.output:
                with a.output.open('xb') as f:f.write(data)
                result=dict(output=str(a.output.resolve()),bytes=len(data))
            else:result=dict(hex=data.hex())
        else:result=dict(written=debug.write(a.address,data))
        print(json.dumps(result,indent=2))


if __name__=='__main__':main()
