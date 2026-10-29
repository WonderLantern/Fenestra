"""Bounded saved copies of native RAW inputs; import never opens a camera.

Native packet bytes and packed-image rows retain their original layout.
No CFA, bit order, linear primary/auxiliary or integration ID is guessed.
"""
import argparse,hashlib,json,struct,uuid
from pathlib import Path
from ptp_observe import Observation,demand,ticket_bytes,PHASES,replace_owned_receipt
from ptp_client import BackendError

FAULT_STAGES=('unknown','rotate','bank-begin','bank-end','consumer','source-IRQ','completion',
    'REC-entry','REC-accepted','REC-return','worker','request','retain','copy','return','freeze','native-adapter')
FAULT_PREDICATES=dict(zip(
    (1,10,11,12,13,20,21,22,23,24,30,31,40,41,42,43,50,51,60,70,71,72,73,74,75,76,80,81,82,83,
     90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,255),
    ('metadata-lock-busy','allocation-range','allocation-overlap','allocation-slot-limit','held-generation-reused',
     'bank-writer-nested','bank-index-or-arguments','bank-return-without-writer','duplicate-bank-slot','bank-post-without-allocation',
     'source-notification-value','IRQ-sequence-overflow','completion-order-or-identity','completion-object',
     'completion-reuse-or-ambiguous-slot','completion-baseline','REC-nested','REC-with-inflight-bank','counter-frequency',
     'copy-geometry','copy-time-budget','copy-read-or-AT','copy-double-read-mismatch','copy-bank-epoch','copy-no-longer-eligible','copy-admission-revoked',
     'retain-result','return-result','return-budget','callback-drain-budget','IRQ-device-object','IRQ-return-context',
     'IRQ-return-boundary','IRQ-runtime-registration-or-status','IRQ-masked-notification','IRQ-nesting',
     'source-entry-object','source-task-SP-job','source-wait-result-read','source-descriptor-association',
     'native-bank-claim','rotation-metadata-read','consumer-metadata-read','native-FIFO-boundary','native-pool-reset',
     'retain-free-count','retain-original-lookup','retain-postconditions','return-token-mismatch','unclassified-source-loss')))
FAULT_WORDS=('phase','reason','error','generation','capture_id','boot_epoch','activation_epoch','irq_sequence','irq_consumed',
    'baseline_ready','baseline_sequence','baseline_skipped','known_bank_mask','tracked_slots','candidate_base','candidate_data',
    'candidate_capacity','candidate_generation','candidate_completion','job_identity','descriptor_identity','slot_generation','slot_pending',
    'native_hold','hold_base','held_ticks','returned_ticks','completed_ticks','consumed_ticks','temporary_used','lease_error','end_ticks')
FAULT_PREDICATES.update({109:'consumer-handoff-order',110:'consumer-handoff-full',111:'copy-native-exclusion-metadata',
                         112:'lifecycle-journal-full',113:'lifecycle-journal-order'})
COPY_BUSY_REASONS=('none','reserve-unspecified','controller-busy','pool-busy','metadata-lock-busy',
    'input-changed','not-eligible','admission-revoked','revoked-during-copy')

def copy_diagnostics_decode(raw,ticket):
    demand((len(raw)==224 and raw[:12]==struct.pack('<4sII',b'FXCD',1,224)) or
        (len(raw)==256 and raw[:12]==struct.pack('<4sII',b'FXCD',2,256)),'copy diagnostics schema')
    reason=struct.unpack_from('<I',raw,12)[0];attempts=struct.unpack_from('<Q',raw,88)[0]
    counts=struct.unpack_from('<9Q',raw,96);state,active,generation,result=struct.unpack_from('<4I',raw,184)
    demand(raw[16:40]==ticket_bytes(ticket) and raw[40:56].hex()==ticket['nonce'] and
        raw[56:88].hex()==ticket['build_identity'] and reason<len(COPY_BUSY_REASONS) and state<=3 and
        generation and result in (*range(14),0xffffffff) and sum(counts)<=attempts and not counts[0] and
        (not reason or result==5) and not any(raw[220:224]),'copy diagnostics bounds/identity')
    result_dict=dict(schema='FXCD/2' if len(raw)==256 else 'FXCD/1',copy_attempts=attempts,last_busy_reason=COPY_BUSY_REASONS[reason],
        busy_counts=dict(zip(COPY_BUSY_REASONS[1:],counts[1:])),selection_state=('empty','publishing','pending','chosen')[state],
        source_callbacks=active,source_version=struct.unpack_from('<Q',raw,168)[0],
        input_version=struct.unpack_from('<Q',raw,176)[0],generation=generation,
        copy_result=None if result==0xffffffff else result,copy_begin=struct.unpack_from('<Q',raw,200)[0],
        copy_end=struct.unpack_from('<Q',raw,208)[0],temporary_copy_bytes=struct.unpack_from('<I',raw,216)[0])
    if len(raw)==256:
        head,tail,drops,producers,poison=struct.unpack_from('<3Q2I',raw,224)
        demand(head<=tail and poison<=1,'journal diagnostic bounds')
        result_dict['journal']=dict(head=head,tail=tail,dropped=drops,publishing=producers,poisoned=bool(poison),
            capacity=32,individual_atomic_observations=True)
    return result_dict

def fault_decode(raw,ticket=None):
    demand(len(raw)==896 and raw[:12]==struct.pack('<4sII',b'FXFF',1,896),'first-fault schema')
    ready=struct.unpack_from('<I',raw,12)[0];capture,boot,activation=struct.unpack_from('<3Q',raw,16)
    generation,predicate,stage,flags=struct.unpack_from('<4I',raw,88)
    demand(ready in (0,2) and capture and boot and activation and generation and stage<len(FAULT_STAGES) and not flags&~31 and
        struct.unpack_from('<I',raw,280)[0]<=1 and struct.unpack_from('<I',raw,284)[0]<=4 and (ready==2 and predicate and struct.unpack_from('<Q',raw,272)[0] or
        ready==0 and not any(raw[92:768])),'first-fault bounds')
    if ticket is not None:demand(raw[16:40]==ticket_bytes(ticket) and raw[40:56].hex()==ticket['nonce'] and
        raw[56:88].hex()==ticket['build_identity'],'first-fault capture/epoch/build differs')
    current=struct.unpack_from('<8I',raw,768);live_id,token,held,returned=struct.unpack_from('<4Q',raw,800)
    demand(current[0] in range(8) and current[1]<=16384 and current[2]<=1 and current[4]<=1 and current[7]<=1 and
        live_id==capture,'first-fault cleanup bounds')
    def result(value):return None if value==0xffffffff else value
    owner_cpu,owner_task,owner_sp,owner_kernel=struct.unpack_from('<4Q',raw,176)
    owner_kind,owner_stage=struct.unpack_from('<2I',raw,208)
    requester_cpu,requester_task,requester_sp,requester_kernel=struct.unpack_from('<4Q',raw,128)
    kind,lock=struct.unpack_from('<2I',raw,160)
    demand(kind<=2 and owner_kind<=2 and owner_stage<len(FAULT_STAGES) and lock<=2,'first-fault context bounds')
    words=dict(zip(FAULT_WORDS,struct.unpack_from('<32Q',raw,288)))
    if ready==2 and flags&2:demand(words['generation']==generation and words['capture_id']==capture and
        words['boot_epoch']==boot and words['activation_epoch']==activation,'first-fault coherent snapshot epoch differs')
    cleanup=dict(phase=PHASES[current[0]] if current[0] else 'IDLE',bytes=current[1],lost=bool(current[2]),error=current[3],
        native_source_owned=bool(current[4]),lease_error=current[5],stop_reason=current[6],quiesced=bool(current[7]),
        native_hold=token,held_ticks=held,returned_ticks=returned)
    cleanup.update(zip(('retain_result','return_result','copy_result','temporary_copy_bytes'),
        [result(x) for x in struct.unpack_from('<4I',raw,832)]))
    cleanup.update(zip(('retain_begin','retain_end','copy_begin','copy_end','return_begin','return_end'),struct.unpack_from('<6Q',raw,848)))
    event=None
    if flags&16:
        seq,gen,cpu,task,sp,kernel,producer_kind,ticks=struct.unpack_from('<8Q',raw,640)
        demand(ready==2 and gen==generation and producer_kind<=2,'queued producer context bounds')
        event=dict(sequence=seq,generation=gen,cpu=cpu,task=task or None,sp=sp,kernel=kernel,
                   kind=('unknown','task','IRQ')[producer_kind],ticks=ticks)
    return dict(schema='FXFF/1',bytes=896,immutable_bytes=768,present=ready==2,capture_id=capture,boot_epoch=boot,activation_epoch=activation,
        nonce=raw[40:56].hex(),build_identity=raw[56:88].hex(),generation=generation,predicate=predicate,
        predicate_name=FAULT_PREDICATES.get(predicate,'unknown'),stage=FAULT_STAGES[stage],ticks=struct.unpack_from('<Q',raw,104)[0],
        native_site_pc=hex(struct.unpack_from('<Q',raw,112)[0]),observer_return_pc=hex(struct.unpack_from('<Q',raw,120)[0]),
        frequency=struct.unpack_from('<Q',raw,272)[0],worker_reference=struct.unpack_from('<I',raw,280)[0],pending_operation=struct.unpack_from('<I',raw,284)[0],
        requester=dict(cpu=requester_cpu,task=requester_task or None,sp=requester_sp,kernel=requester_kernel,kind=('unknown','task','IRQ')[kind]),
        lock_value=lock,owner_publication_sequence=list(struct.unpack_from('<2I',raw,168)),
        lock_owner=dict(cpu=owner_cpu,task=owner_task or None,sp=owner_sp,kernel=owner_kernel,kind=('unknown','task','IRQ')[owner_kind],
            stage=FAULT_STAGES[owner_stage],native_site_pc=hex(struct.unpack_from('<Q',raw,216)[0])) if flags&4 else None,
        protected_snapshot=bool(flags&1),snapshot_coherent=bool(flags&2),snapshot_is_last_published=not bool(flags&1),
        snapshot_publication_sequence=list(struct.unpack_from('<2I',raw,224)),
        atomic_state=dict(zip(('accepting','stop','callbacks','writers','native_bank_claim','write_epoch','owned','pool_reset','native_stop','rec_started'),struct.unpack_from('<10I',raw,232))),
        state_snapshot=words,banks=list(struct.unpack_from('<8Q',raw,544)),predicate_inputs=list(struct.unpack_from('<12Q',raw,608)),
        cleanup_now=cleanup,immutable_sha256=hashlib.sha256(raw[:768]).hexdigest(),raw_hex=raw.hex(),
        queued_event_producer=event,physical_root_cause_proved=False)

def status_decode(raw):
    demand(len(raw)==224 and raw[:12]==struct.pack('<4sII',b'FXFC',1,224),'frame status schema')
    phase=struct.unpack_from('<I',raw,12)[0];capture,boot,activation=struct.unpack_from('<3Q',raw,16)
    source,flags,duration,used,reason,error=struct.unpack_from('<6I',raw,56)
    begin,end,hz=struct.unpack_from('<3Q',raw,80);lost,slots,banks,candidate=struct.unpack_from('<4I',raw,104)
    x,y,w,h,offset,count=struct.unpack_from('<6I',raw,192)
    owned,lease_error=struct.unpack_from('<2I',raw,216)
    if flags&32:
        return dict(schema='FXFC/1',capture_id=capture,boot_epoch=boot,activation_epoch=activation,
            nonce=raw[40:56].hex(),phase=PHASES[phase] if phase<len(PHASES) else str(phase),phase_code=phase,source_type=source,
            allow_disconnect=bool(flags&1),native_layout=True,wait_for_rec_start=bool(flags&4),
            recording_start_observed=bool(flags&8),duration_ms=duration,bytes=used,reason=reason,error=error,
            begin_ticks=begin,end_ticks=end,frequency=hz,lost=bool(lost),tracked_slots=slots,known_bank_mask=banks,
            candidate=bool(candidate),sha256=raw[128:160].hex(),build_identity=raw[160:192].hex(),
            physical_frame_id=None,cfa=None,linear_primary_auxiliary=False,roi=[x,y,w,h] if source==2 else None,
            source_byte_offset=offset,requested_bytes=count,native_lease=False,native_source_owned=False,
            lease_error=0,unchecked_debug=True,atomic_snapshot=False)
    demand(phase in range(1,8) and capture and boot and activation and source in (1,2) and
        flags&2 and not flags&~31 and (not flags&8 or flags&4) and (not flags&16 or flags&4) and 0<duration<=30000 and
        (used==0 or 256<used<=16384) and reason<=7 and error<=6 and hz and lost<=1 and
        slots<=128 and banks<=3 and candidate<=1 and not any(raw[120:128]) and
        (flags&16 and owned<=1 and lease_error<=13 or not flags&16 and not owned and not lease_error) and
        (source==1 and not any((x,y,w,h)) and 0<count<=8192 or
         source==2 and 0<w<=64 and 0<h<=64 and not (x|w)&3 and not offset and not count),'frame status bounds')
    demand(phase<4 or end>=begin and not owned,'frame freeze clock/ownership')
    return dict(schema='FXFC/1',capture_id=capture,boot_epoch=boot,activation_epoch=activation,
        nonce=raw[40:56].hex(),phase=PHASES[phase],phase_code=phase,source_type=source,
        allow_disconnect=bool(flags&1),native_layout=True,wait_for_rec_start=bool(flags&4),
        recording_start_observed=bool(flags&8),duration_ms=duration,bytes=used,reason=reason,error=error,
        begin_ticks=begin,end_ticks=end,frequency=hz,lost=bool(lost),tracked_slots=slots,known_bank_mask=banks,
        candidate=bool(candidate),sha256=raw[128:160].hex(),build_identity=raw[160:192].hex(),
        physical_frame_id=None,cfa=None,linear_primary_auxiliary=False,
        roi=[x,y,w,h] if source==2 else None,source_byte_offset=offset,requested_bytes=count,
        native_lease=bool(flags&16),native_source_owned=bool(owned),lease_error=lease_error)


def sample_decode(data,ticket):
    demand(len(data)==ticket['bytes'],'frame export length')
    if not data:return dict(saved=False,reason=ticket['phase'],payload_bytes=0)
    if ticket.get('unchecked_debug'):
        demand(len(data)>=256,'debug copy header length')
        values=struct.unpack_from('<16I',data,96)
        return dict(saved=True,unchecked_debug=True,atomic_snapshot=False,header_bytes=256,
            payload_bytes=len(data)-256,payload_sha256=hashlib.sha256(data[256:]).hexdigest(),
            source_type=values[0],native_type=values[1],native_format_id=values[2],
            native_width=values[3],native_height=values[4],native_stride=values[5],
            row_bytes=values[10],rows=values[11],native_data_pointer=hex(struct.unpack_from('<Q',data,64)[0]),
            roi=list(values[6:10]) if values[0]==2 else None,source_byte_offset=values[12],
            native_source_format=None,native_input_words=list(struct.unpack_from('<2I',data,208)),
            native_resource_base=hex(struct.unpack_from('<Q',data,48)[0]),
            native_capacity=struct.unpack_from('<Q',data,56)[0],
            native_job=hex(struct.unpack_from('<Q',data,72)[0]),
            native_descriptor=hex(struct.unpack_from('<Q',data,80)[0]),
            native_consumer_ticks=struct.unpack_from('<Q',data,88)[0],
            copy_completed_ticks=struct.unpack_from('<Q',data,192)[0],
            recording_start_ticks=struct.unpack_from('<Q',data,200)[0] or None,
            native_lease=None,predicates=None,cfa=None,physical_frame_id=None,
            same_integration_proved=False,linear_primary_auxiliary=False,packing='unqualified native bytes')
    version,header=(2,320) if ticket.get('native_lease') else (1,256)
    demand(len(data)>header and data[:12]==struct.pack('<4sII',b'FXFB',version,header) and
        struct.unpack_from('<I',data,12)[0]==len(data) and data[16:40]==ticket_bytes(ticket) and
        data[160:192].hex()==ticket['build_identity'] and (version==2 or not any(data[216:256])),'frame header identity')
    generation,base,capacity,pointer,job,descriptor,consumed=struct.unpack_from('<7Q',data,40)
    source,native_type,format_id,width,height,stride,x,y,roi_w,roi_h,row_bytes,rows,offset,source_format,cfa,flags=struct.unpack_from('<16I',data,96)
    copied,rec_ticks=struct.unpack_from('<2Q',data,192)
    input_words=struct.unpack_from('<2I',data,208)
    demand(generation and capacity and base+capacity<=1<<64 and base<=pointer<base+capacity and source==ticket['source_type'] and
        flags==(15+(16 if ticket['recording_start_observed'] else 0)+(32 if version==2 else 0)) and
        cfa==0 and rows and row_bytes and rows*row_bytes==len(data)-header and
        consumed<=copied and (bool(flags&16)==ticket['recording_start_observed']) and
        (not flags&16 or rec_ticks and rec_ticks<=consumed),'frame saved-copy predicates')
    if source==1:demand(rows==1 and row_bytes==ticket['requested_bytes'] and offset==ticket['source_byte_offset'] and not any((x,y,roi_w,roi_h)),'packet span differs')
    else:demand(native_type==0 and format_id==5 and input_words==(5,0) and 0<roi_w<=64 and 0<roi_h<=64 and not (x|roi_w)&3 and
        roi_w<=width and x<=width-roi_w and roi_h<=height and y<=height-roi_h and
        rows==roi_h and row_bytes==roi_w*7//4 and stride>=(width*7+3)//4 and not offset and
        [x,y,roi_w,roi_h]==ticket['roi'] and height*stride<=capacity-(pointer-base),'native row layout differs')
    start=pointer-base+(offset if source==1 else y*stride+x*7//4)
    span=row_bytes if source==1 else (rows-1)*stride+row_bytes
    demand(start<=capacity and span<=capacity-start,'saved source span outside native token')
    completion=None
    if version==2:
        sequence,completed,held,returned=struct.unpack_from('<4Q',data,216)
        bank,slot,origin,retained,restored,reserved=struct.unpack_from('<6I',data,248)
        identity=hashlib.sha256(data[16:40]+struct.pack('<Q',sequence)).digest()
        demand(sequence and rec_ticks<=completed<=consumed<=held<=copied<=returned<=ticket['end_ticks'] and
            bank<2 and slot<4 and (origin,retained,restored,reserved)==(1,1,1,0) and data[272:304]==identity and
            not any(data[304:320]) and not ticket['lease_error'] and not ticket['native_source_owned'],'native lease receipt differs')
        completion=dict(token=sequence,ticks=completed,bank=bank,slot=slot,component='native raw receive stream',
            held_ticks=held,returned_ticks=returned,lease_id=identity.hex(),origin='original 1FC IRQ / kernel flag / FIFO adoption')
    return dict(saved=True,source_type=source,native_allocation_generation=generation,native_resource_base=hex(base),
        native_capacity=capacity,native_data_pointer=hex(pointer),native_job=hex(job),native_descriptor=hex(descriptor),
        native_type=native_type,native_format_id=format_id,native_source_format=source_format,
        native_input_words=list(input_words),
        native_width=width,native_height=height,native_stride=stride,roi=[x,y,roi_w,roi_h] if source==2 else None,
        source_byte_offset=offset,row_bytes=row_bytes,rows=rows,payload_bytes=len(data)-header,header_bytes=header,native_lease=completion,
        native_consumer_ticks=consumed,copy_completed_ticks=copied,recording_start_ticks=rec_ticks or None,
        predicates=dict(both_banks_withdrawn=True,native_allocator_excluded=True,software_generation_stable=True,double_read_equal=True),
        packing='native 14-bit row groups; bit order unqualified' if source==2 else 'opaque native codec packet',
        cfa=None,physical_frame_id=None,same_integration_proved=False,linear_primary_auxiliary=False,
        payload_sha256=hashlib.sha256(data[header:]).hexdigest())


class FrameSample:
    def __init__(self,client,expected_build=None):self.observer=Observation(client,expected_build);self.description=None
    def query(self,op,data=b''):return self.observer.query(9,struct.pack('<I',op)+data)
    def describe(self):
        base=self.observer.describe()
        if not base['available']['software_submit_trace']:
            self.description=dict(available=False,reason='core has no observer extension');return self.description
        try:raw=self.query(0)
        except BackendError as error:
            if error.code!=5:raise
            self.description=dict(available=False,reason='core lacks native input-copy service');return self.description
        caps=struct.unpack_from('<I',raw,12)[0] if len(raw)>=16 else 0
        if caps==71:
            demand(len(raw)==192 and raw[:12]==struct.pack('<4sII',b'FXFD',1,192),'debug descriptor framing')
            self.description=dict(available=True,schema='FXFD/1',build_identity=base['build_identity'],boot_epoch=base['boot_epoch'],
                activation_epoch=base['activation_epoch'],maximum_bytes=16384,maximum_roi=[64,64],maximum_packet_bytes=8192,
                maximum_duration_ms=(1<<32)-1,native_packet_copy=True,native_packed_rows=True,
                wait_for_original_rec_start=True,native_pool_lease=False,maximum_native_hold_ms=0,
                first_fault=False,copy_diagnostics=False,event_journal_version=0,unchecked_debug=True,
                physical_qualification=False,atomic_snapshot=False)
            return self.description
        demand(len(raw)==192 and raw[:12]==struct.pack('<4sII',b'FXFD',1,192) and
            struct.unpack_from('<6I',raw,12)==(caps,16384,256,30000,64,8192) and caps in (7,15,31,63) and
            (not any(raw[36:64]) if caps==7 else struct.unpack_from('<2I',raw,36)==(320,500) and
             (not any(raw[44:64]) if caps==15 else struct.unpack_from('<2I',raw,44)==(1,896) and
              (not any(raw[52:64]) if caps==31 else struct.unpack_from('<3I',raw,52) in ((1,224,0),(2,256,1))))) and
            raw[64:96].hex()==base['build_identity'] and not any(raw[96:]),'frame descriptor differs')
        self.description=dict(available=True,schema='FXFD/1',build_identity=base['build_identity'],boot_epoch=base['boot_epoch'],
            activation_epoch=base['activation_epoch'],maximum_bytes=16384,maximum_roi=[64,64],maximum_packet_bytes=8192,
            maximum_duration_ms=30000,native_packet_copy=True,native_packed_rows=True,wait_for_original_rec_start=True,
            cfa=None,physical_frame_id=False,linear_primary_auxiliary=False,physical_qualification=False,
            native_pool_lease=bool(caps&8),maximum_native_hold_ms=500 if caps&8 else 0,
            first_fault=bool(caps&16),first_fault_schema='FXFF/1' if caps&16 else None,first_fault_bytes=896 if caps&16 else 0,
            copy_diagnostics=bool(caps&32),event_journal_version=struct.unpack_from('<I',raw,60)[0])
        return self.description
    def checked(self,raw,ticket=None):
        result=status_decode(raw);d=self.description or self.describe()
        if result.get('unchecked_debug'):return result
        demand(d['available'] and all(d[k]==result[k] for k in ('build_identity','boot_epoch','activation_epoch')),'frame epoch/build differs')
        if ticket is not None:demand(ticket_bytes(result)==ticket_bytes(ticket) and result['nonce']==ticket['nonce'],'different frame capture')
        return result
    def key(self,ticket):
        d=self.description or self.describe()
        if d.get('unchecked_debug'):return ticket_bytes(ticket)
        demand(d['available'] and all(ticket[k]==d[k] for k in ('build_identity','boot_epoch','activation_epoch')),'frame ticket belongs to another epoch/build')
        return ticket_bytes(ticket)
    def first_fault(self,ticket):
        d=self.description or self.describe();demand(d.get('first_fault'),'core has no first-fault witness')
        return fault_decode(self.query(7,self.key(ticket)),ticket)
    def copy_diagnostics(self,ticket):
        demand((self.description or self.describe()).get('copy_diagnostics'),'core has no copy diagnostics')
        return copy_diagnostics_decode(self.query(8,self.key(ticket)),ticket)
    def arm(self,nonce,source=1,duration_ms=30000,offset=0,count=64,roi=None,allow_disconnect=False,wait_for_rec_start=False,require_lease=False):
        d=self.description or self.describe();demand(d['available'],'native input-copy service unavailable')
        unchecked=d.get('unchecked_debug',False)
        if unchecked:require_lease=False
        demand(not require_lease or d.get('native_pool_lease') and wait_for_rec_start,'native lease requires supported core and explicit REC wait')
        demand(isinstance(nonce,bytes) and len(nonce)==16 and (unchecked or any(nonce)) and source in (1,2) and
            (0<=duration_ms<1<<32 if unchecked else 0<duration_ms<=30000),'frame ARM arguments')
        x=y=w=h=0
        if source==1:demand(roi is None and 0<=offset<1<<32 and 0<count<=8192,'packet byte range')
        else:
            demand(isinstance(roi,(list,tuple)) and len(roi)==4 and all(isinstance(v,int) and 0<=v<1<<32 for v in roi),'native ROI')
            x,y,w,h=roi;demand(0<w<=64 and 0<h<=64 and (unchecked or not (x|w)&3),'native ROI alignment/size');offset=count=0
        flags=2|int(allow_disconnect)|(int(wait_for_rec_start)<<2)|(int(require_lease)<<4)
        result=self.checked(self.query(1,nonce+struct.pack('<11I',duration_ms,flags,source,x,y,w,h,offset,count,0,0)))
        if unchecked:return result
        demand(result['nonce']==nonce.hex() and result['source_type']==source and result['duration_ms']==duration_ms and
            result['allow_disconnect']==allow_disconnect and result['wait_for_rec_start']==wait_for_rec_start and
            result['source_byte_offset']==offset and result['requested_bytes']==count and
            result['roi']==([x,y,w,h] if source==2 else None) and result['native_lease']==require_lease,'frame ARM replay/configuration differs')
        return result
    def find(self,nonce):
        demand(isinstance(nonce,bytes) and len(nonce)==16,'frame nonce');return self.checked(self.query(6,nonce))
    def status(self,ticket):return self.checked(self.query(2,self.key(ticket)),ticket)
    def stop(self,ticket):return self.checked(self.query(4,self.key(ticket)),ticket)
    def release(self,ticket):self.status(ticket);demand(self.query(5,self.key(ticket))==b'','frame RELEASE response')
    def export(self,ticket,path):
        path=Path(path);sidecar=path.with_name(path.name+'.json');payload=path.with_name(path.name+'.payload')
        demand(not any(p.exists() or p.is_symlink() for p in (path,sidecar,payload)),'frame export exists')
        row=self.status(ticket);unchecked=row.get('unchecked_debug',False)
        demand(unchecked or row['phase_code']>=4,'frame copy is not frozen');data=bytearray()
        for offset in range(0,row['bytes'],1008):
            n=min(1008,row['bytes']-offset);part=self.query(3,self.key(row)+struct.pack('<2I',offset,n));demand(len(part)==n,'short frame chunk');data.extend(part)
        demand(unchecked or hashlib.sha256(data).hexdigest()==row['sha256'],'saved frame checksum differs');decoded=sample_decode(data,row)
        if not unchecked:demand(self.status(ticket)==row,'saved frame changed')
        witness=self.first_fault(row) if (self.description or {}).get('first_fault') else None
        diagnostics=self.copy_diagnostics(row) if (self.description or {}).get('copy_diagnostics') else None
        with path.open('xb') as stream:stream.write(data)
        if data:
            with payload.open('xb') as stream:stream.write(data[decoded['header_bytes']:])
        result=dict(ticket=row,sample=decoded,file=str(path.resolve()),file_sha256=hashlib.sha256(data).hexdigest())
        if witness is not None:result['first_fault']=witness
        if diagnostics is not None:result['copy_diagnostics']=diagnostics
        with sidecar.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
        return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=('describe','arm','find','status','stop','read','release','first-fault','copy-diagnostics'))
    p.add_argument('--ticket',type=Path);p.add_argument('--output',type=Path);p.add_argument('--nonce');p.add_argument('--expected-build')
    p.add_argument('--source',choices=('packet','native-rows'),default='packet');p.add_argument('--offset',type=int,default=0)
    p.add_argument('--count',type=int,default=64);p.add_argument('--roi',type=int,nargs=4);p.add_argument('--duration-ms',type=int,default=30000)
    p.add_argument('--allow-disconnect',action='store_true');p.add_argument('--wait-for-rec-start',action='store_true');p.add_argument('--require-lease',action='store_true');a=p.parse_args()
    if a.command in ('arm','find','read') and not a.output:p.error('new --output required')
    if a.output and (a.output.exists() or a.output.is_symlink()):p.error('output exists')
    if a.command in ('status','stop','read','release','first-fault','copy-diagnostics') and not a.ticket:p.error('--ticket required')
    if a.command=='find' and not a.nonce:p.error('--nonce required')
    ticket=json.loads(a.ticket.read_text()) if a.ticket else None;nonce=bytes.fromhex(a.nonce) if a.nonce else uuid.uuid4().bytes
    from ptp_client import Client
    from ptp_transport import open_transport
    with open_transport() as transport:
        client=Client(transport);client.hello();sample=FrameSample(client,a.expected_build)
        description=sample.describe()
        if a.command=='describe':result=description
        elif a.command=='arm':
            demand(description['available'],'native input-copy service unavailable')
            with a.output.open('x',encoding='utf-8') as stream:json.dump(dict(nonce=nonce.hex(),state='submitting',description=description),stream,indent=2)
            result=sample.arm(nonce,1 if a.source=='packet' else 2,a.duration_ms,a.offset,a.count,a.roi,a.allow_disconnect,a.wait_for_rec_start,a.require_lease)
            replace_owned_receipt(a.output,result)
        elif a.command=='find':
            result=sample.find(nonce)
            with a.output.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
        elif a.command=='read':result=sample.export(ticket,a.output)
        elif a.command in ('first-fault','copy-diagnostics'):
            result=sample.first_fault(ticket) if a.command=='first-fault' else sample.copy_diagnostics(ticket)
            if a.output:
                with a.output.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
        elif a.command=='release':sample.release(ticket);result=dict(released=True,capture_id=ticket['capture_id'])
        else:result=getattr(sample,a.command)(ticket)
        print(json.dumps(result,indent=2))


if __name__=='__main__':main()
