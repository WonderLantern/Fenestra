"""Fixed native metadata/REC observations. Import never opens a camera.

No fallback to native-call, arbitrary hooks, MMIO or active sensor control.
An observed task/region token does not supply an exposure or pixel frame ID.
"""
import argparse,hashlib,json,struct,uuid
from pathlib import Path
from ptp_observe import Observation,demand,BASE,PACKAGE,PHASES,ticket_bytes,replace_owned_receipt
from ptp_client import BackendError
from c110_event_sites import TEMPLATES,POINTS,MASK
REGISTERS=[0,1,2,3,8,9,10,11,19,20,21,22,23,24,25,26]
SITES={pc:(t,e,k,depth) for pc,t,e,k,depth in POINTS}
SITES.update({0x413522a8:(13,3,3,0x80),0x413522ac:(13,4,3,0x80)})


def status_decode(raw):
    demand(len(raw)==192 and raw[:12]==struct.pack('<4sII',b'FXEC',1,192),'event status schema')
    phase=struct.unpack_from('<I',raw,12)[0];capture,boot,activation=struct.unpack_from('<3Q',raw,16)
    mask,flags,duration,maximum,records,scopes,used,budget,reason,error=struct.unpack_from('<10I',raw,56)
    begin,end,hz=struct.unpack_from('<3Q',raw,96);dropped,orphans=struct.unpack_from('<2I',raw,120)
    demand(phase in range(1,8) and capture and boot and activation and mask and not mask&~MASK and flags<=7 and
           (not flags&4 or flags&2) and (not flags&2 or mask&(1<<15)) and
           1<=duration<=30000 and 1<=maximum<=256 and records<=maximum and scopes<=64 and
           used==records*512 and budget==131072 and hz and reason<=7 and error<=6,'event status bounds')
    demand(phase<4 or end>=begin,'event freeze clock')
    return dict(schema='FXEC/1',capture_id=capture,boot_epoch=boot,activation_epoch=activation,
        nonce=raw[40:56].hex(),phase=PHASES[phase],phase_code=phase,template_mask=mask,
        templates=[name for t,name in TEMPLATES.items() if mask&(1<<t)],allow_disconnect=bool(flags&1),
        wait_for_rec_start=bool(flags&2),recording_start_observed=bool(flags&4),
        duration_ms=duration,maximum_records=maximum,records=records,open_scopes=scopes,bytes=used,budget=budget,
        reason=reason,error=error,begin_ticks=begin,end_ticks=end,frequency=hz,dropped=dropped,orphaned_scopes=orphans,
        sha256=raw[128:160].hex(),build_identity=raw[160:192].hex(),software_only=True,
        source_atomic=False,frame_association_proved=False,buffer_generation_proved=False)


def records_decode(data,ticket):
    demand(len(data)==ticket['bytes']==ticket['records']*512,'event export length')
    records=[];groups={}
    for seq in range(ticket['records']):
        r=data[seq*512:(seq+1)*512]
        demand(r[:12]==struct.pack('<4sII',b'FXER',1,512) and struct.unpack_from('<I',r,12)[0]==seq and
               r[16:40]==ticket_bytes(ticket) and r[160:192].hex()==ticket['build_identity'],'event record identity')
        begin,end,hz,token,parent,thread,pc,sp,kernel,cpu,entry_sp=struct.unpack_from('<11Q',r,40)
        t,event,flags,n,association,reads,kind,reserved=struct.unpack_from('<8I',r,128)
        binding=SITES.get(pc)
        demand(binding is not None and binding[:3]==(t,event,kind) and t in TEMPLATES and
               ticket['template_mask']&(1<<t) and begin<=end and hz==ticket['frequency'] and
               flags&32 and not flags&~255 and n<=192 and not reserved and sp%16==0,'event site/provenance differs')
        demand(not flags&128 or (t,event,pc)==(15,10,0x40f73370) and token and not flags&0x4f,
               'recording trigger lacks original scoped state transition')
        demand((not token and flags&2) or (token>>32 and thread and entry_sp and not flags&6),'event invocation proof differs')
        demand(not parent or token,'parent without native invocation')
        demand(not any(r[320+n:]),'event unused data differs')
        gp={str(reg):value for reg,value in zip(REGISTERS,struct.unpack_from('<16Q',r,192))}
        segments=[];at=320
        while at<320+n:
            demand(at+16<=320+n,'event segment header truncated')
            address,length,copied,label,error=struct.unpack_from('<Q4H',r,at);at+=16
            demand(length and at+length<=320+n and copied in (0,length) and label and error<=1 and
                   bool(error)==(copied==0),'event segment bounds')
            raw=r[at:at+length];at+=length
            demand(copied or not any(raw),'failed event segment contains stale bytes')
            segments.append(dict(address=hex(address),bytes=length,copied=copied,label=label,error=error,
                raw_hex=raw.hex(),source='guarded-RAM-snapshot' if address else 'saved-native-context',source_atomic=False))
        demand(at==320+n,'event segment length')
        row=dict(sequence=seq,template_id=t,template=TEMPLATES[t],event=event,kind=kind,
            begin_ticks=begin,end_ticks=end,frequency=hz,token=token or None,parent_token=parent or None,
            kernel_task_object=hex(thread) if thread else None,pc=hex(pc),native_sp=hex(sp),entry_sp=hex(entry_sp),
            kernel_context_raw=hex(kernel),mpidr_el1_raw=hex(cpu),flags=flags,association_epoch=association,
            source_reads=reads,registers=gp,segments=segments,call_identity_proved=bool(token),
            task_object_generation_proved=False,frame_association_proved=False,source_atomic=False)
        records.append(row)
        if token:groups.setdefault((token,association,t),[]).append(row)
    invocations=[]
    for (token,association,t),rows in groups.items():
        begin=[r for r in rows if r['kind']==1];end=[r for r in rows if r['kind']==2]
        complete=len(begin)==len(end)==1 and begin[0]['sequence']==rows[0]['sequence'] and end[0]['sequence']==rows[-1]['sequence']
        clean=not any(r['flags']&0x47 for r in rows)
        invocations.append(dict(token=token,template=TEMPLATES[t],association_epoch=association,
            entry_and_exit_observed=complete,local_inputs_valid=clean,
            computation_complete=complete and clean and t==1 and {1,2,4}<={r['event'] for r in rows} and
                (not rows[[r['event'] for r in rows].index(2)]['registers']['0']&1 or any(r['event']==3 for r in rows)),
            same_frame_proved=False,sequences=[r['sequence'] for r in rows]))
    rec_events=recording_events(records)
    demand(all(any(e['sequence']==r['sequence'] and e['kind']=='recording-start-accepted' for e in rec_events)
               for r in records if r['flags']&128),'recording trigger transition differs')
    return dict(records=records,invocations=invocations,rec_events=rec_events,
                auxiliary_replay=auxiliary_replay(records,invocations),
                raw_movie_inputs=raw_movie_inputs(records,invocations),
                pixel_boundaries=pixel_boundaries(records,invocations),
                receiver_notifications=[dict(sequence=r['sequence'],native_notification_id=hex(r['registers']['0']&0xffffffff),
                    native_notification_bits=hex(r['registers']['1']),call_token=r['token'],
                    context_known=not bool(r['flags']&4),hardware_bank=None,physical_frame_id=None)
                    for r in records if r['template_id']==24 and r['event']==1])


def pixel_boundaries(records,invocations):
    """Expose native bank/format/ownership observations without issuing leases.

    Only complete clean synchronous invocations are summarized. Hardware
    notifications in interrupt context remain unassociated raw records.
    Never join asynchronous frames by timestamp, address or queue order.
    """
    result=[]
    names={TEMPLATES[i] for i in range(20,26)}
    def blob(row,label,size):
        parts=[s for s in row['segments'] if s['label']==label and s['bytes']==size and s['copied']==size and not s['error']]
        return bytes.fromhex(parts[0]['raw_hex']) if len(parts)==1 else None
    for inv in invocations:
        if inv['template'] not in names:continue
        rows=[records[i] for i in inv['sequences']]
        clean=inv['entry_and_exit_observed'] and inv['local_inputs_valid'] and not any(r['flags']&8 for r in rows)
        item=dict(template=inv['template'],token=inv['token'],association_epoch=inv['association_epoch'],
            complete_native_call=clean,source_atomic=False,hardware_completion_proved=False,
            buffer_lease_proved=False,buffer_generation_proved=False,pixel_packing_proved=False,
            cfa_proved=False,same_integration_proved=False)
        result.append(item)
        if not clean:continue
        entry=rows[0];end=rows[-1]
        if inv['template'] in ('pool-retire','pool-recycle'):
            item['software_transition_observed']=False
            item['native_return']=end['registers']['0']&0xffffffff
            item['manager']=hex(entry['registers']['2' if inv['template']=='pool-retire' else '1'])
            item['requested_resource_base']=hex(entry['registers']['0'])
            transitions=[r for r in rows if r['event']==2]
            if len(transitions)!=1:continue
            row=transitions[0];wrapper=blob(row,11,40);token=blob(row,12,16)
            if wrapper is None or token is None:continue
            base,capacity=struct.unpack('<2Q',token)
            item.update(native_wrapper=hex(row['registers']['0']),native_slot_index=struct.unpack_from('<I',wrapper,24)[0],
                native_resource_base=hex(base),native_capacity=capacity,
                wrapper_token_consistent=struct.unpack_from('<Q',wrapper,32)[0]==base,
                software_transition_observed=item['native_return']==1 and base==entry['registers']['0'])
        elif inv['template']=='raw-bank-cache':
            item['native_return']=end['registers']['0']&0xffffffff
            selections=[]
            for row in rows:
                if row['event'] not in (2,5):continue
                wrapper=blob(row,11,40);token=blob(row,12,16);desc=blob(row,4,72)
                if wrapper is None or token is None or desc is None:continue
                selections.append(dict(sequence=row['sequence'],native_wrapper=hex(row['registers']['0']),
                    manager=hex(row['registers']['20']),native_slot_index=struct.unpack_from('<I',wrapper,24)[0],
                    native_resource_base=hex(struct.unpack_from('<Q',token)[0]),
                    native_capacity=struct.unpack_from('<Q',token,8)[0],native_descriptor_hex=desc.hex()))
            item['native_pool_selections']=selections
            published=[blob(r,13,168) for r in rows if r['event']==3]
            if len(published)==1 and published[0] is not None:
                cache=published[0];count=struct.unpack_from('<I',cache)[0]
                item['native_address_count']=count
                # Native routine only has storage for four addresses.
                item['native_device_addresses']=[hex(a) for a in struct.unpack_from('<4Q',cache,136)[:min(count,4)]]
                item['native_count_in_bounds']=count<=4
                item['native_image_descriptor_hex']=cache[104:136].hex()
        elif inv['template']=='raw-format-descriptor':
            values=[r for r in rows if r['event']==2]
            if len(values)!=1:continue
            row=values[0];desc=blob(row,4,72);token=blob(row,12,16);config=blob(row,14,48)
            if desc is None or token is None or config is None:continue
            item.update(native_descriptor_address=hex(row['registers']['19']),native_descriptor_hex=desc.hex(),
                native_resource_base=hex(struct.unpack_from('<Q',token)[0]),native_capacity=struct.unpack_from('<Q',token,8)[0],
                native_data_pointer=hex(struct.unpack_from('<Q',desc,64)[0]),native_type=struct.unpack_from('<I',desc,48)[0],
                native_format_id=struct.unpack_from('<I',desc,52)[0],
                native_width=struct.unpack_from('<H',desc,56)[0],native_height=struct.unpack_from('<H',desc,58)[0],
                native_source_format=struct.unpack_from('<I',config,12)[0],native_source_config_hex=config.hex())
        elif inv['template']=='receiver-bank-program':
            item['native_bank_selector']=entry['registers']['0']&0xffffffff
            item['native_active_selector']=entry['registers']['1']&0xffffffff
            item['native_register_bank']=end['registers']['0']&0xffffffff
            item['native_pointer_groups']={}
            pointers=blob(end,15,24)
            if pointers is None:continue
            for index,pointer in enumerate(struct.unpack('<3Q',pointers)):
                values=blob(end,16+index,32)
                item['native_pointer_groups'][str(index)]=None if not pointer else (
                    [hex(a) for a in struct.unpack('<4Q',values)] if values is not None else 'unreadable')
        elif inv['template']=='receiver-notification':
            item['native_notification_id']=hex(entry['registers']['0']&0xffffffff)
            item['native_notification_bits']=hex(entry['registers']['1'])
    return result


def raw_movie_inputs(records,invocations):
    """Decode consumed SOFTWARE descriptors, never follow pixel pointers.

    Only observed synchronous nesting connects a producer to its caller.
    Reused job/descriptor addresses and close timestamps do not connect
    separate tasks or establish sensor/DMA/lease/exposure identity.
    """
    result=[]
    def blob(row,label,size):
        parts=[s for s in row['segments'] if s['label']==label and s['copied']==size]
        return bytes.fromhex(parts[0]['raw_hex']) if len(parts)==1 else None
    for inv in invocations:
        if inv['template']!='raw-movie-input':continue
        rows=[records[i] for i in inv['sequences']]
        out=dict(token=inv['token'],descriptor_complete=False,native_queue_copies=None,
                 frame_completion_proved=False,buffer_lease_proved=False,pixel_format_proved=False,
                 same_integration_proved=False,reason='incomplete or invalid native input invocation')
        result.append(out)
        if not inv['entry_and_exit_observed'] or not inv['local_inputs_valid']:continue
        branches=[r for r in rows if r['event'] in (2,3)]
        handoffs=[r for r in rows if r['event']==4]
        if len(branches)!=1 or len(handoffs)!=1:continue
        branch=branches[0];handoff=handoffs[0];raw=branch['event']==2
        payload=blob(handoff,6 if raw else 7,32)
        outer=blob(handoff,5,40);job=blob(handoff,2,24);descriptor=blob(handoff,3,8)
        if any(p is None for p in (payload,outer,job,descriptor)):continue
        count,repeat=struct.unpack_from('<2I',outer,32)
        out.update(descriptor_complete=True,branch='native-raw' if raw else 'native-image',
                   handoff_sequence=handoff['sequence'],native_job_header_hex=job.hex(),
                   native_descriptor_address=hex(struct.unpack('<Q',descriptor)[0]),
                   native_input_count=count,native_repeat_minus_one=repeat,
                   source_atomic=False,reason='actual original consumer input; physical identity remains unknown')
        if raw:
            base,parameter,data,format_id=struct.unpack('<QI4xQI4x',payload)
            out.update(native_resource_base=hex(base),native_parameter=parameter,
                       native_data_pointer=hex(data),native_format_selector=format_id)
        else:
            stride,width,height=struct.unpack_from('<3H',payload,8)
            out.update(native_row_stride=stride,native_width=width,native_height=height,
                       native_data_pointer=hex(struct.unpack_from('<Q',payload,24)[0]))
        # Queue records are copies of consumed descriptors, not pixel leases.
        copies=[]
        for child in invocations:
            if child['template']!='raw-movie-queue' or child['association_epoch']!=inv['association_epoch'] or not (
                    child['entry_and_exit_observed'] and child['local_inputs_valid']):continue
            rr=[records[i] for i in child['sequences']]
            if not rr or any(r['parent_token']!=inv['token'] for r in rr):continue
            pending=None
            for row in rr:
                if row['event']==2:pending=(row,blob(row,10,168))
                elif row['event']==3 and pending:
                    before,item=pending;pending=None
                    if item is not None and item[72 if raw else 40:104 if raw else 72]==payload:
                        copies.append(dict(sequence=before['sequence'],return_sequence=row['sequence'],
                            native_queue_accepted=bool(row['registers']['0']&1),token=child['token']))
        if copies:out['native_queue_copies']=copies
    return result


def auxiliary_replay(records,invocations):
    """Replay recovered integer operations using the SAME observed call only.

    Unknown/missing branches stay unknown. exp2/normalization is not guessed:
    its actual native return is required unless a captured default applies.
    This checks software arithmetic, never physical gain or frame identity.
    """
    result=[]
    for invocation in invocations:
        if invocation['template']!='auxiliary-ratio':continue
        rows=[records[i] for i in invocation['sequences']];by={r['event']:r for r in rows}
        row=dict(token=invocation['token'],same_frame_proved=False,complete_inputs=False,
                 arithmetic_consistent=None,checks={},reason='incomplete or invalid observed calculation')
        result.append(row)
        if not invocation['computation_complete']:continue
        def blob(event,label,size):
            s=next((s for s in by.get(event,{}).get('segments',[]) if s['label']==label and s['copied']==size),None)
            return bytes.fromhex(s['raw_hex']) if s else None
        def w(event,reg):return by[event]['registers'][str(reg)]&0xffffffff
        checks=row['checks'];expected_p=None;expected_f=None;expected_coefficients=None
        history_valid=bool(w(2,0)&1);row['history_valid']=history_valid
        if 6 in by:expected_p=expected_f=256;row['branch']='forced-default'
        elif not history_valid:expected_p=256;row['branch']='invalid-history-default'
        else:
            history=blob(3,1,8)
            if history:
                a,b=struct.unpack('<2I',history);n=w(3,0);row.update(history_a=a,history_b=b,n=n,policy=w(3,20),limit=w(3,19))
                if not a or not b:initial=256;row['branch']='zero-history-default'
                else:
                    initial=((n<<8)&0xffffffff)//a
                    if 8 not in by:initial=min(initial,((w(3,24)<<8)&0xffffffff)//b)
                    row['branch']='extra-path' if 8 in by else 'two-history-ratios'
                cap={2:2048,3:4096}.get(w(3,20),w(3,19))
                expected_p=min(max(initial&65535,256),cap)
        if expected_f is None:
            if 10 in by:expected_f=w(10,0)
            elif 9 in by and struct.unpack('<i',struct.pack('<I',w(9,8)))[0]<2246:expected_f=256
        if expected_p is not None:
            checks['p']=dict(expected=expected_p,observed=w(4,19),equal=expected_p==w(4,19))
        if expected_f is not None:
            checks['f']=dict(expected=expected_f,observed=w(4,25),equal=expected_f==w(4,25))
        default=blob(6,7,12) or blob(7,7,12)
        if default is not None:expected_coefficients=list(struct.unpack('<3I',default))
        elif expected_p is not None and expected_f is not None:
            k=(((expected_f&65535)*expected_p)&0xffffffff)>>8;k&=65535
            if k>=257:expected_coefficients=[k>>2]*3
        actual=blob(4,3,12)
        if expected_coefficients is not None and actual is not None:
            observed=list(struct.unpack('<3I',actual));checks['coefficients']=dict(expected=expected_coefficients,
                observed=observed,equal=expected_coefficients==observed)
        row['complete_inputs']=set(checks)=={'p','f','coefficients'}
        row['arithmetic_consistent']=all(c['equal'] for c in checks.values()) if row['complete_inputs'] else None
        row['reason']='same-call integer replay' if row['complete_inputs'] else 'required branch/exp-helper/default-coefficient event missing'
    return result


def recording_events(records):
    result=[]
    for row in records:
        if row['template_id']!=15 or row['event']!=10 or not row['call_identity_proved'] or row['flags']&7:continue
        segment=next((s for s in row['segments'] if s['label']==10 and s['copied']==16),None)
        if not segment:continue
        lr,old=struct.unpack('<2Q',bytes.fromhex(segment['raw_hex']));new=row['registers']['1']&0xffffffff
        kind=None
        if lr==0x40f72f80 and old==5 and new==6:kind='recording-start-accepted'
        elif lr==0x40f72ff0 and old==7 and new==0:kind='recording-stop-accepted'
        elif lr==0x40f72f68 and old==5 and new==0:kind='recording-start-failed'
        elif lr==0x40f72fd4 and old==7 and new==6:kind='recording-stop-failed-still-active'
        if kind:result.append(dict(kind=kind,sequence=row['sequence'],token=row['token'],
            ticks=row['begin_ticks'],old_native_state=old,new_native_state=new,
            evidence='actual original callback -> post-STR setter site + preserved native LR',
            hardware_sensor_ack_proved=False,pixel_frame_association_proved=False))
    return result


class EventTrace:
    def __init__(self,client,expected_build=None):self.observer=Observation(client,expected_build);self.description=None
    def query(self,op,data=b''):
        if op:
            d=self.description or self.describe();demand(d['available'],'fixed native event trace unavailable')
        return self.observer.query(10,struct.pack('<I',op)+data)
    def describe(self):
        base=self.observer.describe()
        if not base['available']['software_submit_trace']:
            self.description=dict(available=False,reason='core lacks passive observation extension');return self.description
        try:r=self.query(0)
        except BackendError as error:
            if error.code!=5:raise
            self.description=dict(available=False,reason='core has no fixed native event service');return self.description
        demand(len(r)==224 and r[:12]==struct.pack('<4sII',b'FXED',1,224),'event DESCRIBE schema')
        demand(struct.unpack_from('<7I',r,12)==(7,512,131072,30000,256,192,64),'event limits differ')
        supported=struct.unpack_from('<Q',r,40)[0]
        demand(supported and not supported&~MASK,'unknown event templates')
        demand(struct.unpack_from('<2Q',r,48)==(base['boot_epoch'],base['activation_epoch']) and
               r[64:96].hex()==BASE and r[96:128].hex()==base['build_identity'] and r[128:160].hex()==PACKAGE and
               struct.unpack_from('<2I',r,160)==(1,1) and not any(r[168:]),'event identity/policy differs')
        self.description=dict(available=True,schema='FXED/1',build_identity=base['build_identity'],
            boot_epoch=base['boot_epoch'],activation_epoch=base['activation_epoch'],
            templates={k:v for k,v in TEMPLATES.items() if supported&(1<<k)},
            template_mask=supported,maximum_duration_ms=30000,maximum_records=256,budget_bytes=131072,
            record_bytes=512,payload_bytes=192,maximum_scopes=64,source_atomic=False,
            original_rec_callbacks=bool(supported&(1<<15)),native_raw_input_trace=bool(supported&(1<<17)),
            native_pixel_boundary_trace=(supported&sum(1<<k for k in range(20,26)))==sum(1<<k for k in range(20,26)),
            wait_for_original_rec_start=True,physical_qualification=False,
            frame_completion_and_lease=False,roi_sample=False)
        return self.description
    def checked(self,raw,ticket=None):
        row=status_decode(raw);d=self.description or self.describe()
        demand(d['available'] and all(row[k]==d[k] for k in ('build_identity','boot_epoch','activation_epoch')),'event build/epoch differs')
        demand(not row['template_mask']&~d['template_mask'],'capture has unadvertised event templates')
        if ticket is not None:demand(ticket_bytes(row)==ticket_bytes(ticket) and row['nonce']==ticket['nonce'] and row['template_mask']==ticket['template_mask'],'different event capture')
        return row
    def key(self,ticket):
        d=self.description or self.describe();demand(d['available'] and all(ticket[k]==d[k] for k in ('build_identity','boot_epoch','activation_epoch')),'event ticket belongs to another build/epoch')
        return ticket_bytes(ticket)
    def arm(self,nonce,duration_ms=30000,maximum=256,mask=None,allow_disconnect=False,wait_for_rec_start=False):
        d=self.description or self.describe();demand(d['available'],'fixed native event trace unavailable')
        supported=d.get('template_mask',MASK)
        if mask is None:mask=supported
        demand(isinstance(nonce,bytes) and len(nonce)==16 and any(nonce) and 1<=duration_ms<=30000 and 1<=maximum<=256 and mask and not mask&~supported,'event ARM arguments')
        demand(not wait_for_rec_start or mask&(1<<15),'REC trigger requires recording-callback template')
        flags=int(allow_disconnect)|(int(wait_for_rec_start)<<1)
        row=self.checked(self.query(1,nonce+struct.pack('<4IQ',duration_ms,maximum,flags,0,mask)))
        demand(row['nonce']==nonce.hex() and row['duration_ms']==duration_ms and row['maximum_records']==maximum and row['template_mask']==mask and row['allow_disconnect']==allow_disconnect and row['wait_for_rec_start']==wait_for_rec_start,'event ARM replay/configuration differs')
        return row
    def find(self,nonce):
        demand(isinstance(nonce,bytes) and len(nonce)==16 and any(nonce),'event FIND nonce');return self.checked(self.query(6,nonce))
    def status(self,ticket):return self.checked(self.query(2,self.key(ticket)),ticket)
    def stop(self,ticket):return self.checked(self.query(4,self.key(ticket)),ticket)
    def release(self,ticket):self.status(ticket);demand(self.query(5,self.key(ticket))==b'','event RELEASE reply')
    def export(self,ticket,path):
        sidecar=path.with_name(path.name+'.json');demand(not any(p.exists() or p.is_symlink() for p in (path,sidecar)),'event export already exists')
        row=self.status(ticket);demand(row['phase_code']>=4,'event capture is not frozen');raw=bytearray()
        for offset in range(0,row['bytes'],1008):
            n=min(1008,row['bytes']-offset);part=self.query(3,self.key(row)+struct.pack('<2I',offset,n));demand(len(part)==n,'short event chunk');raw.extend(part)
        demand(hashlib.sha256(raw).hexdigest()==row['sha256'],'event digest differs')
        decoded=records_decode(raw,row);demand(self.status(row)==row,'frozen event status changed')
        with path.open('xb') as stream:stream.write(raw)
        result=dict(ticket=row,**decoded,file=str(path),verified_sha256=row['sha256'])
        with sidecar.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
        return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=('describe','arm','find','status','stop','read','release'))
    p.add_argument('--ticket',type=Path);p.add_argument('--output',type=Path);p.add_argument('--nonce');p.add_argument('--expected-build')
    p.add_argument('--duration-ms',type=int,default=30000);p.add_argument('--maximum',type=int,default=256)
    p.add_argument('--templates',default='all',help='all, pixel, or comma-separated template names');p.add_argument('--allow-disconnect',action='store_true')
    p.add_argument('--wait-for-rec-start',action='store_true');a=p.parse_args()
    if a.command in ('arm','find','read') and not a.output:p.error('new --output required')
    if a.output and (a.output.exists() or a.output.is_symlink()):p.error('output already exists')
    if a.command in ('status','stop','read','release') and not a.ticket:p.error('--ticket required')
    if a.command=='find' and not a.nonce:p.error('--nonce required for uncertain ARM recovery')
    nonce=bytes.fromhex(a.nonce) if a.nonce else uuid.uuid4().bytes
    if len(nonce)!=16 or not any(nonce):p.error('nonzero 16-byte nonce required')
    names={v:k for k,v in TEMPLATES.items()};chosen=a.templates.split(',')
    if a.templates not in ('all','pixel') and any(n not in names for n in chosen):p.error('unknown template; use describe')
    mask=None if a.templates=='all' else sum(1<<k for k in (15,17,18,19,20,21,22,23,24,25)) if a.templates=='pixel' else sum(1<<names[n] for n in set(chosen))
    if a.wait_for_rec_start and mask is not None and not mask&(1<<15):p.error('--wait-for-rec-start requires recording-callback in --templates')
    ticket=json.loads(a.ticket.read_text()) if a.ticket else None
    from ptp_client import Client
    from ptp_transport import open_transport
    with open_transport() as transport:
        client=Client(transport);client.hello();trace=EventTrace(client,a.expected_build or (ticket or {}).get('build_identity'));d=trace.describe()
        if a.command=='describe':result=d
        elif a.command=='arm':
            demand(d['available'],'fixed native event trace unavailable')
            with a.output.open('x',encoding='utf-8') as f:json.dump(dict(nonce=nonce.hex(),state='submitting',description=d),f,indent=2)
            result=trace.arm(nonce,a.duration_ms,a.maximum,mask,a.allow_disconnect,a.wait_for_rec_start);replace_owned_receipt(a.output,result)
        elif a.command=='find':
            result=trace.find(nonce)
            with a.output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2)
        elif a.command=='read':result=trace.export(ticket,a.output)
        elif a.command=='release':trace.release(ticket);result=dict(released=True,capture_id=ticket['capture_id'])
        else:result=getattr(trace,a.command)(ticket)
        print(json.dumps(result,indent=2))


if __name__=='__main__':main()
