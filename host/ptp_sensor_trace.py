"""Bounded passive sensor software-submit trace. Import never opens a camera.

No sensor/native-call fallback exists. A trace is software input/return data,
not physical bus ACK, REC, frame identity, ADC precision or DGO activation.
"""
import argparse,hashlib,json,struct,uuid
from pathlib import Path
from ptp_observe import Observation,demand,ticket_bytes,replace_owned_receipt,PHASES,BASE,PACKAGE


def status_decode(raw):
    demand(len(raw)==192 and raw[:12]==struct.pack('<4sII',b'FXTC',1,192),'sensor trace status schema')
    phase=struct.unpack_from('<I',raw,12)[0];capture,boot,activation=struct.unpack_from('<3Q',raw,16)
    template,flags,duration,maximum,pairs,inflight,used,budget,reason,error=struct.unpack_from('<10I',raw,56)
    begin,end,hz=struct.unpack_from('<3Q',raw,96);dropped,invalid=struct.unpack_from('<2I',raw,120)
    demand(phase in range(1,8) and capture and boot and activation and template==1 and flags<=1 and
           1<=duration<=30000 and 1<=maximum<=128 and pairs<=maximum and inflight<=pairs and
           used==pairs*512 and budget==65536 and hz and reason<=7 and error<=6,'sensor trace bounds')
    demand(phase<4 or (not inflight and end>=begin),'sensor trace is not frozen')
    return dict(schema='FXTC/1',capture_id=capture,boot_epoch=boot,activation_epoch=activation,nonce=raw[40:56].hex(),
      phase=PHASES[phase],phase_code=phase,template=template,allow_disconnect=bool(flags),duration_ms=duration,
      maximum_pairs=maximum,pairs=pairs,inflight=inflight,bytes=used,budget=budget,reason=reason,error=error,
      begin_ticks=begin,end_ticks=end,frequency=hz,dropped=dropped,invalid_returns=invalid,
      sha256=raw[128:160].hex(),build_identity=raw[160:192].hex(),software_only=True,sensor_ack_proved=False,
      rec_proved=False,frame_lease_proved=False,parent_call_proved=False,thread_identity_proved=False)


def records_decode(raw,ticket):
    demand(len(raw)==ticket['bytes'] and len(raw)==ticket['pairs']*512,'sensor trace length')
    result=[]
    for pair in range(ticket['pairs']):
        rows=[]
        for kind in (1,2):
            sequence=pair*2+kind-1;r=raw[sequence*256:(sequence+1)*256]
            demand(r[:12]==struct.pack('<4sII',b'FXTR',1,256) and struct.unpack_from('<I',r,12)[0]==sequence,'trace record schema/sequence')
            token,parent,ticks,hz=struct.unpack_from('<4Q',r,16)
            demand(token and token>>32 and token&0xffffffff==pair+1 and not parent and hz==ticket['frequency'] and
                   r[48:72]==ticket_bytes(ticket) and r[224:256].hex()==ticket['build_identity'],'trace record identity')
            pc,stack,tls,cpu,*args=struct.unpack_from('<8Q',r,72)
            record_kind,transport,flags,total,copied,word_bits=struct.unpack_from('<6I',r,136)
            demand(record_kind==kind and transport==8 and word_bits==16 and flags&4 and not flags&~7 and
                   pc==(0x413522a8 if kind==1 else 0x413522ac) and stack%16==0 and
                   copied<=64 and total<=384 and copied<=total and copied%2==0 and total%2==0,'trace native schema')
            if kind==1:
                demand(args[1]==stack and (flags&2 or (total and copied==min(total,64))) and
                       bool(flags&1)==(total>copied),'trace source/truncation differs')
            else:demand((flags,total,copied)==(4,0,0),'return event contains fabricated payload')
            demand(not any(r[160+copied:224]),'trace unused payload differs')
            data=r[160:160+copied]
            rows.append(dict(sequence=sequence,token=token,parent_token=None,ticks=ticks,pc=hex(pc),
              native_stack=hex(stack),tpidrro_el0_raw=hex(tls),mpidr_el1_raw=hex(cpu),native_args=args,
              event='software-submit' if kind==1 else 'software-return',source_error=bool(flags&2),truncated=bool(flags&1),
              native_tx_bytes=total,copied_bytes=copied,native_word_bits=16,payload_hex=data.hex(),
              native_tx_words=list(struct.unpack('<'+'H'*(len(data)//2),data)) if data else [],
              sensor_ack_proved=False,frame_lease_proved=False,thread_identity_proved=False))
        demand(rows[0]['token']==rows[1]['token'] and rows[0]['ticks']<=rows[1]['ticks'] and
               rows[0]['native_stack']==rows[1]['native_stack'],'unpaired software invocation')
        result.append(dict(token=rows[0]['token'],submit=rows[0],returned=rows[1],
                           software_return_u64=rows[1]['native_args'][0],parent_call_proved=False))
    return result


class SensorTrace:
    def __init__(self,client,expected_build=None):
        self.observer=Observation(client,expected_build);self.description=None

    def query(self,operation,data=b''):
        if operation and self.description is None:self.describe()
        return self.observer.query(8,struct.pack('<I',operation)+data)

    def describe(self):
        base=self.observer.ensure('software_submit_trace');r=self.query(0)
        demand(len(r)==224 and r[:12]==struct.pack('<4sII',b'FXTD',1,224),'sensor trace schema')
        demand(struct.unpack_from('<9I',r,12)==(7,256,65536,30000,128,64,16,1,0),'sensor trace limits')
        demand(struct.unpack_from('<2Q',r,48)==(base['boot_epoch'],base['activation_epoch']) and
               r[64:96].hex()==BASE and r[96:128].hex()==base['build_identity'] and r[128:160].hex()==PACKAGE,'trace target/epochs differ')
        demand(struct.unpack_from('<2I3Q5I',r,160)==(8,1,0x413522a8,0x413522ac,0xa057af9ec,1,0,0,0,0) and not any(r[212:]),'trace provenance policy differs')
        self.description=dict(schema='FXTD/1',build_identity=base['build_identity'],boot_epoch=base['boot_epoch'],
          activation_epoch=base['activation_epoch'],maximum_duration_ms=30000,maximum_pairs=128,
          budget_bytes=65536,record_bytes=256,payload_bytes=64,native_tx_word_bits=16,
          software_submit=True,software_return=True,same_call_token=True,parent_call_proved=False,
          thread_identity_proved=False,rec_proved=False,frame_lease_proved=False,sensor_ack_proved=False,
          transport_selector=8,internal_flash_persistence=False,reboot_loses_records=True)
        return self.description

    def checked(self,raw,ticket=None):
        row=status_decode(raw);d=self.description or self.describe()
        demand(all(row[k]==d[k] for k in ('build_identity','boot_epoch','activation_epoch')),'trace build/epochs differ')
        if ticket is not None:demand(ticket_bytes(row)==ticket_bytes(ticket) and row['nonce']==ticket['nonce'] and row['build_identity']==ticket['build_identity'],'different sensor capture')
        return row

    def arm(self,nonce,duration_ms=30000,maximum=128,allow_disconnect=False):
        self.description or self.describe()
        demand(isinstance(nonce,bytes) and len(nonce)==16 and any(nonce) and 1<=duration_ms<=30000 and 1<=maximum<=128,'trace ARM arguments')
        row=self.checked(self.query(1,nonce+struct.pack('<4I',duration_ms,maximum,int(allow_disconnect),0)))
        demand(row['nonce']==nonce.hex() and row['duration_ms']==duration_ms and row['maximum_pairs']==maximum and
               row['allow_disconnect']==allow_disconnect,'trace ARM echo differs')
        return row

    def find(self,nonce):return self.checked(self.query(6,nonce))
    def ticket(self,ticket):
        d=self.description or self.describe()
        demand(all(ticket[k]==d[k] for k in ('build_identity','boot_epoch','activation_epoch')),'trace ticket belongs to another build/epoch')
        return ticket_bytes(ticket)
    def status(self,ticket):return self.checked(self.query(2,self.ticket(ticket)),ticket)
    def stop(self,ticket):return self.checked(self.query(4,self.ticket(ticket)),ticket)
    def release(self,ticket):
        self.status(ticket);demand(self.query(5,ticket_bytes(ticket))==b'','trace release reply differs')

    def export(self,ticket,path):
        sidecar=path.with_name(path.name+'.json')
        demand(not any(p.exists() or p.is_symlink() for p in (path,sidecar)),'trace output or sidecar already exists')
        row=self.status(ticket);demand(row['phase_code']>=4 and not row['inflight'],'trace capture has not frozen')
        data=bytearray()
        for offset in range(0,row['bytes'],1008):
            n=min(1008,row['bytes']-offset);part=self.query(3,ticket_bytes(row)+struct.pack('<2I',offset,n))
            demand(len(part)==n,'short sensor trace chunk');data.extend(part)
        demand(hashlib.sha256(data).hexdigest()==row['sha256'],'sensor trace digest differs')
        records=records_decode(data,row);after=self.status(row)
        demand(all(after[k]==row[k] for k in ('sha256','bytes','pairs','phase','inflight')),'sensor trace changed during export')
        with path.open('xb') as stream:stream.write(data)
        result=dict(ticket=row,pairs=records,file=str(path),verified_sha256=row['sha256'])
        with sidecar.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
        return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=('describe','arm','find','status','stop','read','release'))
    p.add_argument('--ticket',type=Path);p.add_argument('--output',type=Path);p.add_argument('--nonce');p.add_argument('--expected-build')
    p.add_argument('--duration-ms',type=int,default=30000);p.add_argument('--maximum',type=int,default=128);p.add_argument('--allow-disconnect',action='store_true');a=p.parse_args()
    if a.command in ('arm','find','read') and not a.output:p.error('new --output required; ARM nonce must survive an uncertain response')
    if a.output and (a.output.exists() or a.output.is_symlink()):p.error('output already exists')
    if a.command in ('status','stop','read','release') and not a.ticket:p.error('--ticket required')
    if a.command=='find' and not a.nonce:p.error('FIND needs retained --nonce')
    nonce=bytes.fromhex(a.nonce) if a.nonce else uuid.uuid4().bytes
    if len(nonce)!=16 or not any(nonce):p.error('nonzero 16-byte nonce required')
    ticket=json.loads(a.ticket.read_text()) if a.ticket else None
    from ptp_client import Client
    from ptp_transport import open_transport
    with open_transport() as transport:
        client=Client(transport);client.hello();trace=SensorTrace(client,a.expected_build or (ticket or {}).get('build_identity'));trace.describe()
        if a.command=='describe':result=trace.description
        elif a.command=='arm':
            with a.output.open('x',encoding='utf-8') as f:json.dump(dict(nonce=nonce.hex(),state='submitting',description=trace.description),f,indent=2)
            result=trace.arm(nonce,a.duration_ms,a.maximum,a.allow_disconnect);replace_owned_receipt(a.output,result)
        elif a.command=='find':
            result=trace.find(nonce)
            with a.output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2)
        elif a.command=='read':result=trace.export(ticket,a.output)
        elif a.command=='release':trace.release(ticket);result=dict(released=True,capture_id=ticket['capture_id'])
        else:result=getattr(trace,a.command)(ticket)
        print(json.dumps(result,indent=2))


if __name__=='__main__':main()
