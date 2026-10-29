"""Verify declared C1.10 hooks and restore the complete pinned original image."""
import hashlib
import json
import importlib.util
from pathlib import Path

BASE=0x40160000
SOURCE_SHA="7d81293b5a793f034f15c31554e0ca7b552847da8d15055eec33541c3a509957"
HOOKS={0x403e876c:"fx_c110_menu_page",0x4033a66c:"fx_c110_menu_text",0x401a0000:"emulator_reserved_menu_rom"}
PTP_HOOKS={0x40838be0:'fx_c110_ptp_lookup',0x40837610:'fx_c110_ptp_session'}
PTP_INFO_HOOKS={0x403638e4:'fx_c110_ptp_operations'}
STARTUP_HOOKS={0x40f760e4:'fx_c110_system_dispatch'}
RTOS_HOOKS={0x40e05710:'fx_c110_rtos_printf'}
NAVIGATION_HOOKS={0x403bba78:'fx_c110_menu_navigate'}
UI_DISPATCH_HOOKS={a:'fx_native_ui_'+kind+'_native' for a,kind in (
    (0x4040f9e4,'wait'),(0x4040fa38,'poll'),(0x40412e14,'wait'),(0x40412e5c,'poll'),
    (0x40528234,'wait'),(0x40528274,'poll'),(0x40649720,'wait'),(0x406497e0,'poll'),
    (0x4064bc60,'wait'),(0x4064bcb8,'poll'))}
FONT_SCOPE_HOOKS={a+d:'fx_c110_font_'+n for a in
    (0x402f91f0,0x402f953c,0x402f9654,0x402fa504,0x402fa64c,
     0x402fa7a8,0x402fa960,0x402fabac,0x402fac68)
    for d,n in ((0,'enter'),(28,'leave'))}
CAPTURE_HOOKS={a:'fx_c110_capture_'+n for a,n in (
    (0x4125b1f8,'source'),(0x4125b758,'geometry'),(0x4125cc7c,'cpc'),
    (0x4125cca8,'raw_source'),(0x4125cd28,'raw_stored'),(0x4129ec9c,'active'),
    (0x4129d744,'nrmt'),(0x40f308bc,'file'),(0x4125d968,'large'),
    (0x4125dc88,'small'),(0x4125881c,'picture'),(0x41382a64,'picture_copy'),
    (0x4136b0dc,'sensor'),(0x41244890,'pool'),(0x40594434,'resolution'),
    (0x403fa808,'lookup'),(0x4109079c,'initialize'),(0x410908e0,'restart'),
    (0x41090a24,'build'),(0x41091c04,'job'),(0x412d2608,'properties'),(0x412d22ac,'property'))}
OFFICIAL_EG_SHA='606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542'
CAPTURE_METADATA_HOOKS={0x407063a0:'fx_c110_capture_movie_metadata'}
CAPTURE_QUICK_HOOKS={a:'fx_c110_capture_'+n for a,n in (
    (0x4050d200,'quick_template'),(0x404e769c,'quick_control'),(0x404f79dc,'quick_initialize'),
    (0x404fbc34,'quick_current'),(0x404f8bb0,'quick_commit'),(0x412d2950,'deferred'))}
SENSOR_TRACE_HOOKS={0x413522a8:'fx_c110_sensor_trace_pre',0x413522ac:'fx_c110_sensor_trace_post'}
SENSOR_TRACE_PREIMAGES={0x413522a8:bytes.fromhex('0376ed97'),0x413522ac:bytes.fromhex('1f000071')}


def direct_branch(address,target):
    delta=target-address
    if delta%4 or not -(1<<27)<=delta<(1<<27):raise ValueError('trace branch out of range')
    return (0x14000000|((delta//4)&0x3ffffff)).to_bytes(4,'little')


def reservation(manifest):
    trace=manifest.get('sensor_trace_hook_version',0)
    if trace not in (0,1):raise ValueError('unknown sensor trace hook version')
    code=manifest.get('code_bytes',0)
    if not isinstance(code,int) or isinstance(code,bool) or not 0<code<=0x60000:
        raise ValueError('extension size exceeds the independently pinned padding')
    # Allocation capacity and debugger feature selection are independent.
    # The builder already checks every byte of either original blank span.
    size=0x60000 if trace or code>0x20000 else 0x20000
    if manifest.get('emulator_rom_reservation',{}).get('bytes')!=size:
        raise ValueError('extension reservation differs')
    return size


def verify(payload,manifest_path):
    manifest=json.loads(Path(manifest_path).read_text())
    version=manifest.get('ptp_hook_version',0)
    if version not in (0,1,2):raise ValueError('unknown C1.10 PTP hook version')
    startup=manifest.get('startup_hook_version',0)
    if startup not in (0,1):raise ValueError('unknown startup hook version')
    rtos=manifest.get('rtos_hook_version',0)
    if rtos not in (0,1):raise ValueError('unknown RTOS hook version')
    navigation=manifest.get('navigation_hook_version',0)
    if navigation not in (0,1):raise ValueError('unknown native navigation hook version')
    ui_dispatch=manifest.get('ui_dispatch_hook_version',0)
    if ui_dispatch not in (0,1):raise ValueError('unknown native UI dispatch version')
    font_scope=manifest.get('font_scope_hook_version',0)
    if font_scope not in (0,1,2):raise ValueError('unknown native font scope version')
    font_hooks=dict(FONT_SCOPE_HOOKS) if font_scope else {}
    if font_scope>=2:font_hooks[0x402f9f00]='fx_c110_font_line_metrics'
    capture=manifest.get('capture_hook_version',0)
    limit=reservation(manifest)
    event=manifest.get('event_trace_hook_version',0)
    if event not in (0,1) or (event and not manifest.get('sensor_trace_hook_version')):raise ValueError('unknown or incomplete event trace version')
    event_hooks={}
    if event:
        # The isolated guest receives this file and the fixed-site registry
        # side by side. The repository registry lives in host/. Resolve the
        # known file explicitly so caller sys.path cannot change validation.
        registry=Path(__file__).with_name('c110_event_sites.py')
        if not registry.is_file():registry=Path(__file__).resolve().parents[1]/'host/c110_event_sites.py'
        spec=importlib.util.spec_from_file_location('_fenestra_fixed_c110_sites',registry)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        event_hooks=module.HOOKS
    if capture not in (0,1,2,3):raise ValueError('unknown capture hook version')
    if capture and manifest.get('qemu_test_only') is not True and manifest.get('capture_runtime_build')!='native-c110-v1':
        raise ValueError('capture runtime is not qualified for release')
    hooks={**HOOKS,**(PTP_HOOKS if version else {}),**(STARTUP_HOOKS if startup else {}),
           **(PTP_INFO_HOOKS if version>=2 else {}),
           **(RTOS_HOOKS if rtos else {}),**(NAVIGATION_HOOKS if navigation else {}),**(CAPTURE_HOOKS if capture else {}),
           **(CAPTURE_METADATA_HOOKS if capture>=2 else {}),
           **(CAPTURE_QUICK_HOOKS if capture>=3 else {}),
           **(SENSOR_TRACE_HOOKS if manifest.get('sensor_trace_hook_version') else {}),
           **(UI_DISPATCH_HOOKS if ui_dispatch else {}),**font_hooks,**event_hooks}
    kind=manifest.get('source_kind','analysis-capture')
    if kind not in ('analysis-capture','official-c110-eg'):raise ValueError('unknown C1.10 program source kind')
    source_sha=OFFICIAL_EG_SHA if kind=='official-c110-eg' else SOURCE_SHA
    source_bytes=88157349 if kind=='official-c110-eg' else 88157336
    if (manifest.get("target_version")!="C 1.10" or manifest.get("flashable") is not False or
        manifest.get("base_sha256")!=source_sha or manifest.get("original_bytes")!=source_bytes or
        manifest.get("code_address")!="0x401a0000" or
        not 0<manifest.get("code_bytes",0)<=limit or
        len(payload)!=source_bytes or
        hashlib.sha256(payload).hexdigest()!=manifest.get("sha256")):
        raise ValueError("fixed native menu experiment pin differs")
    restored=bytearray(payload);seen=set()
    for change in manifest["changes"]:
        address=int(change["address"],16)
        if address in seen or hooks.get(address)!=change["symbol"]:
            raise ValueError("only declared menu/PTP interfaces may change")
        seen.add(address);at=address-BASE
        before,after=bytes.fromhex(change["before"]),bytes.fromhex(change["after"])
        count=len(before)
        if ((address==0x401a0000 and (count!=manifest['code_bytes'] or any(before))) or
            (address!=0x401a0000 and count!=(4 if address in SENSOR_TRACE_HOOKS or address in event_hooks or address in UI_DISPATCH_HOOKS or address in font_hooks else 16)) or len(after)!=count or payload[at:at+count]!=after):
            raise ValueError("menu build patch bytes differ")
        if address in UI_DISPATCH_HOOKS or address in font_hooks:
            target=int(manifest['symbols'][{**UI_DISPATCH_HOOKS,**font_hooks}[address]],16)
            expected=(int.from_bytes(direct_branch(address,target),'little')|0x80000000).to_bytes(4,'little')
            if not 0x401a0000<=target<0x401a0000+manifest['code_bytes'] or after!=expected:raise ValueError('native UI call differs')
        if address in SENSOR_TRACE_HOOKS:
            target=int(manifest['symbols'][SENSOR_TRACE_HOOKS[address]],16)
            if (before!=SENSOR_TRACE_PREIMAGES[address] or
                not 0x401a0000<=target<0x401a0000+manifest['code_bytes'] or
                after!=direct_branch(address,target)):
                raise ValueError('sensor trace branch or original instruction differs')
        if address in event_hooks:
            target=int(manifest['symbols'][event_hooks[address]],16)
            if not 0x401a0000<=target<0x401a0000+manifest['code_bytes'] or after!=direct_branch(address,target):
                raise ValueError('fixed event branch differs')
        restored[at:at+count]=before
    if seen!=set(hooks) or hashlib.sha256(restored).hexdigest()!=source_sha:
        raise ValueError("restored C1.10 original bytes differ")
    if any(restored[0x401a0000-BASE:0x401a0000-BASE+limit]):
        raise ValueError('original extension padding differs')
    return manifest
