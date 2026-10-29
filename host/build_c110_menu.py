"""Compile native C1.10 Fenestra hooks; no update envelope is produced.

Test-only storage and fault variants remain explicitly isolated. Native capture
builds require separate exact-image release evidence before packaging. The
official-image builder, not an analysis capture, supplies the update body.
"""
import hashlib
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path
from compiler_dependencies import dependencies,identity_configuration,finalize_identity
from platform_profiles import native_optimization

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'emulation'))
BASE, CODE = 0x40160000, 0x401a0000
SOURCE_SHA = "7d81293b5a793f034f15c31554e0ca7b552847da8d15055eec33541c3a509957"
HOOKS = {0x403638e4:"fx_c110_ptp_operations",0x40e05710:"fx_c110_rtos_printf",0x403e876c:"fx_c110_menu_page",0x4033a66c:"fx_c110_menu_text",
         0x40838be0:"fx_c110_ptp_lookup",0x40837610:"fx_c110_ptp_session",0x40f760e4:"fx_c110_system_dispatch",
         0x403bba78:"fx_c110_menu_navigate"}
PTP_PREIMAGES = {1089954020: ('fx_c110_system_original', '90f0a19094d99d64fd16a94f4a89813b33637eea1a3d08c22a65f1f7368de5ec'), 1082362848: ('fx_c110_ptp_original_lookup', 'f47b89bdeedc4a5c216c5c1bd833b83f57d1b2f232b335e8ed4cfbbd6f2d2477'), 1077295332: ('fx_c110_ptp_original_operations', 'de1533bb4fe111cba176aba3e901507841132aefd0bb24e99ca4d220a9c53152'), 1088444176: ('fx_c110_rtos_original_printf', '585fb7ac1c615998370691881324ac9f1c8fa418cf389a678404477742e2351b'), 1082357264: ('fx_c110_ptp_original_session', 'df6cd9a80a4454c33b19866700c6daaf13cf823ce204d6565a21959ed055c079'), 1077656184: ('fx_c110_navigate_original', 'b977c8ce11c04e2613c89387b4aecbe8ecb14caa0cef71eacd4c08ac254563a5')}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware',type=Path,required=True,help='pinned original C1.10 application component')
    parser.add_argument('--output-dir',default='build/native-menu-build',
        help='isolated build directory within this Fenestra workspace')
    parser.add_argument('--demo-frames',type=int,default=450,help='bounded test duration; full demo acceptance requires450')
    parser.add_argument('--native-capture',action='store_true',
        help='link native C1.10 capture bindings; release still requires exact-image evidence')
    parser.add_argument('--sensor-trace',action='store_true',
        help='link bounded passive native sensor submit/return tracing; no active sensor commands')
    parser.add_argument('--event-trace',action='store_true',help='fixed consumed-input/native event templates; implies --sensor-trace')
    parser.set_defaults(qemu_test=False,capture_experiment=False,native_file_test_backend=False,fault_case=None,fault_module_dir='',fault_on_shooting=False)
    args=parser.parse_args()
    if args.event_trace:args.sensor_trace=True
    if args.native_capture and (args.capture_experiment or args.qemu_test):
        parser.error('native capture and isolated experiment builds are distinct')
    capture=args.capture_experiment or args.native_capture
    if args.capture_experiment and not args.qemu_test:
        parser.error('unqualified capture binding requires --qemu-test')
    if (args.native_file_test_backend or args.fault_case) and not args.qemu_test:
        parser.error('test storage/fault injection requires --qemu-test')
    if args.fault_on_shooting and not args.fault_case:parser.error('shooting fault requires an explicit fault component')
    if not 1<=args.demo_frames<=450:parser.error('demo frames must be1..450')
    from target_c110 import load_profile
    load_profile()
    from platform_profiles import load,generate
    firmware=args.firmware
    _,official=load('zr-c110',firmware)
    original=official
    source_kind='official-c110-eg'
    source_sha=hashlib.sha256(original).hexdigest()
    if source_kind=='analysis-capture' and source_sha!=SOURCE_SHA:raise ValueError('C1.10 analysis image pin differs')
    from inspect_c110_display_abi import inspect
    display_abi=inspect(original)
    from inspect_c110_file_abi import inspect as inspect_files
    file_abi=inspect_files(original)
    out=(ROOT/args.output_dir).resolve()
    if not out.is_relative_to(ROOT) or out==ROOT:
        raise ValueError('build output must remain inside the Fenestra workspace')
    out.mkdir(parents=True,exist_ok=False)
    platform=generate('zr-c110',firmware,out/'bindings')
    from displaced import prepare
    displaced_inputs=prepare('zr-c110',original,out)
    from native_ui_dispatch_sites import call as ui_call,sites as ui_sites,verify_original as verify_ui
    verify_ui('zr-c110',official)
    from native_font_sites import sites as font_sites,verify_original as verify_font
    verify_font('zr-c110',official)
    temporary=out/'compiler-temp'
    temporary.mkdir(exist_ok=True)
    for name in ('TEMP','TMP','TMPDIR'):os.environ[name]=str(temporary)
    from target_c110 import PACKAGE_SHA256,require_module_target
    package_path=ROOT/(args.fault_module_dir+'/'+args.fault_case+'/faulty.wlm') if args.fault_case else None
    if package_path and not package_path.resolve().is_relative_to(ROOT):raise ValueError('module source must stay inside Fenestra')
    package=package_path.read_bytes() if package_path else b''
    fault_case=None
    if args.fault_case:
        if args.output_dir=='build/native-menu-build':
            raise ValueError('fault tests require a separate --output-dir')
        fault_case=json.loads((package_path.parent/'module-case.json').read_text())
        if (fault_case['fault_case']!=args.fault_case or fault_case['target_version']!='C 1.10' or
            fault_case['module_sha256']!=hashlib.sha256(package).hexdigest() or
            any(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h for p,h in fault_case['source_sha256'].items())):
            raise ValueError('fault component source/package identity differs')
    if package:require_module_target(package)
    if package and not 128<len(package)<=16384:
        raise ValueError('bounded demo card module required')
    digest=hashlib.sha256(package).digest() if package else bytes(32)
    def array(value):return ','.join(str(v) for v in value)
    (out/'c110_demo_config.h').write_text(
        '#define FX_C110_MODULE_PATH "'+('FENESTRA/faulty.wlm' if fault_case else 'Fenestra core')+'"\n'+
        '#define FX_C110_NATIVE_MODULE_PATH "A:\\\\FENESTRA\\\\'+('faulty.wlm' if fault_case else 'demo.wlm')+'"\n'+
        '#define FX_C110_DEMO_PACKAGE_SIZE '+str(len(package))+'u\n'+
        '#define FX_C110_DEMO_FRAME_LIMIT '+str(args.demo_frames)+'u\n'+
        'static const unsigned char fx_c110_demo_package_sha[32]={'+array(digest)+'};\n'+
        'static const unsigned char fx_c110_demo_firmware_sha[32]={'+array(bytes.fromhex(PACKAGE_SHA256))+'};\n')
    clang,linker,objcopy,nm=(shutil.which(n) for n in ("clang","ld.lld","llvm-objcopy","llvm-nm"))
    if not all((clang,linker,objcopy,nm)):
        raise RuntimeError("LLVM compiler/linker/binutils required")
    sources=["src/module_bus.c","src/native_resources.c","src/worker_wait.c","src/capacity.c","src/slot_pool.c","src/machine.c","src/hooks.c","src/native_hook_pages.c","src/module_manager.c","src/native_modules.c","src/services.c","src/platform.c","src/binding_store.c","src/native_call.c","src/native_call_a64.S","src/platform_native.c","src/native_editor.c","src/native_menu.c","src/native_ui.c","src/native_ui_routes.c","src/native_ui_pager.c","src/native_ui_editor.c","src/ui_document.c","src/native_ui_reader.c","src/text_page.c","src/native_text.c","src/native_font.c","src/text_regions.c","src/native_apps.c","src/text_fit.c","src/native_visibility.c","src/native_title.c","src/native_ui_callbacks.c","src/platform/zr_c110/services.c","src/startup.c","src/platform/zr_c110/startup.c",
             "src/startup_files.c","src/platform/zr_c110/startup_files.c","src/platform/zr_c110/startup_trampoline.S","src/platform/zr_c110/menu.c","src/settings.c","src/debug.c","src/runtime.c",
             "src/ptp.c","src/ptp_device_info.c","src/native_ptp.c","src/platform/zr_c110/ptp.c","src/platform/zr_c110/ptp_device_info.c","src/platform/zr_c110/ptp_trampoline.S",
             "src/native_linux.c", "src/transfers.c", "src/platform/zr_c110/usb_debug.c",
             "src/platform/zr_c110/rtos_debug.c","src/native_rtos.c","src/native_rtos_monitor.c",
             "src/observation.c","src/platform/zr_c110/observation.c",
             "src/platform/zr_c110/rtos_shell.c","src/platform/zr_c110/rtos_printf.S",
             "src/platform/zr_c110/demo_worker.c","src/platform/zr_c110/private_call.S",
             "src/platform/zr_c110/user_call.S",
             "src/platform/zr_c110/display.c","src/scene.c","src/platform/zr_c110/scene.c",
             "src/native_display.c","src/native_scene.c","src/native_vg_call.S","src/platform/zr_c110/display_profile.c",
             "src/platform/zr_c110/panic.c","src/panic.c","src/native_panic.c",
             "src/platform/zr_c110/vg_call.S",
             "src/module_load_buffer.c","src/accounting.c","src/file_card.c",
             "src/loader.c","src/exec.c","src/module.c","src/memory.c","src/sha256.c","src/card.c",
             "src/text.c","src/platform/zr_c110/text.c"]
    if fault_case:sources.append('modules/card_test/menu.c')
    hooks=dict(HOOKS)
    ui_hooks=ui_sites('zr-c110');font_hooks=font_sites('zr-c110');hooks.update(ui_hooks);hooks.update(font_hooks)
    sources+=['src/ui_dispatch.c','src/native_ui_dispatch.c','src/platform/zr_c110/ui_dispatch_profile.c','src/tasks.c','src/native_tasks.c']
    generated_headers=[(out/'bindings/platform_bindings.h').relative_to(ROOT).as_posix()];host_inputs=['host/build_c110_menu.py','host/inspect_c110_display_abi.py','host/inspect_c110_file_abi.py','host/target_c110.py','targets/nikon-zr.json','host/compiler_dependencies.py','host/platform_profiles.py','host/native_binding_inventory.py','emulation/c110_menu.ld','emulation/native_ui_dispatch_sites.py','emulation/native_font_sites.py']
    from c110_menu_image import SENSOR_TRACE_HOOKS,SENSOR_TRACE_PREIMAGES,direct_branch
    if args.sensor_trace:
        sources += ['src/platform/zr_c110/sensor_trace.c','src/platform/zr_c110/sensor_trace_native.c',
                    'src/platform/zr_c110/sensor_trace_gate.S']
        hooks.update(SENSOR_TRACE_HOOKS)
        for address,before in SENSOR_TRACE_PREIMAGES.items():
            if original[address-BASE:address-BASE+4]!=before:raise ValueError('sensor trace preimage differs')
    event_hooks={};event_link=[]
    if args.event_trace:
        from c110_event_sites import HOOKS as event_hooks,emit as emit_events,link_args as event_link_args
        assembly,header=emit_events(official)
        event_link=event_link_args(assembly)
        generated=out/'event_gates.S';generated.write_text(assembly)
        config=out/'c110_event_config.h';config.write_text(header);generated_headers.append(config.relative_to(ROOT).as_posix())
        sources += ['src/platform/zr_c110/event_trace.c','src/platform/zr_c110/event_trace_native.c',
                    'src/platform/zr_c110/frame_debug.c',generated.relative_to(ROOT).as_posix()]
        host_inputs.append('host/c110_event_sites.py');hooks.update(event_hooks)
    if capture:
        from c110_capture_trampolines import HOOKS as CAPTURE_HOOKS, RUNTIME_HOOKS, emit
        generated=out/'capture_trampolines.S'
        generated.write_text(emit(official,runtime=True))
        sources += ['src/capture_profile.c','src/records.c','src/video_configuration.c','src/platform/zr_c110/capture_projection.c',
                    'src/platform/zr_c110/capture_hooks.c','src/platform/zr_c110/capture_menu.c',
                    'src/platform/zr_c110/capture_quick.c',
                    generated.relative_to(ROOT).as_posix()]
        hooks.update({address:'fx_c110_capture_'+name for name,(address,_) in
                      {**CAPTURE_HOOKS,**RUNTIME_HOOKS}.items()})
        for address,expected in {**CAPTURE_HOOKS,**RUNTIME_HOOKS}.values():
            if hashlib.sha256(original[address-BASE:address-BASE+16]).hexdigest()!=expected:
                raise ValueError('capture entry differs in native analysis image')
    if args.native_file_test_backend:
        sources += ['emulation/pcie_nvme.c','emulation/c110_file_card.c','src/exfat.c']
    local_headers=[p.relative_to(ROOT).as_posix() for p in (ROOT/'src/platform/zr_c110').glob('*.h')]
    identity_inputs=sorted(set([p.relative_to(ROOT).as_posix() for p in displaced_inputs]+['host/displaced.py']+sources+local_headers+generated_headers+host_inputs+['host/build_c110_menu.py']+
        [p.relative_to(ROOT).as_posix() for p in (ROOT/'include').glob('*.h')]))
    debug_configuration={}
    def write_debug_identity(inputs):
        nonlocal debug_configuration
        debug_identity,debug_configuration=identity_configuration(ROOT,out,inputs,{
            'module_sha256':digest.hex(),'input_sha256':source_sha,
            'compiler':subprocess.check_output([clang,'--version'],text=True).strip(),
            'configuration':{'capture':bool(capture),'native_capture':args.native_capture,
                'qemu_test':args.qemu_test,'native_file_test_backend':args.native_file_test_backend,
                'fault_case':args.fault_case,'fault_on_shooting':args.fault_on_shooting,
                'demo_frames':args.demo_frames,'sensor_trace':args.sensor_trace,'event_trace':args.event_trace},
            'kind':'source-and-build-configuration/v2'})
        from target_c110 import PACKAGE_SHA256
        (out/'c110_debug_identity.h').write_text(
            'static const unsigned char fx_c110_debug_identity[32]={'+array(debug_identity)+'};\n'+
            'static const unsigned char fx_c110_debug_base[32]={'+array(bytes.fromhex(
                '606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542'))+'};\n'+
            'static const unsigned char fx_c110_debug_package[32]={'+array(bytes.fromhex(PACKAGE_SHA256))+'};\n')
        return debug_identity
    debug_identity=write_debug_identity(identity_inputs)
    commands=[];objects=[];dependency_graph={};compilations={}
    identity_header=(out/'c110_debug_identity.h').relative_to(ROOT).as_posix()
    def run(command):
        subprocess.run(command,cwd=ROOT,check=True,timeout=30)
        commands.append(command)
    for source in sources:
        obj=out / (source.replace('/','_').replace('.','_')+".o")
        flags=[clang,"--target=aarch64-none-elf","-std=c11",native_optimization(source),"-Wall","-Wextra","-Werror",
             "-Wconversion","-Wshadow","-ffreestanding","-fno-builtin","-mgeneral-regs-only",
             "-mstrict-align","-fno-stack-protector","-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables","-ffunction-sections","-fdata-sections",
             "-DFX_FREESTANDING=1","-DFX_PLATFORM_PROFILE=1","-DFX_NATIVE_SCENE=1","-DFX_NATIVE_TASKS=1","-DFX_NATIVE_UI_INPUT=1","-I","include","-I",str(out),"-I",str(out/'bindings')]
        if args.native_file_test_backend:flags += ['-DFX_NATIVE_FILE_TEST_BACKEND=1','-I','emulation']
        if args.qemu_test:flags += ['-DFX_C110_QEMU_TEST=1']
        if capture:flags += ['-DFX_C110_CAPTURE_EXPERIMENT=1']
        if args.sensor_trace:flags += ['-DFX_C110_SENSOR_TRACE=1']
        if args.event_trace:flags += ['-DFX_C110_EVENT_TRACE=1']
        if fault_case:flags += ['-DFX_C110_FAULT_TEST=1']
        if args.fault_on_shooting:flags += ['-DFX_C110_FAULT_ON_SHOOTING=1']
        if source.endswith('.S'):flags=[clang,"--target=aarch64-none-elf","-I","include","-I",str(out)]
        dep=obj.with_suffix('.d')
        command=[*flags,'-MMD','-MF',str(dep),'-MT','fx_core',"-c",source,"-o",str(obj)]
        run(command);compilations[source]=command
        dependency_graph[source]=dependencies(dep,ROOT)
        objects.append(str(obj))
    # Generated identity cannot hash itself. Discover actual transitive includes
    # through the compiler, then rebuild only units that consume that identity.
    debug_identity,complete_identity_inputs,identity_rebuilt=finalize_identity(
        identity_inputs,dependency_graph,identity_header,debug_identity,write_debug_identity,
        lambda source:run(compilations[source]))
    compiled_inputs=sorted({name for group in dependency_graph.values() for name in group})
    elf=out / "menu.elf"
    run([linker,"-m","aarch64elf","-T","emulation/c110_menu.ld","--gc-sections",
         "--undefined=fx_c110_menu_page",
         "--undefined=fx_c110_rtos_printf","--undefined=fx_native_rtos_metadata",
         "--undefined=fx_c110_menu_layout","--undefined=fx_c110_system_dispatch",
         "--undefined=fx_c110_startup_metadata","--undefined=fx_c110_ptp_owner_metadata",
         "--undefined=fx_c110_ptp_lookup","--undefined=fx_c110_ptp_session",
         "--undefined=fx_c110_ptp_metadata",
         "--undefined=fx_c110_observation_metadata",
         "--undefined=fx_c110_demo_metadata",
         "--undefined=fx_c110_debug_metadata",
         "--undefined=fx_c110_demo_display_metadata",
         "--undefined=fx_c110_demo_text_metadata",
         "--undefined=fx_c110_storage_metadata",
         "--undefined=fx_c110_panic_metadata",
         "--undefined=fx_c110_isolation_metadata",
         "--undefined=fx_c110_preemption_metadata","--undefined=fx_c110_simd_metadata","--undefined=fx_c110_services_metadata",
         "--undefined=fx_c110_scene_metadata",
         "--undefined=fx_c110_shooting_metadata",
         *(['--undefined=fx_c110_sensor_trace_metadata','--undefined=fx_c110_sensor_trace_owner_metadata',
             '--undefined=fx_c110_sensor_trace_pre','--undefined=fx_c110_sensor_trace_post',
             '--defsym=fx_c110_sensor_trace_native_spi=0x40eafab4',
             '--defsym=fx_c110_sensor_trace_native_return=0x413522ac',
             '--defsym=fx_c110_sensor_trace_native_after_cmp=0x413522b0'] if args.sensor_trace else []),
         *event_link,*(['--undefined=fx_c110_event_trace_metadata','--undefined=fx_c110_event_trace_owner_metadata',
            '--undefined=fx_c110_frame_sample_metadata',
            *['--undefined='+n for n in event_hooks.values()]] if args.event_trace else []),
         *['--undefined='+name for name in sorted(set(hooks.values()))],
         *(['--undefined=fx_c110_capture_menu_metadata'] if capture else []),
         *(['--undefined=fx_c110_file_test_metadata'] if args.native_file_test_backend else []),
         "--undefined=fx_c110_menu_text","--undefined=fx_c110_menu_metadata",
         '--undefined=fx_native_modules_metadata','--undefined=fx_c110_menu_ui_metadata','--undefined=fx_c110_manager_ui_metadata',
         '--undefined=fx_native_load_buffer_metadata','--undefined=fx_native_load_owner_metadata','--undefined=fx_worker_wait_metadata','--undefined=fx_native_font_index_metadata','--undefined=fx_native_ui_dispatch_metadata','--undefined=fx_native_ui_topology_metadata','--undefined=fx_native_ui_editor_metadata','--undefined=fx_native_ui_reader_metadata','--undefined=fx_ui_controls_metadata','--undefined=fx_native_ui_pager_metadata',*objects,"-o",str(elf)])
    binary=out / "menu.bin"
    run([objcopy,"-O","binary","--only-section=.text","--only-section=.rodata",str(elf),str(binary)])
    symbols={}
    for line in subprocess.check_output([nm,"--defined-only",str(elf)],text=True).splitlines():
        values=line.split()
        if len(values)==3:symbols[values[2]]=int(values[0],16)
    extension=binary.read_bytes()
    for address,(trampoline,expected_hex) in PTP_PREIMAGES.items():
        expected=original[address-BASE:address-BASE+16]
        if hashlib.sha256(expected).hexdigest()!=expected_hex:raise ValueError('displaced instructions hash differs')
        offset=symbols[trampoline]-CODE
        if original[address-BASE:address-BASE+16]!=expected or extension[offset:offset+16]!=expected:
            raise ValueError('C1.10 PTP displaced instructions differ')
    # The existing pinned padding capacity is independent of trace features.
    # Larger generic SDK builds must not force unrelated debugger hooks on.
    reserved=0x60000 if args.sensor_trace or len(extension)>0x20000 else 0x20000
    if not extension or len(extension)>reserved:
        raise ValueError("bounded native menu experiment required")
    patched=bytearray(original)
    if any(original[CODE-BASE:CODE-BASE+reserved]):
        raise ValueError('pinned emulator ROM reservation is not empty')
    patched[CODE-BASE:CODE-BASE+len(extension)]=extension
    changes=[]
    changes.append({'address':hex(CODE),'symbol':'emulator_reserved_menu_rom',
                    'before':original[CODE-BASE:CODE-BASE+len(extension)].hex(),'after':extension.hex()})
    for address,name in hooks.items():
        offset=address-BASE
        narrow=address in SENSOR_TRACE_HOOKS or address in event_hooks or address in ui_hooks or address in font_hooks
        width=4 if narrow else 16
        before=bytes(patched[offset:offset+width])
        after=ui_call(address,symbols[name]) if address in ui_hooks or address in font_hooks else direct_branch(address,symbols[name]) if narrow else struct.pack("<IIQ",0x58000050,0xd61f0200,symbols[name])
        patched[offset:offset+width]=after
        changes.append({"address":hex(address),"symbol":name,"before":before.hex(),"after":after.hex()})
    restored=bytearray(patched[:len(original)])
    for change in changes:
        at=int(change["address"],16)-BASE
        restored[at:at+len(bytes.fromhex(change['before']))]=bytes.fromhex(change["before"])
    if restored!=original:
        raise ValueError("menu/PTP patch changed other original bytes")
    path=out / "c110-menu.snapshot"
    path.write_bytes(patched)
    manifest={"target_version":"C 1.10","scope":"compiled C1.10 core; independent validation required",
              "source_kind":source_kind,"application_image_from_runtime_capture":source_kind=="analysis-capture",
              "compile_input":{"path":str(firmware.resolve()),
                               "sha256":source_sha,"bytes":len(original)},
              "flashable":False,"physical_camera_accessed":False,"base_sha256":source_sha,
              "original_bytes":len(original),"sha256":hashlib.sha256(patched).hexdigest(),
              "file":path.name,"code_address":hex(CODE),"code_bytes":len(extension),
              "changes":changes,"symbols":{k:hex(v) for k,v in symbols.items()},
              "source_sha256":{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+[
                  'include/fenestra_c110_demo.h','include/fenestra_c110_display.h','include/fenestra_c110_panic.h',
                  'include/fenestra_interrupt.h','include/fenestra_fault_test.h','include/fenestra_file_card.h',
                  'include/fenestra_startup.h','include/fenestra_c110_startup.h','include/fenestra_c110_usb.h',
                  'include/fenestra_c110_rtos.h',
                  'include/fenestra_text.h','include/fenestra_plugin_menu.h','include/fenestra_ptp.h']+
                  [p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'include').glob('*.h'))]+local_headers+generated_headers+host_inputs+compiled_inputs+complete_identity_inputs},
              "compiler_dependencies":dependency_graph,
              "commands":commands,"original_boot_instructions_unchanged":patched[:0x1a58]==original[:0x1a58],
              "original_bytes_preserved_except_menu_interfaces":False,
              "original_bytes_preserved_except_declared_hooks":True,
              "ptp_hook_version":2,"startup_hook_version":1,"rtos_hook_version":1,"navigation_hook_version":1,
              "native_namespace":platform['native_namespace'],
              "native_binding_inventory_version":2,
              "module_discovery_version":1 if fault_case else 2,"module_selection":"native directory .wlm / .wlm.disabled; entry ABI from header; no catalog file",
              "module_entry_abi":0 if fault_case else 1,"core_module_pin":bool(fault_case),
              "observation_service_version":1,"debug_build_identity":debug_identity.hex(),
              "sensor_trace_hook_version":1 if args.sensor_trace else 0,
              "event_trace_hook_version":1 if args.event_trace else 0,
              "ui_dispatch_hook_version":1,"font_scope_hook_version":2,
              "debug_build_identity_kind":"source-and-build-configuration/v2",
              "debug_identity_scheme":2,"debug_identity_configuration":debug_configuration,
              "debug_identity_header":identity_header,"debug_identity_rebuilt":identity_rebuilt,
              "capture_hook_version":3 if capture else 0,
              "card_module_file":"faulty.wlm" if fault_case else None,
              "card_module_source":package_path.relative_to(ROOT).as_posix() if package_path else None,
              "card_module_sha256":hashlib.sha256(package).hexdigest() if package else None,
              "emulator_rom_reservation":{"start":hex(CODE),"bytes":reserved,
                  "original_content":"all zero component alignment padding","physical_firmware_allocation_qualified":False},
              "qualification":{"native_entry":False,"activate":False,"card_module":False,"demo":False}}
    metadata_offset=symbols['fx_c110_menu_metadata']-CODE
    manifest['menu_metadata']=list(struct.unpack_from('<8I',extension,metadata_offset))
    manifest['module_manager_layout']=list(struct.unpack_from('<22I',extension,symbols['fx_native_modules_metadata']-CODE))
    manifest['native_ui_layout']=list(struct.unpack_from('<9I',extension,symbols['fx_c110_menu_ui_metadata']-CODE))
    manifest['native_manager_ui_layout']=list(struct.unpack_from('<5I',extension,symbols['fx_c110_manager_ui_metadata']-CODE))
    manifest['module_load_buffer_layout']=list(struct.unpack_from('<15I',extension,symbols['fx_native_load_buffer_metadata']-CODE))
    manifest['module_load_owner_layout']=list(struct.unpack_from('<3I',extension,symbols['fx_native_load_owner_metadata']-CODE))
    manifest['worker_wait_layout']=list(struct.unpack_from('<14I',extension,symbols['fx_worker_wait_metadata']-CODE))
    manifest['native_font_index_layout']=list(struct.unpack_from('<14I',extension,symbols['fx_native_font_index_metadata']-CODE))
    manifest['native_ui_dispatch_layout']=list(struct.unpack_from('<10I',extension,symbols['fx_native_ui_dispatch_metadata']-CODE))
    manifest['native_ui_topology_layout']=list(struct.unpack_from('<10I',extension,symbols['fx_native_ui_topology_metadata']-CODE))
    manifest['ui_controls_layout']=list(struct.unpack_from('<15I',extension,symbols['fx_ui_controls_metadata']-CODE))
    manifest['native_ui_pager_layout']=list(struct.unpack_from('<19I',extension,symbols['fx_native_ui_pager_metadata']-CODE))
    if capture:
        manifest['capture_menu_metadata']=list(struct.unpack_from('<11I',extension,symbols['fx_c110_capture_menu_metadata']-CODE))
        manifest['capture_qualification']={'native_menu_selection':False,'live_recording':False,
            'physical_camera':False,'release_allowed':False}
    manifest['ptp_metadata']=list(struct.unpack_from('<8I',extension,symbols['fx_c110_ptp_metadata']-CODE))
    manifest['ptp_capabilities']={'transport':'original C1.10 vendor PTP',
        'implemented':['hello','asynchronous status','binary echo','retained-result fetch',
            'out-of-band job status/cancel/release (0x95e3)',
            'worker diagnostics','module region inventory','bounded module region read',
            'normal Linux IPC bridge','retained Linux agent requests','native RTOS register/translation/RAM monitor'],
        'linux_agent_handshake_required':True,
        'linux_agent_qualification':'separate syscall/shared-loop reports required; physical IPC unverified',
        'linux_shell':'external agent after verified runtime handshake',
        'rtos_shell':True,'native_call':True,
        'memory_read_write':'RTOS direct native addresses without region admission; Linux process_vm via agent',
        'physical_usb_verified':False,'device_info_advertisement':False,
        'local_enable_required':True,'disconnect_hook_runtime_verified':False}
    manifest['demo_metadata']=list(struct.unpack_from('<7I',extension,symbols['fx_c110_demo_metadata']-CODE))
    manifest['debug_metadata']=list(struct.unpack_from('<10I',extension,symbols['fx_c110_debug_metadata']-CODE))
    manifest['display_metadata']=list(struct.unpack_from('<7I',extension,symbols['fx_c110_demo_display_metadata']-CODE))
    manifest['scene_metadata']=list(struct.unpack_from('<18I',extension,symbols['fx_c110_scene_metadata']-CODE))
    manifest['demo_frame_limit']=args.demo_frames
    manifest['panic_metadata']=list(struct.unpack_from('<14I',extension,symbols['fx_c110_panic_metadata']-CODE))
    manifest['isolation_metadata']=list(struct.unpack_from('<10I',extension,symbols['fx_c110_isolation_metadata']-CODE))
    shooting_version=struct.unpack_from('<I',extension,symbols['fx_c110_shooting_metadata']-CODE+4)[0]
    manifest['shooting_metadata']=list(struct.unpack_from('<5I' if shooting_version==3 else '<4I',extension,symbols['fx_c110_shooting_metadata']-CODE))
    manifest['fault_on_shooting']=args.fault_on_shooting
    manifest['module_execution']={'exception_level':0,'kernel_state_accessible':False,
        'exchange':'read-only services; copied and validated scenes','preemption_verified':False}
    manifest['qemu_test_only']=args.qemu_test
    manifest['capture_runtime_build']='native-c110-v1' if args.native_capture else 'experiment' if capture else None
    manifest['observation_metadata']=list(struct.unpack_from('<17I',extension,symbols['fx_c110_observation_metadata']-CODE))
    if args.sensor_trace:
        manifest['sensor_trace_metadata']=list(struct.unpack_from('<11I',extension,symbols['fx_c110_sensor_trace_metadata']-CODE))
        manifest['sensor_trace_owner_metadata']=list(struct.unpack_from('<4I',extension,symbols['fx_c110_sensor_trace_owner_metadata']-CODE))
    if args.event_trace:
        manifest['event_trace_metadata']=list(struct.unpack_from('<11I',extension,symbols['fx_c110_event_trace_metadata']-CODE))
        manifest['event_trace_owner_metadata']=list(struct.unpack_from('<4I',extension,symbols['fx_c110_event_trace_owner_metadata']-CODE))
        manifest['frame_sample_metadata']=list(struct.unpack_from('<16I',extension,symbols['fx_c110_frame_sample_metadata']-CODE))
        manifest['native_frame_lease_version']=0
        manifest['native_frame_first_fault_version']=0
        manifest['native_consumer_handoff_version']=0
        manifest['native_lifecycle_journal_version']=0
        manifest['unchecked_debug_version']=1
    manifest['physical_activation_blocked']=False
    manifest['activation_policy']='local Activate or card-configured auto activation after native readiness; verified flag before activation; no Deactivate'
    manifest['internal_flash_write_policy']='core has no automatic Flash writer; EL0 MMIO unmapped; explicit privileged debug writes/calls are unrestricted'
    manifest['physical_runtime_verified']=False
    manifest['storage_backend']='C1.10 native file open/read/close; bounded snapshot before Loader validation'
    if fault_case:manifest['fault_test']=fault_case
    manifest['display_backend']='Nikon C1.10 native GUI object/list/paint service; no direct display MMIO'
    manifest['demo_surface']='original foreground composition plane; background menu chrome preserved'
    manifest['display_abi']=display_abi
    manifest['file_abi']=file_abi
    manifest['text_metadata']=list(struct.unpack_from('<5I',extension,symbols['fx_c110_demo_text_metadata']-CODE))
    manifest['storage_metadata']=list(struct.unpack_from('<5I',extension,symbols['fx_c110_storage_metadata']-CODE))
    manifest['card_not_ready_policy']='no panic before descriptor acquisition; explicit local retry; no automatic mount or controller access'
    manifest['menu_row_count']=6 if fault_case else 5
    manifest['ptp_owner_metadata']=list(struct.unpack_from('<4I',extension,symbols['fx_c110_ptp_owner_metadata']-CODE))
    manifest['startup_metadata']=list(struct.unpack_from('<8I',extension,symbols['fx_c110_startup_metadata']-CODE))
    manifest['startup_policy']={'default_auto':False,'flag':'A:\\FENESTRA\\boot.flag','config_slots':['startup.0','startup.1'],'abnormal_blocks_activation':True,'normal_shutdown_requires_quiesced':True,
        'attach_after_original_event':20,'early_ready_or_mount_cannot_initialize':True,'menu_lookup_cannot_initialize':True}
    manifest['module_simd_layout']=list(struct.unpack_from('<7I',extension,symbols['fx_c110_simd_metadata']-CODE))
    manifest['preemption_metadata']=list(struct.unpack_from('<5I',extension,symbols['fx_c110_preemption_metadata']-CODE))
    manifest['services_metadata']=list(struct.unpack_from('<12I',extension,symbols['fx_c110_services_metadata']-CODE))
    manifest['module_services']={'abi':1,'catalog':None,'discovery':'FENESTRA/*.wlm','additional_modules':4,'video_capture_transaction':bool(args.native_capture)}
    manifest['menu_layout']=list(struct.unpack_from('<12I',extension,symbols['fx_c110_menu_layout']-CODE))
    manifest['menu_organization']={'revision':2,'setup_entries':['Fenestra','WonderLantern'],
        'fenestra':['Activate','USB Debug (Off/On)','Auto activate','Shutdown guard',*(['Failsafe test'] if fault_case else []),'Back'],
        'wonderlantern':['demo','Module demo','Write text','Back'] if fault_case else ['module contributions','Back'],
        'plugin_contributions':['modules/card_test/menu.c'] if fault_case else [],
        'card_test_scope':'historical trusted fixture' if fault_case else 'independent service-ABI module',
        'dynamic_plugin_menu_registration':True}
    manifest['demo_before_fault']=bool(fault_case and args.fault_case=='software-panic' and not args.fault_on_shooting)
    manifest['fault_action_stage']=(26 if manifest['demo_before_fault'] else 24) if fault_case else None
    manifest['panic_page']='owned error scene on Nikon foreground; OK dismisses, Panic stays latched'
    manifest['software_interrupt']={'svc_immediate':'0x464e','scope':'terminal software Panic from isolated EL0 module',
        'native_vbar_restored':True,'hardware_irq_or_preemption':False}
    manifest['text_write']={'path':'A:\\FENESTRA\\fenestra.txt','replace_existing':False,
        'backend':'C1.10 native filesystem wrappers','success_requires':'close and exact readback'}
    manifest['native_file_test_backend']=args.native_file_test_backend
    if args.native_file_test_backend:
        manifest['file_test_metadata']=list(struct.unpack_from('<5I',extension,symbols['fx_c110_file_test_metadata']-CODE))
        manifest['source_sha256']['src/nvme.c']=hashlib.sha256((ROOT/'src/nvme.c').read_bytes()).hexdigest()
        manifest['file_test_backend_scope']='isolated NVMe partition device callbacks; Nikon registers/mounts/creates/writes filesystem normally; not physical HAL'
    if source_kind=='official-c110-eg':
        windows=[]
        for kind in ('display_abi','file_abi'):
            for name,window in manifest[kind]['functions'].items():
                windows.append(dict(name=kind+'/'+name,address=window['address'],bytes=window['bytes'],sha256=window['sha256']))
        from native_binding_inventory import addresses
        for address in sorted(addresses(manifest)):
            at=address-BASE
            if at<0 or at+32>len(original):raise ValueError('native call outside official EG')
            windows.append(dict(name='native-call',address=hex(address),bytes=32,sha256=hashlib.sha256(original[at:at+32]).hexdigest()))
        manifest['native_binding_windows']=windows
    (out / "build.json").write_text(json.dumps(manifest,indent=2))
    print(json.dumps({k:manifest[k] for k in ("sha256","code_bytes","flashable","qualification")},indent=2))


if __name__=="__main__":
    main()
