"""Fixed C110 passive observation sites. No host-supplied function/address API.

The whole official EG fingerprint and each displaced instruction are checked
before generation. Gates preserve the native stack, LR, GP/NZCV/PAR and replay
the original instruction once. The generated code contains no device accesses.
"""
import hashlib,struct,re
SHA='606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542'
BASE=0x40160000
TEMPLATES={1:'auxiliary-ratio',2:'sdes-write',3:'af-request',4:'af-transport',5:'af-receipt',
    6:'exposure-submit',7:'exposure-metadata-copy',8:'exposure-stage',9:'exposure-notification',
    10:'sensor-exposure-adoption',11:'sensor-row-selector',12:'sensor-packet-batch',
    13:'sensor-byte-wrapper',14:'sensor-request-adoption',15:'recording-callback',16:'af-row-table',
    17:'raw-movie-input',18:'raw-source-adoption',19:'raw-movie-queue',
    20:'raw-bank-cache',21:'pool-retire',22:'pool-recycle',
    23:'raw-format-descriptor',24:'receiver-notification',25:'receiver-bank-program',
    26:'native-buffer-post',27:'native-resource-rotation',28:'native-pool-reset',29:'native-source-irq',30:'native-source-dispatch'}
# pc, template, local event, kind(1 entry/2 exit/3 stage/4 verified tail),
# exact original SP depth relative to observed scope entry (not a frame ID).
POINTS=[
 (0x41013444,1,1,1,0),(0x4101344c,1,2,3,0),(0x41013504,1,3,3,0),
 (0x41013550,1,5,3,0),(0x410135fc,1,6,3,0),(0x410137a8,1,7,3,0),
 (0x410145a8,1,8,3,0),(0x410135a4,1,9,3,0),(0x410135ec,1,10,3,0),
 (0x410135c4,1,11,3,0),(0x4101362c,1,4,2,0),
 (0x40e2ab24,2,1,1,0),(0x40e2ab48,2,2,3,0),(0x40e2ab4c,2,3,3,0),(0x40e2ab54,2,4,2,0),
 (0x410371a0,3,1,1,0),(0x41037338,3,2,3,0x70),(0x41037390,3,3,3,0x70),
 (0x410373b4,3,4,3,0x70),(0x410373cc,3,5,3,0x70),(0x410373e4,3,6,2,0),
 (0x40ecaa68,4,1,1,0),(0x40ecabe8,4,2,3,0x50),(0x40ecac00,4,3,3,0x50),
 (0x40ecac24,4,4,3,0x50),(0x40ecac88,4,5,2,0),(0x40ecabfc,4,6,3,0x50),
 (0x40ecad28,5,1,1,0),(0x40ecad58,5,2,3,0x10),(0x40ecad5c,5,3,3,0x10),
 (0x40ecad7c,5,4,2,0),(0x40ecad98,5,5,2,0),
 (0x4122a9a4,6,1,1,0),(0x4122a9a8,6,2,3,0),(0x4122a9e4,6,3,3,0),(0x4122abd4,6,4,2,0),
 (0x412286cc,7,1,1,0),(0x412286e8,7,2,3,0xc0),(0x41228718,7,3,3,0xc0),(0x41228af0,7,4,2,0),
 (0x41148568,8,1,1,0),(0x41148618,8,2,3,0x50),(0x41148bc4,8,3,3,0x50),(0x4114894c,8,4,2,0),
 (0x41142638,9,1,1,0),(0x4114264c,9,2,3,0),(0x41142698,9,3,3,0),
 (0x411426b0,9,4,3,0),(0x411429bc,9,5,2,0),
 (0x4135f72c,10,1,1,0),(0x4135f8d8,10,2,3,0x40),(0x4135f8f0,10,3,3,0x40),(0x4135fa04,10,4,2,0),
 (0x41362f5c,11,1,1,0),(0x41362f74,11,2,2,0),(0x41362fb4,11,3,4,0),
 (0x41362e04,12,1,1,0),(0x41362e28,12,2,3,0x30),(0x41362e34,12,3,3,0x30),(0x41362e5c,12,4,2,0),
 (0x413521d4,13,1,1,0),(0x413522ec,13,2,2,0),
 (0x4135fa08,14,1,1,0),(0x4135fb24,14,2,3,0x80),(0x4135fb40,14,3,3,0x80),(0x4135fcc4,14,4,2,0),
 (0x40f72f48,15,1,1,0),(0x40f72f6c,15,3,2,0),(0x40f72f98,15,4,2,0),
 (0x40f72f9c,15,5,1,0),(0x40f73000,15,9,2,0),(0x40f73370,15,10,3,0),
 (0x41014274,16,1,1,0),(0x410142dc,16,2,3,0),(0x410142e0,16,3,2,0),
 # Native movie consumer: distinct R3D and ordinary-image input branches.
 (0x4108ae7c,17,1,1,0),(0x4108af30,17,2,3,0xd0),(0x4108afa0,17,3,3,0xd0),
 (0x4108b028,17,4,3,0xd0),(0x4108b02c,17,5,3,0xd0),(0x4108aed8,17,6,2,0),
 # Wait completion, native bank programming and software descriptor adoption.
 # None of these sites alone establishes a completed physical pixel frame.
 (0x410862d8,18,1,1,0),(0x410863f8,18,2,3,0x50),
 (0x41086384,18,3,3,0x50),(0x41086388,18,4,3,0x50),
 (0x410864bc,18,5,3,0x50),(0x41086310,18,6,2,0),
 # The actual original synchronous queue producer, not an invented lease.
 (0x40f324c8,19,1,1,0),(0x40f3266c,19,2,3,0x130),
 (0x40f32670,19,3,3,0x130),(0x40f32524,19,4,2,0),
 # Actual native pool selection and address cache publication. Addresses
 # identify programmed resources, not completed physical frames or planes.
 (0x4107ce74,20,1,1,0),(0x4107d088,20,2,3,0xa0),
 (0x4107cfb0,20,5,3,0xa0),(0x4107d144,20,3,3,0xa0),(0x4107d15c,20,4,2,0),
 # Observe existing ownership transitions; never invoke them on the host's
 # behalf. The hardware may still refer to a software-retired resource.
 (0x4123a370,21,1,1,0),(0x4123a3c4,21,2,3,0xc0),(0x4123a644,21,4,2,0),
 (0x4123a648,22,1,1,0),(0x4123a670,22,2,3,0x20),(0x4123a684,22,3,3,0x20),
 (0x4123a6a4,22,4,2,0),
 (0x41244810,23,1,1,0),(0x41244880,23,2,3,0x20),(0x41244888,23,3,2,0),
 # Original receiver callback, including interrupt context. Such records
 # retain raw notification bits but cannot acquire a fabricated task token.
 (0x4107d160,24,1,1,0),(0x4107d1b8,24,2,3,0x10),
 (0x4107d1d4,24,3,3,0x10),(0x4107d1a0,24,4,2,0),
 (0x4107522c,25,1,1,0),(0x410752e4,25,2,2,0),
 # Common native driver stores, allocator rotation while its mutex is held,
 # and pool destruction. The input-copy collector also observes these when
 # the ordinary event recorder is not armed.
 (0x40e3133c,26,1,1,0),(0x40e313c8,26,2,2,0),
 (0x4123a748,27,1,1,0),(0x4123a7b8,27,2,3,0x30),(0x4123a7c8,27,3,2,0),
 (0x4123a0b0,28,1,1,0),(0x4123a1d0,28,2,2,0),
 (0x40e30fb8,29,1,1,0),(0x40e30fd8,29,2,4,0),
 (0x40e31000,29,3,2,0),(0x40e31004,29,4,2,0),
 (0x40ec9ad8,30,1,1,0),(0x40ec9b9c,30,2,4,0),(0x40ec9ba8,30,3,2,0)]
HOOKS={p:'fx_c110_event_'+format(p,'x') for p,*_ in POINTS}
MASK=sum(1<<n for n in TEMPLATES)


def emit(image):
    if hashlib.sha256(image).hexdigest()!=SHA:raise ValueError('fixed C110 official image required')
    from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
    cs=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
    lines=['.text','.macro save'];targets={}
    def native(address):
        address=int(address.strip().lstrip('#'),0) if isinstance(address,str) else address
        name='fx_c110_event_pc_'+format(address,'x');targets[name]=address;return name
    def link_return(pc):
        return [f'movz x30,#{(pc+4)&65535}',f'movk x30,#{(pc+4)>>16},lsl #16']
    for n in range(0,30,2):lines.append(f'stp x{n},x{n+1},[sp,#{n*8}]')
    lines+=['str x30,[sp,#240]','mrs x9,nzcv','str x9,[sp,#248]',
        'mrs x9,tpidrro_el0','str x9,[sp,#264]','mrs x9,mpidr_el1','str x9,[sp,#272]',
        'mrs x9,par_el1','str x9,[sp,#280]','mrs x9,tpidr_el1','str x9,[sp,#288]',
        'mrs x9,daif','str x9,[sp,#296]','.endm','.macro restore',
        'ldr x9,[sp,#280]','msr par_el1,x9','ldr x9,[sp,#248]','msr nzcv,x9']
    for n in range(0,30,2):lines.append(f'ldp x{n},x{n+1},[sp,#{n*8}]')
    lines+=['ldr x30,[sp,#240]','.endm']
    for index,(pc,_,_,_,_) in enumerate(POINTS):
        name=HOOKS[pc];raw=image[pc-BASE:pc-BASE+4];ins=list(cs.disasm(raw,pc))
        if len(ins)!=1:raise ValueError('invalid original event instruction '+hex(pc))
        i=ins[0];op=i.mnemonic;args=i.op_str
        lines+=['.global '+name,'.type '+name+',%function',name+':','sub sp,sp,#320','save',
            'mov x0,sp',f'mov w1,#{index}','bl fx_c110_event_trace_observe','restore','add sp,sp,#320']
        if op=='b':lines+=['b '+native(args)]
        elif op=='bl':
            lines += link_return(pc)+['b '+native(args)]
        elif op=='blr':
            if args=='x30':raise ValueError('indirect LR target not supported')
            lines += link_return(pc)+['br '+args]
        elif op=='ret':lines += ['ret '+args]
        elif op in ('cbz','cbnz','tbz','tbnz'):
            inv={'cbz':'cbnz','cbnz':'cbz','tbz':'tbnz','tbnz':'tbz'}[op]
            fields=args.rsplit(',',1);lines += [inv+' '+fields[0]+',1f','b '+native(fields[1]),'1:','b '+native(pc+4)]
        elif op.startswith('b.'):
            inv={'eq':'ne','ne':'eq','lo':'hs','hs':'lo','lt':'ge','ge':'lt','le':'gt','gt':'le','hi':'ls','ls':'hi','mi':'pl','pl':'mi','vs':'vc','vc':'vs'}
            lines += ['b.'+inv[op[2:]]+' 1f','b '+native(args),'1:','b '+native(pc+4)]
        else:
            if op in ('adr','adrp') or '#' in args and op in ('ldr','ldrsw') and '[' not in args:
                raise ValueError('PC-relative event site requires explicit relocation '+hex(pc))
            lines += ['.inst '+hex(struct.unpack('<I',raw)[0]),'b '+native(pc+4)]
        lines+=['.size '+name+',.-'+name]
    header='#include <stdint.h>\n#define FX_C110_EVENT_MASK UINT64_C('+hex(MASK)+')\n'
    header+='static const struct {uint64_t pc;uint32_t template_id,event,kind,depth;} fx_event_sites[]={\n'
    header+=''.join('{UINT64_C('+hex(pc)+'),'+','.join(str(x)+'u' for x in (t,e,k,depth))+'},\n' for pc,t,e,k,depth in POINTS)
    header+='};\n'
    lines+=['.extern '+name for name in targets]
    return '\n'.join(lines)+'\n',header


def link_args(assembly):
    return ['--defsym=fx_c110_event_pc_'+pc+'=0x'+pc for pc in sorted(set(re.findall(r'fx_c110_event_pc_([0-9a-f]+)',assembly)))]
