"""Compile a C5.32 core candidate in original RX padding; no updater envelope."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess,sys
from platform_profiles import generate,load,verify_page_namespace,ROOT,BASE,native_optimization
from compiler_dependencies import dependencies,identity_configuration,finalize_identity

CODE=0x401a0000
LIMIT=0x40200000
HOOKS={0x403d6618:'fx_z9_menu_page',0x4032e57c:'fx_z9_menu_text',0x40d65a98:'fx_z9_system_dispatch',
       0x403afa44:'fx_z9_menu_navigate',0x407e2e0c:'fx_z9_ptp_lookup',
       0x407e1834:'fx_z9_ptp_session',0x4035705c:'fx_z9_ptp_operations',0x40c05714:'fx_z9_rtos_printf'}
sys.path.insert(0,str(ROOT/'emulation'))
from native_ui_dispatch_sites import call as ui_call,sites as ui_sites,verify_original as verify_ui
from native_font_sites import sites as font_sites,verify_original as verify_font
HOOKS.update(ui_sites('z9-c532'));HOOKS.update(font_sites('z9-c532'))

def build(firmware,output):
    profile,original=load('z9-c532',firmware);out=Path(output).resolve()
    verify_ui('z9-c532',original);verify_font('z9-c532',original)
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('new owned evidence output required')
    if any(original[CODE-BASE:LIMIT-BASE]):raise ValueError('original RX padding not empty')
    # Independent startup evidence is a real C532 event table, not a ZR enum.
    table=0x433ad2f0-BASE
    for event,name in ((19,b'eSyscCommandId_AppFirstStartComplete'),(7,b'eSyscCommandId_BSShutdown'),
                       (24,b'eSyscCommandId_CardMountComplete')):
        ident,pointer=struct.unpack_from('<I4xQ',original,table+event*88)
        if ident!=event or original[pointer-BASE:pointer-BASE+len(name)+1]!=name+b'\0':raise ValueError('C532 system event differs')
    out.mkdir(parents=True);generate('z9-c532',firmware,out/'bindings')
    from displaced import prepare
    displaced_inputs=prepare('z9-c532',original,out)
    tools=[shutil.which(n) for n in ('clang','ld.lld','llvm-objcopy','llvm-nm')]
    if not all(tools):raise RuntimeError('LLVM tools required')
    cc,ld,objcopy,nm=tools
    names=['src/panic.c','src/native_panic.c','src/scene.c','src/native_scene.c','src/native_display.c','src/native_vg_call.S','src/platform/z9_c532/display_profile.c','src/module_bus.c','src/native_resources.c','src/worker_wait.c','src/capacity.c','src/slot_pool.c','src/machine.c','src/hooks.c','src/native_hook_pages.c','src/module_manager.c','src/native_modules.c','src/platform.c','src/binding_store.c','src/records.c','src/video_configuration.c','src/capture_profile.c','src/native_call.c','src/native_call_a64.S','src/platform_native.c','src/native_editor.c','src/native_menu.c','src/native_ui.c','src/native_ui_routes.c','src/native_ui_pager.c','src/native_ui_editor.c','src/ui_document.c','src/native_ui_reader.c','src/text_page.c','src/native_visibility.c','src/native_title.c','src/native_ui_callbacks.c','src/native_text.c','src/native_font.c','src/text_regions.c','src/native_apps.c','src/text_fit.c','src/services.c','src/memory.c',
           'src/sha256.c','src/module.c','src/loader.c','src/card.c','src/exec.c','src/module_load_buffer.c','src/accounting.c','src/file_card.c','src/runtime.c']
    names+=['src/platform/z9_c532/'+s for s in ('menu.c','services.c','task.c','card.c','module_stage.c',
        'address_space.c','exec_context.c','module_session.c','module_worker.c','private_call.S','user_call.S','startup_trampoline.S')]
    names+=['src/ui_dispatch.c','src/native_ui_dispatch.c','src/platform/z9_c532/ui_dispatch_profile.c','src/tasks.c','src/native_tasks.c',
            'src/startup.c','src/startup_files.c','src/startup_service.c','src/platform/z9_c532/startup_files.c']
    names+=['src/ptp.c','src/ptp_device_info.c','src/native_ptp.c',
            'src/platform/z9_c532/ptp.c','src/platform/z9_c532/ptp_trampoline.S',
            'src/native_rtos.c','src/native_rtos_monitor.c','src/platform/z9_c532/rtos.c','src/platform/z9_c532/rtos_printf.S',
            'src/native_linux.c','src/transfers.c','src/platform/z9_c532/usb_debug.c']
    identity_inputs=sorted(set([p.relative_to(ROOT).as_posix() for p in displaced_inputs]+['host/displaced.py']+names+['host/build_z9_core.py','host/platform_profiles.py',
        'host/compiler_dependencies.py','src/native_rtos_printf.inc','src/native_translation_impl.h','emulation/z9_core.ld','emulation/native_ui_dispatch_sites.py','emulation/native_font_sites.py']+
        [p.relative_to(ROOT).as_posix() for p in (ROOT/'include').rglob('*.h')]+
        [p.relative_to(ROOT).as_posix() for p in (ROOT/'src/platform/z9_c532').glob('*.h')]))
    debug_configuration={}
    def write_debug_identity(inputs):
        nonlocal debug_configuration
        identity,debug_configuration=identity_configuration(ROOT,out,inputs,dict(
            target='z9-c532',application=profile['sha256'],kind='source-and-build-configuration/v2',
            compiler=subprocess.check_output([cc,'--version'],text=True)))
        def array(value):return ','.join(str(b) for b in value)
        (out/'z9_debug_identity.h').write_text(
            'static const unsigned char fx_z9_debug_build[32]={'+array(identity)+'};\n'+
            'static const unsigned char fx_z9_debug_application[32]={'+array(bytes.fromhex(profile['sha256']))+'};\n')
        return identity
    debug_identity=write_debug_identity(identity_inputs)
    identity_header=(out/'z9_debug_identity.h').relative_to(ROOT).as_posix()
    objects=[];commands=[];dependency_graph={};compilations={}
    def run(cmd):
        subprocess.run(cmd,cwd=ROOT,check=True,timeout=45);commands.append(cmd)
    for i,name in enumerate(names):
        obj=out/f'{i:02}.o';objects.append(str(obj))
        # Cold copied-message/UI code trades inlining for size inside the
        # independently checked original RX reservation. Hot paths stay O2.
        optimization=native_optimization(name)
        flags=['--target=aarch64-none-elf','-std=c11',optimization,'-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
               '-ffreestanding','-fno-builtin','-mgeneral-regs-only','-mstrict-align','-fno-stack-protector',
               '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections',
               '-DFX_FREESTANDING=1','-DFX_PLATFORM_PROFILE=1','-DFX_NATIVE_SCENE=1','-DFX_NATIVE_TASKS=1','-DFX_NATIVE_UI_INPUT=1','-I','include','-I',str(out/'bindings'),'-I',str(out)]
        if name.endswith('.S'):flags=['--target=aarch64-none-elf','-I','include','-I',str(out)]
        dep=obj.with_suffix('.d')
        command=[cc,*flags,'-MMD','-MF',str(dep),'-MT','fx_core','-c',name,'-o',str(obj)]
        run(command);compilations[name]=command
        dependency_graph[name]=dependencies(dep,ROOT)
    debug_identity,complete_identity_inputs,identity_rebuilt=finalize_identity(
        identity_inputs,dependency_graph,identity_header,debug_identity,write_debug_identity,
        lambda source:run(compilations[source]))
    elf=out/'core.elf';binary=out/'core.bin'
    run([ld,'-m','aarch64elf','-T','emulation/z9_core.ld','--gc-sections',
         *['--undefined='+name for name in [*HOOKS.values(),'fx_z9_menu_metadata','fx_z9_panic_metadata','fx_z9_startup_metadata','fx_z9_ptp_metadata','fx_native_rtos_metadata','fx_native_modules_metadata','fx_native_scene_metadata','fx_native_load_buffer_metadata','fx_native_load_owner_metadata','fx_worker_wait_metadata','fx_native_font_index_metadata','fx_native_ui_dispatch_metadata','fx_native_ui_topology_metadata','fx_native_ui_editor_metadata','fx_native_ui_reader_metadata','fx_ui_controls_metadata','fx_native_ui_pager_metadata']],*objects,'-o',str(elf)])
    run([objcopy,'-O','binary',str(elf),str(binary)])
    symbols={p[2]:int(p[0],16) for line in subprocess.check_output([nm,'--defined-only',str(elf)],text=True).splitlines() if len(p:=line.split())==3}
    extension=binary.read_bytes();candidate=bytearray(original);candidate[CODE-BASE:CODE-BASE+len(extension)]=extension
    for address,name in ((0x40d65a98,'fx_z9_system_original'),(0x403afa44,'fx_z9_navigate_original'),
                         (0x407e2e0c,'fx_z9_ptp_original_lookup'),(0x407e1834,'fx_z9_ptp_original_session'),
                         (0x4035705c,'fx_z9_ptp_original_operations'),(0x40c05714,'fx_z9_rtos_original_printf')):
        offset=symbols[name]-CODE
        if extension[offset:offset+16]!=original[address-BASE:address-BASE+16]:raise ValueError('displaced native instructions differ')
    changes=[]
    for address,name in HOOKS.items():
        delta=symbols[name]-address
        if delta%4 or not -(1<<27)<=delta<(1<<27):raise ValueError('branch out of range')
        before=original[address-BASE:address-BASE+4];after=ui_call(address,symbols[name]) if address in ui_sites('z9-c532') or address in font_sites('z9-c532') else struct.pack('<I',0x14000000|((delta//4)&0x3ffffff))
        candidate[address-BASE:address-BASE+4]=after
        changes.append(dict(address=hex(address),symbol=name,before=before.hex(),after=after.hex()))
    # Independent reconstruction: zero extension and restore every entry;
    # the entire original, including boot prefix/MMU/data, must match exactly.
    restored=bytearray(candidate);restored[CODE-BASE:CODE-BASE+len(extension)]=bytes(len(extension))
    for change in changes:
        at=int(change['address'],16)-BASE;restored[at:at+4]=bytes.fromhex(change['before'])
    if bytes(restored)!=original or candidate[:0x40000]!=original[:0x40000]:raise ValueError('unexpected firmware mutation')
    (out/'z9-core.main').write_bytes(candidate)
    source_names=[*names,*[n for group in dependency_graph.values() for n in group],
                  'host/compiler_dependencies.py','src/native_rtos_printf.inc','src/native_translation_impl.h','emulation/z9_core.ld','host/build_z9_core.py','host/platform_profiles.py','emulation/native_ui_dispatch_sites.py','emulation/native_font_sites.py',
                  *[p.relative_to(ROOT).as_posix() for p in (ROOT/'include').glob('*.h')],
                  *[p.relative_to(ROOT).as_posix() for p in (ROOT/'src/platform/z9_c532').glob('*.h')]]
    result=dict(schema='fenestra.native-core/v1',target='z9-c532',base_sha256=profile['sha256'],
        code_bytes=len(extension),code_sha256=hashlib.sha256(extension).hexdigest(),
        candidate_sha256=hashlib.sha256(candidate).hexdigest(),symbols=symbols,changes=changes,
        boot_prefix_unchanged=True,original_reconstruction_equal=True,original_mmu_unchanged=True,
        activated=False,qemu_qualified=False,flashable=False,
        native_namespace=verify_page_namespace(profile,original),navigation_hook_version=1,
        ui_dispatch_hook_version=1,font_scope_hook_version=2,
        ptp_hook_version=1,rtos_hook_version=1,ptp_capabilities=355,debug_build_identity=debug_identity.hex(),
        debug_identity_scheme=2,debug_identity_configuration=debug_configuration,
        debug_identity_header=identity_header,debug_identity_rebuilt=identity_rebuilt,
        module_load_buffer_layout=list(struct.unpack_from('<15I',extension,symbols['fx_native_load_buffer_metadata']-CODE)),
        module_load_owner_layout=list(struct.unpack_from('<3I',extension,symbols['fx_native_load_owner_metadata']-CODE)),
        worker_wait_layout=list(struct.unpack_from('<14I',extension,symbols['fx_worker_wait_metadata']-CODE)),
        native_font_index_layout=list(struct.unpack_from('<14I',extension,symbols['fx_native_font_index_metadata']-CODE)),
        native_ui_dispatch_layout=list(struct.unpack_from('<10I',extension,symbols['fx_native_ui_dispatch_metadata']-CODE)),
        native_ui_topology_layout=list(struct.unpack_from('<10I',extension,symbols['fx_native_ui_topology_metadata']-CODE)),
        ui_controls_layout=list(struct.unpack_from('<15I',extension,symbols['fx_ui_controls_metadata']-CODE)),
        native_ui_pager_layout=list(struct.unpack_from('<19I',extension,symbols['fx_native_ui_pager_metadata']-CODE)),
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sorted(set(source_names+complete_identity_inputs))},
        compiler_dependencies=dependency_graph,commands=commands)
    (out/'build.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--firmware',required=True);p.add_argument('--output-dir',required=True)
    a=p.parse_args();r=build(a.firmware,a.output_dir);print(json.dumps({k:r[k] for k in ('target','code_bytes','candidate_sha256','boot_prefix_unchanged','flashable')}))

if __name__=='__main__':main()
