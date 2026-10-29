"""Independent firmware identities and named ABI bindings, owned implementation.

Generated fingerprints come from a complete hash-pinned original image. A
generated fingerprint is not evidence of an ABI: the references below name
the existing original-code ABI checks. No NLoader source is used.
"""
import hashlib
import json
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE=0x40160000
def native_optimization(source):
    """Shared size policy for cold control/UI code; native hot paths stay O2."""
    cold=('src/platform.c','src/binding_store.c','src/native_ui.c',
        'src/native_ui_routes.c','src/native_ui_pager.c','src/native_ui_editor.c','src/tasks.c',
        'src/native_resources.c','src/worker_wait.c','src/capacity.c','src/slot_pool.c',
        'src/module_manager.c','src/native_modules.c','src/platform/z9_c532/menu.c',
        'src/platform/zr_c110/menu.c','src/startup.c','src/startup_files.c','src/startup_service.c')
    return '-Os' if source in cold else '-O2'

PROFILES={
 'zr-c110':dict(model='Nikon ZR',version='C 1.10',bytes=88157349,
  sha256='606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542',
  module_identity='43bf4f5f1278fa6b0a551ceb6fa4ee749dc74bbc8a5ef4fba19b30062184e546',
  functions={'rtos.memory.allocate':0x40f41084,'rtos.task.create':0x4018507c,'rtos.task.resume':0x401855f8,'rtos.task.delay':0x40187b18,
   'camera.card.open':0x402e883c,'camera.card.read':0x402e8a0c,'camera.card.close':0x402e889c,
   'camera.card.seek64':0x402e8c34,
   'camera.menu.page.get':0x403e876c,'camera.menu.text.get':0x4033a66c,
   'camera.ui.editor.init':0x40641f9c,'camera.ui.editor.read':0x40644098,
   'camera.ui.editor.insert-byte':0x40645638,'camera.ui.editor.delete-byte':0x406441fc,'camera.ui.editor.hit':0x406457dc,
   'camera.ui.text.measure':0x4032a618,'camera.ui.text.align':0x404012a4,
   'camera.ui.bounds.get':0x4032a4f4,'camera.ui.bounds.set':0x4032a490,
   'camera.card.directory.open':0x402e8fb8,'camera.card.directory.next':0x402e9068,
   'camera.card.directory.close':0x402e90b8,'camera.card.rename':0x402e8d48,'camera.card.write':0x402e8ad0,'native.string.length':0x41486740,
   'rtos.memory.release':0x40f410c4,
   'rtos.mmu.map':0x4016eb68,'rtos.mmu.unmap':0x4016eba0,'rtos.mmu.protect':0x4016eb0c,
   'rtos.mmu.context.map':0x40172b88,'rtos.mmu.context.unmap':0x40172380,
   'rtos.mmu.context.protect':0x401731c0,'rtos.mmu.context.walk':0x40172608,
   'rtos.mmu.context.translate':0x40173098,'rtos.mmu.context.attributes':0x40173104,
   'rtos.mmu.translate':0x4016eea4,'rtos.mmu.attributes':0x4016f0f4,'rtos.mmu.mapped':0x4016f0bc,'rtos.mmu.invalidate':0x4016f178,
   'rtos.mmu.invalidate-all':0x4016f1d0,
   'rtos.irq.register':0x4016c8f4,'rtos.irq.register-affinity':0x4016c918,'rtos.irq.unregister':0x4016c938,
   'rtos.irq.disable':0x4016c9c4,'rtos.irq.enable':0x4016c9e4,'rtos.irq.status':0x4016ca04,
   'rtos.irq.priority-levels':0x4016ca78,'rtos.irq.limit':0x4016caa8,
   'rtos.irq.priority.get':0x4016cac0,'rtos.irq.priority.set':0x4016cb24,
   'rtos.irq.trigger.get':0x4016cb48,'rtos.irq.trigger.set':0x4016cb9c,
   'rtos.irq.pending.set':0x4016cc2c,'rtos.irq.pending.clear':0x4016cbe8,'rtos.irq.pending.is-set':0x4016cc70,
   'rtos.irq.affinity.set':0x4016cd78,'rtos.irq.affinity.get':0x4016cdc4,
   'rtos.mutex.create':0x4017f8dc,'rtos.mutex.delete':0x4017fad0,
   'rtos.mutex.lock':0x4017fd24,'rtos.mutex.try-lock':0x40180004,
   'rtos.mutex.lock-timeout':0x401801f8,'rtos.mutex.unlock':0x401805a4,
   'rtos.spinlock.init':0x40173b9c,'rtos.spinlock.try-lock':0x40173c60,'rtos.spinlock.unlock':0x40173d04,
   'rtos.semaphore.create':0x40180e68,'rtos.semaphore.delete':0x40181044,
   'rtos.semaphore.signal':0x40181200,'rtos.semaphore.signal-irq':0x401813fc,
   'rtos.semaphore.wait':0x401815e4,'rtos.semaphore.poll':0x40181778,
   'rtos.semaphore.wait-timeout':0x40181898,
   'rtos.context.is-irq':0x401834a8,'rtos.event.create':0x4017b254,'rtos.event.delete':0x4017b420,
   'rtos.event.set':0x4017b5dc,'rtos.event.set-irq':0x4017b79c,
   'rtos.event.clear-keep':0x4017b95c,'rtos.event.clear-keep-irq':0x4017ba74,'rtos.event.wait':0x4017c040},
  tables={'camera.ui.editor.template':(0xa02927188,96),'rtos.irq.controller':(0xa059a2ec0,0x2338),'rtos.mmu.context':(0xa059aa3a0,0x58),'camera.menu.page-roots':(0xa034dfc20,56),'camera.locale.font-root':(0xa030fafa8,8),'camera.video.correction.records':(0xa029c96d0,64)},
  native_pages=dict(first=84296264,stop=0x749,kinds={2:1518,0:3,3:147,5:196}),
  abi_evidence=['docs/porting.md','host/verify_platform_build.py','host/verify_native_namespace.py']),
 'z9-c532':dict(model='Nikon Z9',version='C 5.32',bytes=81823629,
  sha256='6380ded18395ce11cb327c766721274c85127d2cf1a1e887ee60e3d156810bdd',
  module_identity='6380ded18395ce11cb327c766721274c85127d2cf1a1e887ee60e3d156810bdd',
  functions={'rtos.memory.allocate':0x40d30458,'rtos.task.create':0x40185064,'rtos.task.resume':0x401855e0,'rtos.task.delay':0x40187b00,
   'camera.card.open':0x402dc960,'camera.card.read':0x402dcb30,'camera.card.close':0x402dc9c0,
   'camera.card.seek64':0x402dcd50,
   'camera.menu.page.get':0x403d6618,'camera.menu.text.get':0x4032e57c,
   'camera.ui.editor.init':0x4060ae34,'camera.ui.editor.read':0x4060cf30,
   'camera.ui.editor.insert-byte':0x4060e4d0,'camera.ui.editor.delete-byte':0x4060d094,'camera.ui.editor.hit':0x4060e674,
   'camera.ui.text.measure':0x4031e87c,'camera.ui.text.align':0x403ecbc4,
   'camera.ui.bounds.get':0x4031e758,'camera.ui.bounds.set':0x4031e6f4,
   'camera.card.directory.open':0x402dd0d4,'camera.card.directory.next':0x402dd184,
   'camera.card.directory.close':0x402dd1d4,'camera.card.rename':0x402dce64,'camera.card.write':0x402dcbf4,
   'rtos.memory.release':0x40d30498,
   'rtos.mmu.map':0x4016eb68,'rtos.mmu.unmap':0x4016eba0,'rtos.mmu.protect':0x4016eb0c,
   'rtos.mmu.context.map':0x40172b88,'rtos.mmu.context.unmap':0x40172380,
   'rtos.mmu.context.protect':0x401731c0,'rtos.mmu.context.walk':0x40172608,
   'rtos.mmu.context.translate':0x40173098,'rtos.mmu.context.attributes':0x40173104,
   'rtos.mmu.translate':0x4016eea4,'rtos.mmu.attributes':0x4016f0f4,'rtos.mmu.mapped':0x4016f0bc,'rtos.mmu.invalidate':0x4016f178,
   'rtos.mmu.invalidate-all':0x4016f1d0,
   'rtos.irq.register':0x4016c8f4,'rtos.irq.register-affinity':0x4016c918,'rtos.irq.unregister':0x4016c938,
   'rtos.irq.disable':0x4016c9c4,'rtos.irq.enable':0x4016c9e4,'rtos.irq.status':0x4016ca04,
   'rtos.irq.priority-levels':0x4016ca78,'rtos.irq.limit':0x4016caa8,
   'rtos.irq.priority.get':0x4016cac0,'rtos.irq.priority.set':0x4016cb24,
   'rtos.irq.trigger.get':0x4016cb48,'rtos.irq.trigger.set':0x4016cb9c,
   'rtos.irq.pending.set':0x4016cc2c,'rtos.irq.pending.clear':0x4016cbe8,'rtos.irq.pending.is-set':0x4016cc70,
   'rtos.irq.affinity.set':0x4016cd78,'rtos.irq.affinity.get':0x4016cdc4,
   'rtos.mutex.create':0x4017f8dc,'rtos.mutex.delete':0x4017fad0,
   'rtos.mutex.lock':0x4017fd24,'rtos.mutex.try-lock':0x40180004,
   'rtos.mutex.lock-timeout':0x401801f8,'rtos.mutex.unlock':0x4018058c,
   'rtos.spinlock.init':0x40173b9c,'rtos.spinlock.try-lock':0x40173c60,'rtos.spinlock.unlock':0x40173d04,
   'rtos.semaphore.create':0x40180e50,'rtos.semaphore.delete':0x4018102c,
   'rtos.semaphore.signal':0x401811e8,'rtos.semaphore.signal-irq':0x401813e4,
   'rtos.semaphore.wait':0x401815cc,'rtos.semaphore.poll':0x40181760,
   'rtos.semaphore.wait-timeout':0x40181880,
   'rtos.context.is-irq':0x40183490,'rtos.event.create':0x4017b254,'rtos.event.delete':0x4017b420,
   'rtos.event.set':0x4017b5dc,'rtos.event.set-irq':0x4017b79c,
   'rtos.event.clear-keep':0x4017b95c,'rtos.event.clear-keep-irq':0x4017ba74,'rtos.event.wait':0x4017c040},
  tables={'camera.ui.editor.template':(0xa0290c7f0,96),'rtos.irq.controller':(0xa04e05820,0x2338),'rtos.mmu.context':(0xa04e0cd00,0x58),'camera.menu.page-roots':(0xa032d5de8,56),'camera.locale.font-root':(0xa02f4ab30,8)},
  native_pages=dict(first=79632912,stop=0x6b8,kinds={2:1396,0:3,3:143,5:177}),
  abi_evidence=['docs/porting.md','host/verify_platform_build.py','host/verify_native_namespace.py'])}

# Independently observed original dispatcher ABI. These are mid-function
# instruction windows, never exported as callable named bindings. The source
# page is x20; selected 32-byte event is x22; x0 is NOT a page argument.
UI_CONDITION_ABI = {'zr-c110': (1079156136, 168, 'd268b51081f7f0a1103a87522470a16cc7601cdcdda0dbbcd35dfcfa5792f4bc'), 'z9-c532': (1078993640, 168, '8731d7c78c916f02977eaaf3f9327f3f1484df5f58adc81465938dccc4f7a3c8')}

def verify_ui_condition_abi(target,data):
    if target not in UI_CONDITION_ABI:raise ValueError('unknown native condition ABI')
    address,size,digest=UI_CONDITION_ABI[target];at=address-BASE;original=data[at:at+size];expected=original.hex()
    if len(original)!=size or hashlib.sha256(original).hexdigest()!=digest:raise ValueError('native condition caller ABI differs')
    return dict(schema='fenestra.native-condition/v1',source_page='x20',selected_event='x22',
                ordinary_arguments=False,dispatcher_window=hex(address),original=expected,
                condition_call=hex(address+0xa4),return_address=hex(address+0xa8),
                evidence='docs/porting.md')

def verify_page_namespace(profile,data):
    """Check the full fixed native array, including gaps, dialogs and live view.

    The stop selector is independently bound by each original navigator. It is
    NOT the first gap or the end of the consecutive kind-2 menu prefix.
    """
    layout=profile['native_pages'];counts={};gaps=[]
    if not 0<layout['stop']<0x7000:raise ValueError('native page range overlaps extensions')
    for page in range(1,layout['stop']):
        identity=struct.unpack_from('<I',data,layout['first']+(page-1)*96)[0]
        kind=identity&65535;counts[kind]=counts.get(kind,0)+1
        if not identity:gaps.append(page)
        elif identity>>16!=page or kind not in (2,3,5):raise ValueError('native page array differs')
    if counts!=layout['kinds']:raise ValueError('native page kinds/counts differ')
    return dict(first_file=layout['first'],native_slots=layout['stop']-1,kinds=counts,gaps=gaps,
                fenestra=0x7000,wonderlantern=0x7001,module_base=0x7100,module_limit=0x9500,
                manager_base=0x704a,startup_auto=0x705c,startup_guard=0x705d,extension_limit=0x9500,collision_free=True)

def verify_card_capacity(target,data):
    """Independent original call chain and FCB bound, not a runtime free count."""
    if target=='zr-c110':
        chain=[(0x402e8878,0x4090c41c),(0x4090c45c,0x4090e1c0),(0x4090e210,0x4090fd2c)]
        loop,error=0x4090fd7c,0x4090e238
    elif target=='z9-c532':
        chain=[(0x402dc99c,0x408b94f8),(0x408b9538,0x408bb1f8),(0x408bb248,0x408bcd54)]
        loop,error=0x408bcda4,0x408bb270
    else:raise ValueError('unverified native card capacity profile')
    for address,destination in chain:
        word=struct.unpack_from('<I',data,address-BASE)[0]
        displacement=word&0x03ffffff
        if displacement&(1<<25):displacement-=1<<26
        if word>>26!=0x25 or address+4*displacement!=destination:
            raise ValueError('original file allocation chain differs')
    # CMP X21,#30 / B.LO loop; MOV W21,#0x901 / MOVK W21,#0x8000,LSL#16.
    if struct.unpack_from('<II',data,loop-BASE)!=(0xf1007abf,0x54ffff43):raise ValueError('original FCB limit differs')
    if struct.unpack_from('<II',data,error-BASE)!=(0x52812035,0x72b00015):raise ValueError('original FCB exhaustion code differs')
    return dict(name='camera.card.streams.a',limit=30,exhausted='0x80000901',
        call_chain=[dict(call=hex(a),target=hex(b)) for a,b in chain],loop=hex(loop),error=hex(error),
        coverage='partial; original callers and raw native calls untracked',free_capacity_known=False)

def load(target,firmware):
    if target not in PROFILES:raise ValueError('unknown platform profile')
    p=PROFILES[target];path=Path(firmware)
    if path.stat().st_size!=p['bytes']:raise ValueError('wrong original application length')
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=p['sha256']:raise ValueError('wrong original firmware identity')
    verify_page_namespace(p,data)
    verify_ui_condition_abi(target,data)
    verify_card_capacity(target,data)
    return p,data

def generate(target,firmware,output):
    p,data=load(target,firmware);out=Path(output)
    if not out.resolve().is_relative_to(ROOT) or out.exists():raise ValueError('new owned binding output required')
    out.mkdir(parents=True)
    records=[];lines=['/* Generated from a complete pinned original image. */',
                     'static const FxNamedBinding fx_target_bindings[]={']
    for name,address in p['functions'].items():
        at=address-BASE;original=data[at:at+16]
        if at<0 or len(original)!=16 or not any(original):raise ValueError('missing native function '+name)
        records.append(dict(name=name,kind='function',abi='aapcs64-u64x8',address=hex(address),original=original.hex(),version=1))
        lines.append('{'+json.dumps(name)+',FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,'+
                     hex(address)+'ull,{'+','.join(map(str,original))+'}},')
    for name,(address,size) in p['tables'].items():
        records.append(dict(name=name,kind='table',abi='bytes',address=hex(address),bytes=size,version=1,
                            original=None,note='original keyboard page; geometry only, business callbacks must be replaced' if name=='camera.ui.editor.template' else 'original native correction records' if name=='camera.video.correction.records' else 'BSS pointer storage; populated by original initialization'))
        lines.append('{'+json.dumps(name)+',FX_BIND_TABLE,FX_CALL_BYTES,1,FX_BIND_READ|FX_BIND_ESCAPE,FX_SDK_BINDING,'+
                     str(size)+','+hex(address)+'ull,{0}},')
    lines.insert(1,'#include "fenestra_native_editor.h"')
    lines.append('{"camera.ui.editor.page.build",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_editor_build,{0}},')
    records.append(dict(name='camera.ui.editor.page.build',kind='function',abi='aapcs64-u64x8',version=1,native_builtin=True,
                        origin='src/native_editor.c:fx_native_editor_build',original=None))
    lines.insert(1,'extern int64_t fx_native_irq_mailbox(uintptr_t,uintptr_t);')
    lines.append('{"rtos.irq.mailbox",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_irq_mailbox,{0}},')
    records.append(dict(name='rtos.irq.mailbox',kind='function',abi='aapcs64-u64x8',version=1,native_builtin=True,
                        origin='src/native_irq.inc:fx_native_irq_mailbox',original=None))
    lines.insert(1,'extern int64_t fx_native_event_signal(uintptr_t,uint32_t);')
    lines.append('{"rtos.event.signal",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_event_signal,{0}},')
    records.append(dict(name='rtos.event.signal',kind='function',abi='aapcs64-u64x8',version=1,native_builtin=True,
                        origin='src/native_events.inc:fx_native_event_signal',original=None))
    lines.append('{"core.context",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_context,{0}},')
    records.append(dict(name='core.context',kind='function',abi='aapcs64-u64x8',version=1,native_builtin=True,
                        origin='src/platform_native.c:fx_native_context',original=None))
    lines.append('{"core.translation",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_translation,{0}},')
    records.append(dict(name='core.translation',kind='function',abi='aapcs64-u64x8',version=1,native_builtin=True,
                        origin='src/platform_native.c:fx_native_translation',original=None))
    for name,kind,abi,cap in [('core.schedule','DEVICE','DEVICE','QUERY'),('core.critical','DEVICE','DEVICE','NATIVE'),('rtos.mappings','DEVICE','DEVICE','NATIVE'),('rtos.events','DEVICE','DEVICE','NATIVE'),('core.machine','DEVICE','DEVICE','NATIVE'),('core.memory','DEVICE','DEVICE','MEMORY'),('card.files','DEVICE','DEVICE','FILES'),('core.hooks','HOOK','EVENTS','NATIVE'),('core.tick','HOOK','EVENTS','QUERY'),('core.services','DEVICE','DEVICE','BINDING')]:
        records.append(dict(name=name,kind=kind.lower(),abi=abi.lower(),version=1,native_provider=True))
        lines.append('{'+json.dumps(name)+',FX_BIND_'+kind+',FX_CALL_'+abi+',1,0,FX_SDK_'+cap+',0,1,{0}},')
    lines.insert(1,'extern FxResult fx_account_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);')
    lines.append('{"core.resources",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_QUERY,0,(uintptr_t)fx_account_resource,{0}},')
    records.append(dict(name='core.resources',kind='device',abi='device',version=1,native_provider=True,
                        origin='src/accounting.c:fx_account_resource'))
    lines.insert(1,'extern FxResult fx_capacity_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);')
    lines.append('{"core.capacity",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_QUERY,0,(uintptr_t)fx_capacity_resource,{0}},')
    records.append(dict(name='core.capacity',kind='device',abi='device',version=1,native_provider=True,
                        origin='src/capacity.c:fx_capacity_resource'))
    if target=='zr-c110':
        # The generic registry routes model providers by the checked profile's
        # callback pointer, not a camera-specific branch in common services.
        lines+=['#ifdef FX_C110_CAPTURE_EXPERIMENT',
                '{"camera.video.configuration",FX_BIND_DEVICE,FX_CALL_DEVICE,3,FX_BIND_PROVIDER,FX_SDK_BINDING,0,(uintptr_t)fx_c110_capture_resource,{0}},',
                '#endif']
        lines.insert(1,'#ifdef FX_C110_CAPTURE_EXPERIMENT\nextern FxResult fx_c110_capture_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);\n#endif')
        records.append(dict(name='camera.video.configuration',kind='device',abi='device',version=3,
                            native_provider=True,conditional='FX_C110_CAPTURE_EXPERIMENT'))
    lines+=['#ifdef FX_NATIVE_SCENE',
            '{"camera.display.scene",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_MENU,0,(uintptr_t)fx_native_scene_resource,{0}},',
            '#endif']
    lines.insert(1,'#ifdef FX_NATIVE_SCENE\nextern FxResult fx_native_scene_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);\n#endif')
    records.append(dict(name='camera.display.scene',kind='device',abi='device',version=1,
                        native_provider=True,conditional='FX_NATIVE_SCENE'))
    lines.insert(1,'#ifdef FX_NATIVE_UI_INPUT\nextern FxResult fx_native_ui_text_input(uint32_t,const char *,uint32_t);\n#endif')
    lines+=['#ifdef FX_NATIVE_UI_INPUT',
            '{"camera.ui.input.text",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_ui_text_input,{0}},',
            '#endif']
    records.append(dict(name='camera.ui.input.text',kind='function',abi='aapcs64-u64x8',version=1,
                        native_builtin=True,conditional='FX_NATIVE_UI_INPUT',
                        origin='src/native_ui_callbacks.c:fx_native_ui_text_input',original=None))
    lines.insert(1,'#ifdef FX_NATIVE_UI_INPUT\nextern FxResult fx_ui_document_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);\n#endif')
    lines+=['#ifdef FX_NATIVE_UI_INPUT',
            '{"core.ui.document",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_MENU,0,(uintptr_t)fx_ui_document_resource,{0}},',
            '#endif']
    records.append(dict(name='core.ui.document',kind='device',abi='device',version=1,
                        native_provider=True,conditional='FX_NATIVE_UI_INPUT',
                        origin='src/ui_document.c:fx_ui_document_resource'))
    lines+=['#ifdef FX_NATIVE_TASKS',
            '{"core.tasks",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_NATIVE,0,(uintptr_t)fx_native_tasks_resource,{0}},',
            '#endif']
    lines.insert(1,'#ifdef FX_NATIVE_TASKS\nextern FxResult fx_native_tasks_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);\n#endif')
    records.append(dict(name='core.tasks',kind='device',abi='device',version=1,
                        native_provider=True,conditional='FX_NATIVE_TASKS'))
    lines+=['};', '#define FX_TARGET_IDENTITY {'+','.join(map(str,bytes.fromhex(p['sha256'])))+'}',
            '#define FX_TARGET_MODULE_IDENTITY {'+','.join(map(str,bytes.fromhex(p['module_identity'])))+'}',
            '#define FX_TARGET_MODEL '+json.dumps(p['model']), '#define FX_TARGET_VERSION '+json.dumps(p['version']),
            '#define FX_TARGET_CARD_STREAM_LIMIT 30u','#define FX_TARGET_CARD_STREAM_EXHAUSTED UINT32_C(0x80000901)']
    (out/'platform_bindings.h').write_text('\n'.join(lines)+'\n')
    result=dict(schema='fenestra.named-platform/v1',target=target,model=p['model'],version=p['version'],
                application_sha256=p['sha256'],module_identity=p['module_identity'],bindings=records,
                abi_evidence=p['abi_evidence'],native_namespace=verify_page_namespace(p,data),
                native_condition_abi=verify_ui_condition_abi(target,data),
                native_card_capacity=verify_card_capacity(target,data),
                runtime_hooks_and_devices_qualified=False,
                registered_native_providers=['core.schedule','core.resources','core.capacity','rtos.mappings','rtos.events','core.machine','core.memory','card.files','core.hooks','core.tick','core.services'])
    (out/'bindings.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
