"""Fenestra FXP2 client. Explicit CLI invocation opens Windows WPD; import does not.

Only Fenestra's vendor operations are admitted by the transport. No
maintenance mode, shell-tool dependency, driver replacement or firmware write.
"""
import argparse
import json
import struct
import time
import zlib
from pathlib import Path
from ptp_identity import IDENTITY_OPERATION,parse_identity

HELLO,SUBMIT,FETCH,CONTROL=0x95e0,0x95e1,0x95e2,0x95e3
HEADER=struct.Struct('<4sHHQIIII')
MAX_PAYLOAD=1024


class PtpError(RuntimeError):
    def __init__(self,code):
        self.code=code
        super().__init__(f'PTP response 0x{code:04x}')


class BackendError(RuntimeError):
    def __init__(self,code):
        self.code=code
        super().__init__(f'FXP2 backend result {code}')


def request_frame(session,request_id,operation,data=b''):
    if not 0<session<1<<64 or not 0<request_id<1<<32 or not 0<=operation<1<<16 or len(data)>MAX_PAYLOAD:
        raise ValueError('invalid FXP2 request')
    raw=HEADER.pack(b'FXP2',2,operation,session,request_id,len(data),0,0)+bytes(data)
    return raw[:28]+struct.pack('<I',zlib.crc32(raw))+raw[32:]


def parse_response(raw,session,request_id,operation):
    if not 32<=len(raw)<=1056:raise ValueError('FXP2 response length')
    magic,version,op,token,id,n,status,crc=HEADER.unpack_from(raw)
    if (magic,version,op,token,id,n)!=(b'FXP2',2,operation,session,request_id,len(raw)-32):
        raise ValueError('FXP2 response identity/length differs')
    if not status&0x80000000 or status&0x7fffffff>6:raise ValueError('FXP2 response status')
    if zlib.crc32(raw[:28]+b'\0'*4+raw[32:])!=crc:raise ValueError('FXP2 response CRC')
    if status!=0x80000000:raise BackendError(status&0x7fffffff)
    return raw[32:]


class Client:
    def __init__(self,transport):
        self.transport=transport;self.session=0;self.last_id=0;self.pending=None;self.capabilities=0
        self.control_available=False

    def hello(self):
        if self.pending is not None:raise RuntimeError('pending job must be resolved before a new handshake')
        self.session=0;self.last_id=0;self.capabilities=0;self.control_available=False
        raw=self.transport.read(HELLO,(),64)
        if len(raw)!=64 or raw[:4]!=b'FXP2' or struct.unpack_from('<I',raw,4)[0]!=2:
            raise ValueError('not a Fenestra FXP2 endpoint')
        token,limit,caps,firmware,retained=struct.unpack_from('<Q4I',raw,8)
        if not token or limit!=1024 or not firmware or retained!=1:raise ValueError('endpoint contract differs')
        self.session=token;self.last_id=0;self.capabilities=caps
        self.control_available=bool(struct.unpack_from('<I',raw,36)[0]&4)
        report=dict(protocol=2,firmware=f'C {firmware//100}.{firmware%100:02d}',session=token,payload_limit=limit,
            capabilities=caps,job_control=self.control_available,linux_backend=bool(struct.unpack_from('<I',raw,32)[0]),
            privileged_backend=bool(struct.unpack_from('<I',raw,36)[0]))
        if caps&(1<<(IDENTITY_OPERATION-1)):
            try:
                identity=self.identity()
                if identity['firmware_code']!=firmware or identity['capabilities']!=caps:
                    raise ValueError('identity differs from current handshake')
            except Exception:
                # Preserve an uncertain retained request for fetch/retry, but
                # never leave a completed malformed handshake usable.
                if self.pending is None:self.session=0
                raise
            report.update(model=identity['model'],identity=identity,identity_available=True)
        else:report['identity_available']=False
        return report

    def submit(self,operation,data=b''):
        if not self.session or self.pending is not None:raise RuntimeError('handshake required or job already pending')
        id=self.last_id+1
        raw=request_frame(self.session,id,operation,data)
        # Retain before transmission: a lost acknowledgement is not permission
        # to create a new request ID and execute the same operation twice.
        self.pending=(id,operation,raw);self.last_id=id
        self.transport.write(SUBMIT,(),raw)
        return id

    def retry_submit(self):
        if self.pending is None:raise RuntimeError('no retained request')
        self.transport.write(SUBMIT,(),self.pending[2])

    def fetch(self,timeout=3):
        if self.pending is None:raise RuntimeError('no pending request')
        if not 0<timeout<=30:raise ValueError('bounded timeout required')
        id,operation,_=self.pending;end=time.monotonic()+timeout
        params=(self.session&0xffffffff,self.session>>32,id,0,1056)
        while True:
            try:raw=self.transport.read(FETCH,params,1056)
            except PtpError as exc:
                if exc.code!=0x2019:raise
                if time.monotonic()>=end:raise TimeoutError(f'job {id} result not ready; retained for query') from exc
                time.sleep(.025);continue
            try:data=parse_response(raw,self.session,id,operation)
            except BackendError:
                self.pending=None
                raise
            self.pending=None
            return data

    def job_control(self,action=0,request_id=None):
        """Inspect, cancel, or release the retained job, outside the worker queue.

        RUNNING+cancel_requested means execution can still be in progress. No
        retry may use a new request identity until a terminal state is observed.
        A lost/malformed control response leaves pending unchanged.
        """
        if not self.session or not self.control_available:
            raise RuntimeError('endpoint does not advertise out-of-band job control')
        if action not in (0,1,2):raise ValueError('unknown job control action')
        id=request_id if request_id is not None else (self.pending[0] if self.pending else self.last_id)
        if type(id) is not int or not 0<id<1<<32:raise ValueError('job identity required')
        raw=self.transport.read(CONTROL,(self.session&0xffffffff,self.session>>32,id,action),32)
        if len(raw)!=32:raise ValueError('job control length')
        magic,version,actual,state,cancelled,size,session=struct.unpack('<4s5IQ',raw)
        if ((magic,version,actual,session)!=(b'FXC1',1,id,self.session) or state not in range(1,6)
                or cancelled not in (0,1) or size>1056 or (state!=3 and size)):
            raise ValueError('job control identity/state')
        if state in (4,5) and self.pending and self.pending[0]==id:self.pending=None
        return dict(request_id=id,state=state,cancel_requested=bool(cancelled),response_bytes=size,
                    terminal=state in (3,4,5))

    def cancel(self):
        return self.job_control(1)

    def release(self,request_id=None):
        return self.job_control(2,request_id)

    def query(self,operation,data=b''):
        if operation not in range(1,33) or not self.capabilities&(1<<(operation-1)):
            raise RuntimeError('operation not advertised by this firmware')
        self.submit(operation,data)
        return self.fetch()

    def identity(self):
        return parse_identity(self.query(IDENTITY_OPERATION))

    def diagnostics(self):
        raw=self.query(3)
        if len(raw)!=128 or raw[:4]!=b'FXD1' or struct.unpack_from('<II',raw,4)!=(1,128):
            raise ValueError('diagnostic schema differs')
        names=['error','task_id','frame','invocations','context_checks','context_errors',
               'el0_entries','el0_exits','faults','card_load_attempts']
        result=dict(zip(names,struct.unpack_from('<10I',raw,24)))
        result.update(phase=struct.unpack_from('<I',raw,12)[0],uptime_ms=struct.unpack_from('<Q',raw,16)[0])
        result.update(zip(['esr','elr','far'],[f'0x{x:x}' for x in struct.unpack_from('<3Q',raw,64)]))
        result.update(zip(['text_state','generation','outstanding','module_bytes','entry_offset',
            'package_bytes','capabilities'],struct.unpack_from('<7I',raw,88)))
        return result

    def regions(self):
        # A completed old endpoint ARGUMENT is safe to retry with its legacy
        # read-only schema. Transport timeouts/uncertain jobs are never retried.
        try:raw=self.query(4,struct.pack('<I',0))
        except BackendError as error:
            if error.code!=1:raise
            raw=self.query(4)
        regions=[];seen=set();cursor=0
        while True:
            if len(raw)<16:raise ValueError('region schema differs')
            magic,version,count,stride=struct.unpack_from('<4sIII',raw)
            paged=(magic,version)==(b'FXR3',3)
            if paged:
                if len(raw)<32:raise ValueError('paged region header truncated')
                total,nxt,reserved0,reserved1=struct.unpack_from('<4I',raw,16)
                if (stride!=32 or count>31 or len(raw)!=32+count*32 or not cursor<=total<=131068
                        or reserved0 or reserved1 or (nxt!=0xffffffff and (not cursor<nxt<=total or not count))):
                    raise ValueError('paged region geometry differs')
                start=32;last=total if nxt==0xffffffff else nxt
            else:
                if (cursor or (magic,version) not in ((b'FXR1',1),(b'FXR2',2)) or stride!=32 or count>8
                        or len(raw)!=16+count*32 or (version==1 and count!=2)):
                    raise ValueError('region schema differs')
                start=16
            for index in range(count):
                id,flags,size,volatile,address,reserved=struct.unpack_from('<4I2Q',raw,start+32*index)
                owner,kind=(reserved&0xffffffff,reserved>>32) if version>=2 else (1,index)
                if (not 1<=owner<=(65534 if paged else 4) or kind>1 or id!=(owner-1)*2+kind+1 or id in seen
                        or flags!=1 or not 0<size<=(1048576 if kind==0 else 4194304) or volatile!=kind
                        or (version==1 and reserved) or (paged and not cursor<id<=last)):
                    raise ValueError('invalid region descriptor')
                seen.add(id)
                regions.append(dict(id=id,name='package' if kind==0 else 'module-image',size=size,
                    read_only=True,volatile=bool(volatile),address=f'0x{address:x}',**({'owner':owner} if version>=2 else {})))
            if not paged or nxt==0xffffffff:return regions
            cursor=nxt;raw=self.query(4,struct.pack('<I',cursor))

    def read_region(self,region,offset,length):
        if type(region) is not int or not 1<=region<=131068 or not 0<=offset<1<<32 or not 1<=length<=MAX_PAYLOAD:
            raise ValueError('region read requires id1..131068, uint32 offset and length1..1024')
        raw=self.query(5,struct.pack('<III',region,offset,length))
        if len(raw)!=length:raise ValueError('short region read')
        return raw


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['hello','identity','status','echo','diagnostics','regions','read-region'])
    parser.add_argument('--hex',default='',help='binary echo input, at most1024 bytes')
    parser.add_argument('--region',type=int,help='region id returned by regions')
    parser.add_argument('--offset',type=lambda s:int(s,0),default=0)
    parser.add_argument('--length',type=int,default=256,help='region read length1..1024')
    parser.add_argument('--output',type=Path,help='new file for region bytes; existing files are preserved')
    args=parser.parse_args()
    data=bytes.fromhex(args.hex)
    if len(data)>1024 or (args.command!='echo' and data):parser.error('invalid payload')
    if args.command=='read-region' and (args.region is None or not 0<=args.offset<1<<32 or not 1<=args.length<=1024):
        parser.error('read-region requires --region1/2, nonnegative uint32 --offset and --length1..1024')
    if args.command!='read-region' and (args.region is not None or args.output is not None or args.offset!=0 or args.length!=256):
        parser.error('region arguments require read-region')
    if args.output is not None and args.output.exists():parser.error('output already exists')
    from ptp_transport import open_transport
    with open_transport() as transport:
        client=Client(transport);report=client.hello()
        if args.command=='identity':report['identity']=client.identity()
        elif args.command=='diagnostics':report['diagnostics']=client.diagnostics()
        elif args.command=='regions':report['regions']=client.regions()
        elif args.command=='read-region':
            result=client.read_region(args.region,args.offset,args.length)
            if args.output is not None:
                with args.output.open('xb') as stream:stream.write(result)
                report['output']=str(args.output.resolve());report['bytes']=len(result)
            else:report['result_hex']=result.hex()
        elif args.command!='hello':
            client.submit(1 if args.command=='status' else 2,data)
            result=client.fetch();report['result_hex']=result.hex()
            if args.command=='echo' and result!=data:raise ValueError('echo mismatch')
        print(json.dumps(report,indent=2))


if __name__=='__main__':main()
