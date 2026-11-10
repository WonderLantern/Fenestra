"""Build a preimage-checked C2.01 SYSCTL entry/return probe for emulation."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from build_z63_adapter import ROOT, load_application
from z63_placement import check as check_placement, guards as build_guards, marker_address, MARKER


def build(application, output, linker, llvm_bin, module=None, menu=False, discovery=False, sdk=False, ptp=False, runtime=False, drawing=False, ui=False):
    profile, original = load_application(application)
    base = profile['image_base']
    hook = profile['sysctl']
    at = hook['dispatcher'] - base
    displaced = original[at:at + hook['hook_bytes']]
    if hashlib.sha256(displaced).hexdigest() != hook['hook_sha256']:
        raise ValueError('SYSCTL preimage differs')
    # The only displaced instructions are four STP register saves, with no PC
    # operands. Reject a changed instruction class even after identity checks.
    if any(w & 0x3a000000 != 0x28000000 for (w,) in struct.iter_unpack('<I', displaced)):
        raise ValueError('unsupported displaced instruction')
    begin, end = (profile['probe_code_region'][k] for k in ('begin', 'end'))
    placement = check_placement(profile, original)
    header = (ROOT / 'src/platform/z63_c201/core_region.h').read_text()
    if 'FX_Z63_CORE_BEGIN UINT64_C(0x%x)' % begin not in header or 'FX_Z63_CORE_END UINT64_C(0x%x)' % end not in header:
        raise ValueError('core_region.h differs from the profile code region')
    names = {'startup_sync_hook': b'eSyscCommandId_AppFirstStartCompleteSyncHook',
             'startup_complete': b'eSyscCommandId_AppFirstStartComplete',
             'shutdown': b'eSyscCommandId_BSShutdown',
             'card_remove': b'eSyscCommandId_CardRemove',
             'card_mount': b'eSyscCommandId_CardMountComplete'}
    for key, event in hook['events'].items():
        offset = hook['event_table'] - base + event * hook['event_stride']
        ident, pointer = struct.unpack_from('<I4xQ', original, offset)
        name = names[key] + b'\0'
        if ident != event or original[pointer-base:pointer-base+len(name)] != name:
            raise ValueError('SYSCTL event differs: ' + key)
    out = Path(output).resolve()
    if out.exists():
        raise ValueError('new output directory required')
    out.mkdir(parents=True)
    (out / 'z63_system_original.inc').write_text('\n'.join('.inst 0x%08x' % w for (w,) in struct.iter_unpack('<I', displaced)) + '\n')
    cc = str(Path(llvm_bin) / 'clang')
    flags = ['--target=aarch64-none-elf', '-O2', '-ffreestanding', '-fno-builtin',
             '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector',
             '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-Wall', '-Wextra', '-Werror', '-I', str(out)]
    flags += ['-DFX_FREESTANDING=1', '-ffunction-sections', '-fdata-sections', '-I', str(ROOT / 'include')]
    sources = ['src/platform/z63_c201/sysctl_probe.c', 'src/platform/z63_c201/sysctl_trampoline.S']
    if ui:
        if not runtime:raise ValueError('native UI requires the discovered worker runtime')
        flags+=['-DFX_Z63_UI=1','-DFX_NATIVE_UI_INPUT=1','-DFX_NATIVE_SCENE=1','-DFX_NATIVE_TASKS=1']
        sources+=['src/platform/z63_c201/ui.c','src/platform/z63_c201/display_profile.c','src/platform/z63_c201/manager.c','src/native_modules.c','src/module_manager.c']
        sources+=['src/native_panic.c','src/panic.c']
        sources+=['src/'+n for n in ('ui_dispatch.c','native_ui_dispatch.c','native_tasks.c','native_scene.c','native_display.c','native_vg_call.S','native_ui.c','native_ui_routes.c','native_ui_pager.c','native_ui_editor.c','native_ui_reader.c','text_page.c','ui_document.c','native_ui_callbacks.c','native_apps.c','text_regions.c','native_editor.c','native_title.c','native_visibility.c','native_text.c')]
        for row in [*profile['ui_calls'],profile['ui_condition'],*profile['ui_bindings'],*profile['display_bindings']]:
            start=row.get('file_offset',row.get('window',row['address'])-base)
            if hashlib.sha256(original[start:start+row['bytes']]).hexdigest()!=row['sha256']:raise ValueError('UI caller preimage differs')
    if drawing:
        if module is None or runtime:
            raise ValueError('drawing probe requires its pinned module experiment')
        flags += ['-DFX_Z63_DRAWING=1']
        sources += ['src/platform/z63_c201/display_profile.c','src/platform/z63_c201/draw_probe.c',
                    'src/native_display.c','src/native_vg_call.S','src/scene.c']
        for binding in profile['display_bindings']:
            at=binding['address']-base
            if hashlib.sha256(original[at:at+binding['bytes']]).hexdigest()!=binding['sha256']:
                raise ValueError('display binding differs: '+binding['name'])
    if runtime:
        if not sdk or not menu or module is not None:
            raise ValueError('runtime requires SDK/menu, and discovers packages without --module')
        flags += ['-DFX_Z63_RUNTIME=1','-DFX_Z63_MODULE_PROBE=1']
        sources += ['src/platform/z63_c201/runtime.c','src/platform/z63_c201/module_worker.c','src/platform/z63_c201/discovery.c']
        sources += ['src/platform/z63_c201/startup.c','src/startup.c','src/startup_files.c']
    if ptp:
        if not sdk or not menu:
            raise ValueError('PTP experiment requires SDK and menu')
        flags += ['-DFX_Z63_PTP=1']
        sources += ['src/ptp.c','src/ptp_device_info.c','src/native_ptp.c',
                    'src/platform/z63_c201/ptp.c','src/platform/z63_c201/ptp_trampoline.S',
                    'src/native_rtos.c','src/native_rtos_monitor.c','src/platform/z63_c201/rtos.c','src/platform/z63_c201/rtos_printf.S',
                    'src/native_linux.c','src/transfers.c','src/platform/z63_c201/usb_debug.c']
        for row in profile['debug_bindings']:
            at=row['address']-base
            if hashlib.sha256(original[at:at+row['bytes']]).hexdigest()!=row['sha256']:raise ValueError('debug binding preimage differs')
        for name, binding in profile['ptp_hooks'].items():
            start = binding['address'] - base
            span = original[start:start + binding['bytes']]
            if hashlib.sha256(span).hexdigest() != binding['sha256']:
                raise ValueError('PTP preimage differs: ' + name)
            # Each displaced window is only original saves/MOV-immediates.
            # This explicit whitelist rejects PC-relative instruction classes.
            words = struct.unpack('<4I', span)
            if any(not (w & 0x3a000000 == 0x28000000 or w & 0x3b000000 in (0x38000000,0x39000000) or
                        w & 0x1f800000 == 0x12800000 or w & 0xffe0ffe0 in (0x2a0003e0,0xaa0003e0) or w & 0xffc003ff == 0xd10003ff) for w in words):
                raise ValueError('unsupported PTP displaced instruction')
            symbol = 'fx_z63_ptp_original_' + name
            (out / (symbol + '.inc')).write_text('\n'.join('.inst 0x%08x' % w for w in words)+'\n')
        # Discovery pass uses a fixed-size placeholder; the dependency graph
        # is finalized before hashing and rebuilding every identity consumer.
        (out/'z63_debug_identity.h').write_text('static const unsigned char fx_z63_debug_build[32]={0};\n'+
            'static const unsigned char fx_z63_debug_application[32]={'+','.join(map(str,bytes.fromhex(profile['application_sha256'])))+'};\n'+
            'static const unsigned char fx_z63_debug_payload[32]={'+','.join(map(str,bytes.fromhex(profile['payload_sha256'])))+'};\n')
    if sdk:
        from z63_bindings import generate
        generate(profile, original, out)
        flags += ['-DFX_Z63_SDK=1', '-DFX_PLATFORM_PROFILE=1']
        sources += ['src/platform/z63_c201/services.c']
        sources += ['src/' + n for n in ('services.c','platform.c','binding_store.c','platform_native.c','native_resources.c','native_call.c','native_call_a64.S','machine.c','hooks.c','records.c','worker_wait.c','native_hook_pages.c','capacity.c','slot_pool.c','text_fit.c','tasks.c','scene.c','module_bus.c')]
    if discovery:
        if module is None:
            raise ValueError('discovery probe requires a module')
        flags += ['-DFX_Z63_DISCOVERY=1']
        sources += ['src/platform/z63_c201/discovery.c', 'src/services.c']
    if module is not None:
        from module_header import validate_package
        package = Path(module).read_bytes()
        validate_package(package, bytes.fromhex(profile['payload_sha256']))
        digest = hashlib.sha256(package).digest()
        (out / 'z63_probe_module.h').write_text('#define FX_Z63_PROBE_BYTES %du\nstatic const unsigned char fx_z63_probe_digest[32]={%s};\n' % (len(package), ','.join(str(x) for x in digest)))
        flags += ['-DFX_Z63_MODULE_PROBE=1']
    if module is not None or runtime:
        if not runtime:
            sources += ['src/platform/z63_c201/module_probe.c']
        sources += ['src/platform/z63_c201/' + name for name in ('module_session.c', 'module_stage.c', 'address_space.c', 'exec_context.c', 'task.c', 'card.c', 'private_call.S', 'user_call.S')]
        sources += ['src/' + name for name in ('memory.c', 'sha256.c', 'module.c', 'loader.c', 'card.c', 'exec.c', 'runtime.c', 'file_card.c', 'module_load_buffer.c', 'accounting.c')]
    hooks = {hook['dispatcher']: 'fx_z63_system_dispatch'}
    if menu:
        if module is None and not runtime:
            raise ValueError('menu test requires its pinned test module')
        flags += ['-DFX_Z63_MENU=1']
        sources += ['src/platform/z63_c201/menu.c', 'src/platform/z63_c201/menu_trampoline.S',
                    'src/platform/z63_c201/lifecycle.c', 'src/native_menu.c', 'src/native_font.c']
        for name, binding in profile['menu_hooks'].items():
            start = binding['address'] - base
            span = original[start:start + binding['bytes']]
            if hashlib.sha256(span).hexdigest() != binding['sha256']:
                raise ValueError('menu preimage differs: ' + name)
            hooks[binding['address']] = 'fx_z63_menu_' + name
            if name == 'navigate':
                (out / 'z63_navigate_original.inc').write_text('\n'.join('.inst 0x%08x' % word for (word,) in struct.iter_unpack('<I', span)) + '\n')
    sources = list(dict.fromkeys(sources))
    if ptp:
        hooks.update({binding['address']: 'fx_z63_ptp_' + name for name,binding in profile['ptp_hooks'].items()})
    if ui:
        hooks.update({row['address']:row['symbol'] for row in profile['ui_calls']})
    from compiler_dependencies import dependencies, identity_configuration, finalize_identity
    objects=[];graph={};compilations={}
    for index,name in enumerate(sources):
        obj=out/('%02d.o'%index);dep=out/('%02d.d'%index)
        command=[cc,*[f for f in flags if not(name.endswith('.S') and f=='-mgeneral-regs-only')],
                 '-MMD','-MF',str(dep),'-MT','fx_core','-c',str(ROOT/name),'-o',str(obj)]
        subprocess.run(command,check=True,cwd=ROOT)
        graph[name]=dependencies(dep,ROOT);compilations[name]=command;objects.append(str(obj))
    identity_inputs=[*sources,'host/build_z63_probe.py','host/build_z63_adapter.py','host/z63_bindings.py',
                     'host/compiler_dependencies.py','host/verify_z63_build.py','host/z63_package.py','host/z63_placement.py',
                     'fenestra.py','targets/z63-c201.json','emulation/z63_probe.ld']
    header=(out/'z63_debug_identity.h').relative_to(ROOT).as_posix()
    configuration={}
    def emit(inputs):
        nonlocal configuration
        digest,configuration=identity_configuration(ROOT,out,inputs,dict(target='z63-c201',
            application=profile['application_sha256'],module=hashlib.sha256(Path(module).read_bytes()).hexdigest() if module else None,
            flags=[f.replace(str(out),'generated').replace(str(ROOT),'.') for f in flags],
            compiler=subprocess.check_output([cc,'--version'],text=True),
            linker=subprocess.check_output([str(linker),'--version'],text=True)))
        if ptp:
            (out/'z63_debug_identity.h').write_text('static const unsigned char fx_z63_debug_build[32]={'+','.join(map(str,digest))+'};\n'+
                'static const unsigned char fx_z63_debug_application[32]={'+','.join(map(str,bytes.fromhex(profile['application_sha256'])))+'};\n'+
                'static const unsigned char fx_z63_debug_payload[32]={'+','.join(map(str,bytes.fromhex(profile['payload_sha256'])))+'};\n')
        return digest
    identity,identity_inputs,rebuilt=finalize_identity(identity_inputs,graph,header,bytes(32),emit,
        lambda name:subprocess.run(compilations[name],check=True,cwd=ROOT))
    (out/'identity.json').write_text(json.dumps(configuration,indent=2)+'\n')
    elf = out / 'probe.elf'
    subprocess.run([str(linker), '-m', 'aarch64elf', '--gc-sections', *['--undefined=' + name for name in [*hooks.values(),*(['fx_z63_startup_layout','fx_z63_worker_layout'] if runtime else []),*(['fx_z63_panic_layout','fx_z63_menu_ui_offset','fx_z63_menu_runtime_offset','fx_z63_runtime_layout','fx_z63_scene_layout','fx_native_scene_metadata','fx_z63_ui_layout','fx_z63_manager_layout','fx_native_modules_metadata','fx_native_ui_editor_metadata','fx_native_ui_reader_metadata'] if ui else [])]], '--defsym=fx_z63_core_begin=0x%x' % begin, '--defsym=fx_z63_core_end=0x%x' % end, '-T', str(ROOT / 'emulation/z63_probe.ld'), *objects, '-o', str(elf)], check=True)
    binary = out / 'probe.bin'
    subprocess.run([str(Path(llvm_bin) / 'llvm-objcopy'), '-O', 'binary', str(elf), str(binary)], check=True)
    symbols = {}
    for line in subprocess.check_output([str(Path(llvm_bin) / 'llvm-nm'), '--defined-only', str(elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    code = binary.read_bytes()
    marker = marker_address(profile)
    if not code or begin + len(code) > marker:
        raise ValueError('probe overflow')
    destination = symbols['fx_z63_system_dispatch']
    delta = destination - hook['dispatcher']
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('hook branch out of range')
    candidate = bytearray(original)
    candidate[begin-base:begin-base+len(code)] = code
    candidate[marker-base:marker-base+len(MARKER)] = MARKER
    # No hook branches straight into the core. Each goes through a guard in
    # copied original text that falls back to original behaviour when the
    # marker, and so the core, is not present.
    calls = {row['address'] for row in profile['ui_calls']} if ui else set()
    guard_code, guard_table = build_guards(profile, original, {a: (symbols[s], a in calls) for a, s in hooks.items()})
    guard_begin = profile['guard_region']['begin']
    candidate[guard_begin-base:guard_begin-base+len(guard_code)] = guard_code
    for address in hooks:
        delta = guard_table[address] - address
        if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
            raise ValueError('hook branch out of range')
        candidate[address-base:address-base+4] = struct.pack('<I', (0x94000000 if address in calls else 0x14000000) | ((delta // 4) & 0x3ffffff))
    # The emulator consumes the EG directly. No member CRC or update package
    # is emitted; retained CRC bytes remain original and must not be mistaken
    # for a package-integrity result.
    image = out / 'probe.eg.bin'
    image.write_bytes(candidate)
    # Independently reverse declared edits and prove all other bytes unchanged.
    restored = bytearray(candidate)
    restored[begin-base:begin-base+len(code)] = original[begin-base:begin-base+len(code)]
    restored[marker-base:marker-base+len(MARKER)] = original[marker-base:marker-base+len(MARKER)]
    restored[guard_begin-base:guard_begin-base+len(guard_code)] = original[guard_begin-base:guard_begin-base+len(guard_code)]
    for address in hooks:
        restored[address-base:address-base+4] = original[address-base:address-base+4]
    if restored != original:
        raise ValueError('undeclared image change')
    receipt = dict(target='z63-c201', artifact='emulator-sysctl-probe', native_ui=ui,
                   application_sha256=profile['application_sha256'],
                   candidate_sha256=hashlib.sha256(candidate).hexdigest(),
                   symbols=symbols, hook=hook, hooks={hex(a): n for a,n in hooks.items()}, menu=menu, discovery=discovery, sdk=sdk, ptp=ptp, runtime=runtime, drawing=drawing, original_reconstruction=True,
                   placement=placement, guards={hex(a): hex(s) for a, s in guard_table.items()},
                   firmware_package=False, runtime_qualified=False, do_not_flash=True,
                   compiler=subprocess.check_output([cc, '--version'], text=True).splitlines()[0],
                   linker=subprocess.check_output([str(linker), '--version'], text=True).splitlines()[0],
                   build_identity=identity.hex(),identity_consumers_rebuilt=rebuilt,
                   sources={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in identity_inputs
                            if not (ROOT/name).resolve().is_relative_to(out)},
                   generated={Path(name).relative_to(out.relative_to(ROOT)).as_posix():hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                              for name in identity_inputs if (ROOT/name).resolve().is_relative_to(out)},
                   module_sha256=hashlib.sha256(Path(module).read_bytes()).hexdigest() if module is not None else None)
    (out / 'probe.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt
