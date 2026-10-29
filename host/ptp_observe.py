"""Read-only built-in observations. Import never opens a camera.

Schema negotiation is mandatory; a C1.10 version label is insufficient. ARM
nonces must be retained before transmission. Reconnection queries an existing
capture; no shell/native-call work is kept alive by this protocol.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import uuid

from ptp_rtos import RtosDebug

BASE = '606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542'
PACKAGE = '43bf4f5f1278fa6b0a551ceb6fa4ee749dc74bbc8a5ef4fba19b30062184e546'
POOL_START, POOL_END, STRIDE = 0x8af5d9c00, 0x8f025fc00, 0x8a3400
SEGMENTS = [('sensor',0xa02b6f4c8,408),('gain',0xa057b2250,100),
    ('request-kind',0xa03df3d8c,4),('pool',0xa02be8df8,32),('video',0xa03df2938,192),
    ('manager',0xa03def068,64),('update-request',0xa057b0a20,128),
    ('send-scratch',0xa057b0eec,18),('sensor-after',0xa02b6f4c8,408),
    ('video-after',0xa03df2938,192)]
PHASES = ('IDLE','ARMED','RUNNING','STOPPING','COMPLETE','EXPIRED','CANCELLED','FAULT')


def demand(condition, message):
    if not condition:
        raise ValueError(message)


def status_decode(raw):
    demand(len(raw)==192 and raw[:12]==struct.pack('<4sII',b'FXO1',1,192),'observation status schema')
    phase=struct.unpack_from('<I',raw,12)[0]
    demand(phase<len(PHASES),'observation state')
    capture,boot,activation=struct.unpack_from('<3Q',raw,16)
    template,flags,period,duration,maximum,records,used,budget,reason,error=struct.unpack_from('<10I',raw,56)
    start,end,frequency=struct.unpack_from('<3Q',raw,96)
    demand(capture and boot and activation and frequency and template==1 and flags<=1 and
           100<=period<=30000 and 1<=duration<=30000 and 1<=maximum<=64 and
           records<=maximum and used==records*2154 and used<=budget<=196608,
           'observation status bounds')
    demand(raw[120:128]==bytes(8),'unknown observation proof flags')
    return dict(capture_id=capture,boot_epoch=boot,activation_epoch=activation,
        nonce=raw[40:56].hex(),phase=PHASES[phase],phase_code=phase,template=template,
        allow_disconnect=bool(flags),period_ms=period,duration_ms=duration,
        maximum_records=maximum,records=records,bytes=used,budget=budget,reason=reason,error=error,
        begin_ticks=start,end_ticks=end,frequency=frequency,sha256=raw[128:160].hex(),
        build_identity=raw[160:192].hex(),source_atomic=False,recording_active_proved=False,
        frame_association_proved=False,buffer_generation_proved=False,format_decoded=False)


def ticket_bytes(ticket):
    values=[ticket[name] for name in ('capture_id','boot_epoch','activation_epoch')]
    demand(all(isinstance(value,int) and 0<value<1<<64 for value in values),'capture ticket')
    return struct.pack('<3Q',*values)


def replace_owned_receipt(path,result):
    """The initial nonce receipt survives a failed result-file write."""
    temporary=path.with_name(path.name+'.result-tmp')
    with temporary.open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2);stream.flush();os.fsync(stream.fileno())
    temporary.replace(path)


def records_decode(raw, ticket):
    demand(len(raw)==ticket['bytes'] and len(raw)%2154==0,'record export length')
    records=[]
    for offset in range(0,len(raw),2154):
        r=raw[offset:offset+2154]
        demand(r[:12]==struct.pack('<4sII',b'FXOR',1,2154),'record schema')
        sequence=struct.unpack_from('<I',r,12)[0]
        demand(sequence==len(records) and r[16:40]==ticket_bytes(ticket),'record identity/sequence')
        begin,end,frequency=struct.unpack_from('<3Q',r,40)
        trigger,template,count,errors,stable=struct.unpack_from('<5I',r,64)
        demand((trigger,template,count)==(1,1,10) and stable<=1 and
               r[84:96]==bytes(12) and r[96:128].hex()==ticket['build_identity'] and
               begin<=end and frequency==ticket['frequency'],'record provenance')
        at=128;segments=[]
        for label,address,size in SEGMENTS:
            actual,length,copied,before,after,par,error,flags=struct.unpack_from('<QIIQQQII',r,at)
            demand(actual==address and length==size and copied in (0,size) and before<=after and
                   flags==0 and error in (0,1) and bool(error)==(copied==0),'context segment bounds')
            data=r[at+48:at+48+size]
            demand(len(data)==size and (copied or not any(data)),'context segment payload')
            segments.append(dict(name=label,address=hex(address),bytes=size,copied=copied,
                begin_ticks=before,end_ticks=after,par=hex(par),error=error,raw_hex=data.hex()))
            at+=48+size
        demand(at==2154 and errors==sum(s['error'] for s in segments),'record length/error count')
        records.append(dict(sequence=sequence,begin_ticks=begin,end_ticks=end,frequency=frequency,
            trigger='periodic-software-context',context_equal_before_after=bool(stable),
            source_atomic=False,recording_active_proved=False,frame_association_proved=False,
            buffer_generation_proved=False,format_decoded=False,segments=segments))
    return records


class Observation:
    def __init__(self,client,expected_build=None):
        self.client=client;self.expected_build=expected_build;self.description=None

    def query(self,op,data=b''):
        return self.client.query(8,struct.pack('<I',op)+data)

    def describe(self):
        raw=self.query(0)
        demand(len(raw)==224 and raw[:12]==struct.pack('<4sII',b'FXOV',1,224),'observation schema')
        caps,chunk,budget,duration,hz,records,jobs,template,record_size=struct.unpack_from('<9I',raw,12)
        boot,activation=struct.unpack_from('<2Q',raw,48)
        demand(caps&~255==0 and 1<=chunk<=1008 and 1<=budget<=196608 and
               1<=duration<=30000 and 1<=hz<=10 and 1<=records<=64 and 1<=jobs<=128 and
               template==1 and record_size==2154 and boot and activation,'observation limits')
        demand(raw[64:96].hex()==BASE and raw[128:160].hex()==PACKAGE,'observation target/base differs')
        build=raw[96:128].hex()
        demand(any(raw[96:128]) and (self.expected_build is None or build==self.expected_build),
               'observation build identity differs')
        demand(struct.unpack_from('<2I',raw,160)==(1,1) and
               struct.unpack_from('<2Q',raw,168)==(POOL_START,POOL_END) and not any(raw[184:]),
               'unsupported observation policy')
        names=('normal_ram_read','video_pool_read','configuration_snapshot','offline_observation',
               'software_submit_trace','real_rec_events','frame_completion_and_lease','roi_sample')
        self.description=dict(schema='FXOV/1',base_sha256=BASE,package_base_sha256=PACKAGE,
            build_identity=build,build_identity_kind='source-and-build-configuration/v1',
            boot_epoch=boot,activation_epoch=activation,available={n:bool(caps&(1<<i)) for i,n in enumerate(names)},
            readonly=True,chunk_bytes=chunk,budget_bytes=budget,maximum_duration_ms=duration,
            maximum_hz=hz,maximum_records=records,maximum_captures_per_boot=jobs,template=template,
            record_bytes=record_size,retained_captures=1,internal_flash_persistence=False,
            normal_ram_physical_range=['0xa00000000','0xa40000000'],
            video_pool_identity_range=[hex(POOL_START),hex(POOL_END)],
            reboot_loses_records=True)
        return self.description

    def ensure(self,capability):
        description=self.description or self.describe()
        demand(description['available'][capability],f'observation capability unavailable: {capability}')
        return description

    def check_status(self,raw,ticket=None):
        result=status_decode(raw);description=self.description or self.describe()
        demand(result['build_identity']==description['build_identity'] and
               result['boot_epoch']==description['boot_epoch'] and
               result['activation_epoch']==description['activation_epoch'],'capture build/epoch differs')
        if ticket is not None:
            demand(ticket_bytes(result)==ticket_bytes(ticket) and
                   result['build_identity']==ticket['build_identity'] and
                   result['nonce']==ticket['nonce'],'different retained capture')
        return result

    def arm(self,nonce,*,period_ms=1000,duration_ms=30000,maximum=32,allow_disconnect=False,snapshot=False):
        self.ensure('configuration_snapshot' if snapshot else 'offline_observation')
        demand(isinstance(nonce,bytes) and len(nonce)==16 and any(nonce),'nonzero 16-byte arm nonce')
        demand(100<=period_ms<=30000 and 1<=duration_ms<=30000 and 1<=maximum<=64 and
               isinstance(allow_disconnect,bool),'arm limits')
        if snapshot:demand(maximum==1 and not allow_disconnect,'snapshot limits')
        raw=self.query(1 if snapshot else 2,nonce+struct.pack('<6I',1,period_ms,duration_ms,
            maximum,int(allow_disconnect),0))
        result=self.check_status(raw);demand(result['nonce']==nonce.hex(),'arm nonce differs')
        return result

    def find(self,nonce):
        self.ensure('configuration_snapshot')
        demand(len(nonce)==16 and any(nonce),'arm nonce')
        result=self.check_status(self.query(7,nonce));demand(result['nonce']==nonce.hex(),'find nonce differs')
        return result

    def status(self,ticket):
        self.ensure('configuration_snapshot');return self.check_status(self.query(3,ticket_bytes(ticket)),ticket)

    def stop(self,ticket):
        self.ensure('configuration_snapshot');return self.check_status(self.query(5,ticket_bytes(ticket)),ticket)

    def release(self,ticket):
        self.ensure('configuration_snapshot')
        demand(self.query(6,ticket_bytes(ticket))==b'','release schema')

    def export(self,ticket,path):
        status=self.status(ticket)
        demand(status['phase_code']>=4,'capture must be stopped before export')
        digest=hashlib.sha256();offset=0;raw=bytearray()
        with Path(path).open('xb') as stream:
            while offset<status['bytes']:
                count=min(1008,status['bytes']-offset)
                block=self.query(4,ticket_bytes(ticket)+struct.pack('<2I',offset,count))
                demand(len(block)==count,'short saved-copy chunk')
                stream.write(block);digest.update(block);raw.extend(block);offset+=count
        demand(digest.hexdigest()==status['sha256'],'saved-copy digest differs; partial output retained')
        after=self.status(ticket)
        demand((after['bytes'],after['sha256'],after['phase'])==
               (status['bytes'],status['sha256'],status['phase']),'frozen copy changed')
        return dict(status=status,records=records_decode(raw,status),output=str(Path(path).resolve()))

    def probe64(self):
        self.ensure('video_pool_read');debug=RtosDebug(self.client)
        anchors=[SEGMENTS[i] for i in (3,4,5,0,2)]
        def snapshot():
            return {name:debug.read(address,count) for name,address,count in anchors}
        before_time=time.monotonic_ns();before=snapshot();v=before['video'];p=before['pool'];m=before['manager']
        demand(struct.unpack_from('<IIIIQ',p)==(512,0x40c86000,STRIDE,120,POOL_START) and
               struct.unpack_from('<2I',p,24)==(1,7),'current pool differs')
        demand(struct.unpack_from('<I',m)[0]==1 and struct.unpack_from('<Q',m,16)[0]==0x41239374,
               'pool manager differs')
        def u(b,offset,size=4):return int.from_bytes(b[offset:offset+size],'little')
        demand(u(v,0)==10 and u(v,8)==4 and u(v,0x30,8)==0 and u(v,0x50,8)==0 and
               u(v,0x70)==3 and u(v,0x74)==5 and struct.unpack_from('<2H',v,0x78)==(4040,2276),
               'video cache outside verified layout')
        demand(u(before['sensor'],0)==10 and u(before['sensor'],0x164)==0,
               'sensor context outside verified mode10/front-end-source0 template')
        address=u(v,0x80,8);item,offset=divmod(address-POOL_START,STRIDE)
        demand(POOL_START<=address<=POOL_END-64 and offset==0x400 and 0<=item<=116 and
               struct.unpack_from('<4Q',v,0x90)==tuple(POOL_START+(item+i)*STRIDE+0x400+0x400000000 for i in range(4)),
               'current video addresses/aliases differ')
        for at in range(address&~4095,((address+63)&~4095)+1,4096):
            translated=debug.translate(at)
            demand(not translated['fault'] and translated['attribute'] in (0xff,0x44,0xbb) and
                   int(translated['physical'],16)==at,'video pool translation differs')
        data=debug.read(address,64);after=snapshot();after_time=time.monotonic_ns()
        fields=(('pool',0,32),('video',0,16),('video',0x70,0x40),('manager',0,4),
                ('manager',0x10,8),('sensor',0,4),('sensor',0x10,0x28),('sensor',0x148,0x20),('request-kind',0,4))
        return dict(purpose='interface-probe',address=hex(address),bytes=64,data_hex=data.hex(),
            before={k:b.hex() for k,b in before.items()},after={k:b.hex() for k,b in after.items()},
            context_equal_before_after=all(before[k][o:o+n]==after[k][o:o+n] for k,o,n in fields),
            begin_host_monotonic_ns=before_time,end_host_monotonic_ns=after_time,
            source_atomic=False,recording_active_proved=False,frame_association_proved=False,
            buffer_generation_proved=False,format_decoded=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=('describe','probe64','snapshot','arm','find','status','stop','read','release'))
    parser.add_argument('--ticket',type=Path);parser.add_argument('--output',type=Path)
    parser.add_argument('--nonce');parser.add_argument('--expected-build')
    parser.add_argument('--period-ms',type=int,default=1000)
    parser.add_argument('--duration-ms',type=int,default=30000)
    parser.add_argument('--maximum',type=int,default=32)
    parser.add_argument('--allow-disconnect',action='store_true')
    args=parser.parse_args()
    if args.command in ('arm','snapshot','find','probe64','read') and args.output is None:
        parser.error('new --output required; arm nonce is retained before transmission')
    if args.output and (args.output.exists() or args.output.is_symlink()):parser.error('--output already exists')
    if args.command in ('status','stop','read','release') and args.ticket is None:parser.error('--ticket required')
    if args.command=='find' and not args.nonce:parser.error('find requires retained --nonce')
    try:nonce=bytes.fromhex(args.nonce) if args.nonce else uuid.uuid4().bytes
    except ValueError:parser.error('--nonce must be 32 hex digits')
    if len(nonce)!=16 or not any(nonce):parser.error('nonzero 16-byte --nonce')
    ticket=json.loads(args.ticket.read_text()) if args.ticket else None
    from ptp_client import Client
    from ptp_transport import open_transport
    with open_transport() as transport:
        client=Client(transport);client.hello()
        observer=Observation(client,args.expected_build or (ticket or {}).get('build_identity'))
        description=observer.describe()
        if args.command=='describe':result=description
        elif args.command in ('arm','snapshot'):
            with args.output.open('x',encoding='utf-8') as stream:
                json.dump(dict(nonce=nonce.hex(),state='submitting',description=description),stream,indent=2)
            result=observer.arm(nonce,period_ms=args.period_ms,duration_ms=args.duration_ms,
                maximum=1 if args.command=='snapshot' else args.maximum,
                allow_disconnect=args.allow_disconnect,snapshot=args.command=='snapshot')
            replace_owned_receipt(args.output,result)
        elif args.command=='find':result=observer.find(nonce)
        elif args.command=='probe64':result=observer.probe64()
        elif args.command=='read':result=observer.export(ticket,args.output)
        elif args.command=='release':observer.release(ticket);result=dict(released=True,capture_id=ticket['capture_id'])
        else:result=getattr(observer,args.command)(ticket)
        if args.output and args.command in ('find','probe64'):
            with args.output.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
        print(json.dumps(result,indent=2))


if __name__=='__main__':main()
