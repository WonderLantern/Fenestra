"""Build the complete Z8 C3.11 Fenestra application candidate from explicit bindings.

Native addresses require independent correspondence or checked Z8 UI metadata.
Unknown mappings are rejected. This builder neither executes a camera nor makes
physical-hardware qualification claims. The diagnostic prototype stays separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

from platform_profiles import ROOT, BASE, load, generate, native_optimization
from compiler_dependencies import dependencies, identity_configuration, finalize_identity
from z8_ui_profile import get_profile, verify_ui_fields, header as ui_header
from z8_full_profile import MANIFEST as NAMED_MANIFEST

TARGET = 'z8-c311'
MAIN_BYTES = 81807709
MAIN_SHA256 = '2b43a31c9de838f84b12416de4348a7004e65be07f853ec56ec2b10f3a8e3853'
CODE = 0x401a0000
LIMIT = 0x40200000
RAM_BEGIN, RAM_END = 0xa00000000, 0xa40000000
PLATFORM = 'src/platform/z8_c311_full/'
# Original Z9 identities are keys only, never execution-address fallbacks.
ENTRY_HOOKS = {
    0x403d6618: 'fx_z8_menu_page', 0x4032e57c: 'fx_z8_menu_text',
    0x40d65a98: 'fx_z8_system_dispatch', 0x403afa44: 'fx_z8_menu_navigate',
    0x407e2e0c: 'fx_z8_ptp_lookup', 0x407e1834: 'fx_z8_ptp_session',
    0x4035705c: 'fx_z8_ptp_operations', 0x40c05714: 'fx_z8_rtos_printf'}
DISPLACED = {
    0x40d65a98: ('fx_z8_system_original', 'FX_Z8_CONT_SYSTEM'),
    0x403afa44: ('fx_z8_navigate_original', 'FX_Z8_CONT_NAVIGATE'),
    0x407e2e0c: ('fx_z8_ptp_original_lookup', 'FX_Z8_CONT_PTP_LOOKUP'),
    0x407e1834: ('fx_z8_ptp_original_session', 'FX_Z8_CONT_PTP_SESSION'),
    0x4035705c: ('fx_z8_ptp_original_operations', 'FX_Z8_CONT_PTP_OPERATIONS'),
    0x40c05714: ('fx_z8_rtos_original_printf', 'FX_Z8_CONT_RTOS_PRINTF')}
SOURCES = [
    "src/panic.c",
    "src/native_panic.c",
    "src/scene.c",
    "src/native_scene.c",
    "src/native_display.c",
    "src/native_vg_call.S",
    "src/module_bus.c",
    "src/native_resources.c",
    "src/worker_wait.c",
    "src/capacity.c",
    "src/slot_pool.c",
    "src/machine.c",
    "src/hooks.c",
    "src/native_hook_pages.c",
    "src/module_manager.c",
    "src/native_modules.c",
    "src/platform.c",
    "src/binding_store.c",
    "src/records.c",
    "src/video_configuration.c",
    "src/capture_profile.c",
    "src/native_call.c",
    "src/native_call_a64.S",
    "src/platform_native.c",
    "src/native_editor.c",
    "src/native_menu.c",
    "src/native_ui.c",
    "src/native_ui_routes.c",
    "src/native_ui_pager.c",
    "src/native_ui_editor.c",
    "src/ui_document.c",
    "src/native_ui_reader.c",
    "src/text_page.c",
    "src/native_visibility.c",
    "src/native_title.c",
    "src/native_ui_callbacks.c",
    "src/native_text.c",
    "src/native_font.c",
    "src/text_regions.c",
    "src/native_apps.c",
    "src/text_fit.c",
    "src/services.c",
    "src/memory.c",
    "src/sha256.c",
    "src/module.c",
    "src/loader.c",
    "src/card.c",
    "src/exec.c",
    "src/module_load_buffer.c",
    "src/accounting.c",
    "src/file_card.c",
    "src/runtime.c",
    "src/ui_dispatch.c",
    "src/native_ui_dispatch.c",
    "src/tasks.c",
    "src/native_tasks.c",
    "src/startup.c",
    "src/startup_files.c",
    "src/startup_service.c",
    "src/ptp.c",
    "src/ptp_device_info.c",
    "src/native_ptp.c",
    "src/native_rtos.c",
    "src/native_rtos_monitor.c",
    "src/native_linux.c",
    "src/transfers.c",
    "src/platform/z8_c311_full/address_space.c",
    "src/platform/z8_c311_full/card.c",
    "src/platform/z8_c311_full/file_read.c",
    "src/platform/z8_c311_full/storage.c",
    "src/platform/z8_c311_full/display_profile.c",
    "src/platform/z8_c311_full/exec_context.c",
    "src/platform/z8_c311_full/menu.c",
    "src/platform/z8_c311_full/module_session.c",
    "src/platform/z8_c311_full/module_stage.c",
    "src/platform/z8_c311_full/module_worker.c",
    "src/platform/z8_c311_full/private_call.S",
    "src/platform/z8_c311_full/ptp.c",
    "src/platform/z8_c311_full/ptp_trampoline.S",
    "src/platform/z8_c311_full/rtos.c",
    "src/platform/z8_c311_full/rtos_printf.S",
    "src/platform/z8_c311_full/services.c",
    "src/platform/z8_c311_full/startup_files.c",
    "src/platform/z8_c311_full/startup_trampoline.S",
    "src/platform/z8_c311_full/task.c",
    "src/platform/z8_c311_full/ui_dispatch_profile.c",
    "src/platform/z8_c311_full/usb_debug.c",
    "src/platform/z8_c311_full/user_call.S"
]
KEEP = ['fx_z8_menu_metadata', 'fx_z8_panic_metadata', 'fx_z8_startup_metadata', 'fx_z8_ptp_metadata',
    'fx_z8_install_metadata', 'fx_z8_install_step_metadata', 'fx_z8_resume_metadata', 'fx_z8_resume_diagnostic_metadata', 'fx_native_rtos_metadata', 'fx_native_modules_metadata', 'fx_native_scene_metadata',
    'fx_native_load_buffer_metadata', 'fx_native_load_owner_metadata', 'fx_worker_wait_metadata',
    'fx_native_font_index_metadata', 'fx_native_ui_dispatch_metadata', 'fx_native_ui_topology_metadata',
    'fx_native_ui_editor_metadata', 'fx_native_ui_reader_metadata', 'fx_ui_controls_metadata', 'fx_native_ui_pager_metadata']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def number(value):
    if isinstance(value, bool) or not isinstance(value, (str, int)):
        raise ValueError('missing or invalid native address')
    return int(value, 0) if isinstance(value, str) else value


def branch(address, target, call=False):
    delta = target-address
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('A64 direct branch/call out of range')
    return struct.pack('<I', (0x94000000 if call else 0x14000000) | ((delta//4) & 0x3ffffff))


def reject_relative(window):
    for word, in struct.iter_unpack('<I', window):
        if (not word or word & 0x1f000000 == 0x10000000 or word & 0x7c000000 == 0x14000000 or
            word & 0xff000000 == 0x54000000 or word & 0x7e000000 in (0x34000000, 0x36000000) or
            word & 0x3b000000 == 0x18000000 or word & 0xfe000000 == 0xd6000000 or word & 0xff000000 == 0xd4000000):
            raise ValueError('unsupported displaced A64 instruction '+hex(word))


def read_correspondence(path, ui):
    raw = Path(path).read_bytes(); result = json.loads(raw)
    if result.get('schema') != 'fenestra.z8-full-correspondence/v1' or result.get('target') != TARGET:
        raise ValueError('wrong complete Z8 native correspondence schema')
    if result.get('inputs', {}).get('z8') != MAIN_SHA256:
        raise ValueError('correspondence belongs to another application')
    addresses = {}; evidence = {}
    for key, record in result.get('mappings', {}).items():
        if not isinstance(record, dict) or not record.get('evidence') or record.get('status') != 'unique_static_correspondence':
            continue
        old = number(key); new = number(record.get('address'))
        if new <= 0:
            raise ValueError('invalid recovered native address '+key)
        addresses[old] = new; evidence[old] = record
    # UI choices can intentionally select an equivalent source table/resource;
    # every such selection is independently checked against original Z8 bytes.
    for key, value in ui['address_map'].items():
        old, new = number(key), number(value)
        addresses[old] = new
        evidence[old] = {'address': hex(new), 'status': 'checked-z8-ui-metadata',
                         'evidence': {'source': 'host/z8_ui_profile.py', 'original_checked': True}}
    required = json.loads((ROOT/(PLATFORM+'required-addresses.json')).read_text())
    source_required = set()
    for source in (ROOT/PLATFORM).iterdir():
        if source.suffix in ('.c', '.h', '.S'):
            source_required.update(int(match, 16) for match in re.findall(r'FX_Z8_ADDR_([0-9a-f]+)', source.read_text()))
    declared = {number(key) for key in required['addresses']}
    if source_required != declared:
        raise ValueError('complete Z8 source address manifest is stale')
    missing = sorted((declared | set(ENTRY_HOOKS))-set(addresses))
    if missing:
        raise ValueError('unresolved complete Z8 native bindings: '+', '.join(hex(x) for x in missing))
    return addresses, evidence, raw


def system_events(original):
    names = ('eSyscCommandId_AppFirstStartComplete', 'eSyscCommandId_BSShutdown', 'eSyscCommandId_CardMountComplete',
             'eSyscCommandId_BSShutdownCancel', 'eSyscCommandId_PowerOnBoot')
    candidates = {}
    for name in names:
        literal = name.encode()+b'\0'; start = 0; found = []
        while (at := original.find(literal, start)) >= 0:
            pointer = struct.pack('<Q', BASE+at); search = 0
            while (reference := original.find(pointer, search)) >= 0:
                if reference >= 8 and reference % 8 == 0:
                    event, reserved = struct.unpack_from('<II', original, reference-8)
                    table = reference-8-event*88
                    if reserved == 0 and event < 256 and table >= 0:
                        found.append((table, event))
                search = reference+1
            start = at+1
        candidates[name] = found
    common = set(t for t, _ in candidates[names[0]])
    for name in names[1:]:
        common &= {t for t, _ in candidates[name]}
    if len(common) != 1:
        raise ValueError('original Z8 system-event table is not independently unique')
    table = common.pop(); values = [next(event for t, event in candidates[name] if t == table) for name in names]
    return {'table': hex(BASE+table), 'startup': values[0], 'shutdown': values[1], 'card_mount': values[2], 'shutdown_cancel': values[3], 'power_on': values[4],
            'record_bytes': 88, 'message_id_offset': 32, 'source': 'unique original system-event names/table'}


def build(firmware, output, *, llvm_bin=None):
    profile, original = load(TARGET, firmware)
    if len(original) != MAIN_BYTES or sha(original) != MAIN_SHA256:
        raise ValueError('complete Z8 builder requires pinned MAIN without the component CRC')
    ui = get_profile()
    ui_checks = verify_ui_fields(original)
    addresses, address_evidence, mapping_raw = read_correspondence(NAMED_MANIFEST, ui)
    events = system_events(original)
    out = Path(output).resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():
        raise ValueError('new owned build output directory required')
    if any(original[CODE-BASE:LIMIT-BASE]):
        raise ValueError('original executable reservation is not empty')
    hooks = {addresses[old]: {'symbol': symbol, 'call': False, 'kind': 'function-entry'} for old, symbol in ENTRY_HOOKS.items()}
    for record in ui['hook_records']:
        address = number(record['address'])
        if address in hooks:
            raise ValueError('native hook overlaps another entry')
        if sha(original[address-BASE:address-BASE+4]) != record['preimage_sha256']:
            raise ValueError('original UI/font preimage differs')
        hooks[address] = {'symbol': record['symbol'], 'call': True, 'kind': record['kind']}
    if len(hooks) != 37:
        raise ValueError('complete Z8 core needs exactly 8 entry, 10 UI and 19 font hooks')
    displaced_windows = {}
    for old, (label, continuation) in DISPLACED.items():
        address = addresses[old]; window = original[address-BASE:address-BASE+16]
        if len(window) != 16:
            raise ValueError('displaced window out of application bounds')
        reject_relative(window)
        displaced_windows[label] = {'address': address, 'continuation': address+16, 'macro': continuation, 'window': window}
    def tool(name):
        found = shutil.which(name, path=str(llvm_bin) if llvm_bin else None)
        if not found:
            raise RuntimeError('LLVM tool required: '+name)
        return found
    cc, ld, objcopy, nm = [tool(name) for name in ('clang', 'ld.lld', 'llvm-objcopy', 'llvm-nm')]
    out.mkdir(parents=True); generate(TARGET, firmware, out/'bindings')
    (out/'native-correspondence.input.json').write_bytes(mapping_raw)
    (out/'ui-profile.input.json').write_text(json.dumps(ui, indent=2)+'\n')
    (out/'ui-original-checks.json').write_text(json.dumps(ui_checks, indent=2)+'\n')
    (out/'z8_full_ui.h').write_text(ui_header())
    header = ['/* Explicit Z8 native values; no Z9 address fallback. */', '#ifndef FX_Z8_FULL_BINDINGS_H', '#define FX_Z8_FULL_BINDINGS_H',
              '#include "z8_full_ui.h"', '#define FX_Z8_CODE_BEGIN '+hex(CODE), '#define FX_Z8_CODE_END '+hex(LIMIT),
              '#define FX_Z8_RAM_BEGIN '+hex(RAM_BEGIN), '#define FX_Z8_RAM_END '+hex(RAM_END),
              '#define FX_Z8_LINUX_QUEUE_BLOCK_END (FX_Z8_ADDR_a0f000000 + 0x801000)',
              '#define FX_Z8_TASK_ATTRIBUTES 0u', '#define FX_Z8_TASK_PRIORITY 28u', '#define FX_Z8_TASK_PROCESSOR 1u',
              '#define FX_Z8_TASK_AFFINITY 0xffffffffu', '#define FX_Z8_MESSAGE_EVENT_OFFSET '+str(events['message_id_offset'])+'u',
              '#define FX_Z8_STARTUP_EVENT '+str(events['startup'])+'u', '#define FX_Z8_SHUTDOWN_EVENT '+str(events['shutdown'])+'u',
              '#define FX_Z8_CARD_MOUNT_EVENT '+str(events['card_mount'])+'u',
              '#define FX_Z8_SHUTDOWN_CANCEL_EVENT '+str(events['shutdown_cancel'])+'u',
              '#define FX_Z8_POWER_ON_EVENT '+str(events['power_on'])+'u',
              '#define FX_Z8_APPLICATION_IDENTITY {'+','.join(map(str,bytes.fromhex(MAIN_SHA256)))+'}']
    for old in sorted(addresses):
        header.append('#define FX_Z8_ADDR_'+format(old, 'x')+' '+hex(addresses[old]))
    for label, record in displaced_windows.items():
        header.append('#define '+record['macro']+' '+hex(record['continuation']))
        (out/(label+'.inc')).write_text('.byte '+','.join(map(str, record['window']))+'\n')
    (out/'z8_full_bindings.h').write_text('\n'.join(header+['#endif', '']))
    generated = [(out/name).relative_to(ROOT).as_posix() for name in ('z8_full_ui.h', 'z8_full_bindings.h',
        'native-correspondence.input.json', 'ui-profile.input.json', *[label+'.inc' for label in displaced_windows])]
    host_inputs = ['host/build_z8_core.py', 'host/platform_profiles.py', 'host/compiler_dependencies.py', 'host/z8_ui_profile.py',
                   NAMED_MANIFEST.resolve(strict=True).relative_to(ROOT).as_posix()]
    host_inputs.append('host/z8_full_profile.py')
    inputs = sorted(set([*SOURCES, *host_inputs, 'emulation/z8_core.ld', PLATFORM+'required-addresses.json',
        'src/native_rtos_printf.inc', 'src/native_translation_impl.h', *generated,
        *[p.relative_to(ROOT).as_posix() for p in (ROOT/'include').rglob('*.h')],
        *[p.relative_to(ROOT).as_posix() for p in (ROOT/PLATFORM).glob('*.h')],
        *[p.relative_to(ROOT).as_posix() for p in (out/'bindings').rglob('*') if p.is_file()]]))
    compiler = subprocess.check_output([cc, '--version'], text=True)
    identity_config = {}
    def emit_identity(names):
        nonlocal identity_config
        identity, identity_config = identity_configuration(ROOT, out, names, {'target': TARGET, 'kind': 'source-and-build-configuration/v2',
            'application': MAIN_SHA256, 'compiler': compiler, 'code_begin': CODE, 'code_end': LIMIT, 'profile': 'complete', 'ptp_capabilities': 355})
        array = lambda value: ','.join(map(str, value))
        (out/'z8_debug_identity.h').write_text('static const unsigned char fx_z8_debug_build[32]={'+array(identity)+'};\n'+
            'static const unsigned char fx_z8_debug_application[32]={'+array(bytes.fromhex(MAIN_SHA256))+'};\n')
        return identity
    identity = emit_identity(inputs); identity_header = (out/'z8_debug_identity.h').relative_to(ROOT).as_posix()
    objects, commands, graph, compilations = [], [], {}, {}
    def run(command):
        subprocess.run(command, cwd=ROOT, check=True, timeout=60); commands.append(command)
    for i, source in enumerate(SOURCES):
        obj = out/(str(i)+'.o'); dep = obj.with_suffix('.d'); objects.append(str(obj))
        optimization = '-Os' if source == PLATFORM+'menu.c' else native_optimization(source)
        flags = ['--target=aarch64-none-elf', '-std=c11', optimization, '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wshadow',
            '-ffreestanding', '-fno-builtin', '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector', '-fno-unwind-tables',
            '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections', '-DFX_FREESTANDING=1', '-DFX_PLATFORM_PROFILE=1',
            '-DFX_NATIVE_SCENE=1', '-DFX_Z8_C311_FULL=1', '-DFX_NATIVE_TASKS=1', '-DFX_NATIVE_UI_INPUT=1', '-I', 'include', '-I', str(out/'bindings'), '-I', str(out)]
        if source.endswith('.S'):
            flags = ['--target=aarch64-none-elf', '-I', 'include', '-I', str(out)]
        command = [cc, *flags, '-MMD', '-MF', str(dep), '-MT', 'fx_core', '-c', source, '-o', str(obj)]
        run(command); compilations[source] = command; graph[source] = dependencies(dep, ROOT)
    identity, all_inputs, identity_rebuilt = finalize_identity(inputs, graph, identity_header, identity, emit_identity, lambda source: run(compilations[source]))
    elf, binary = out/'core.elf', out/'core.bin'
    run([ld, '-m', 'aarch64elf', '--defsym=FX_Z8_CODE_BEGIN='+hex(CODE), '--defsym=FX_Z8_CODE_END='+hex(LIMIT),
        '-T', 'emulation/z8_core.ld', '--gc-sections', *['--undefined='+x for x in [*[h['symbol'] for h in hooks.values()], *KEEP]],
        *objects, '-o', str(elf)])
    run([objcopy, '-O', 'binary', str(elf), str(binary)])
    symbols = {fields[2]: int(fields[0], 16) for line in subprocess.check_output([nm, '--defined-only', str(elf)], text=True).splitlines()
               if len(fields := line.split()) == 3}
    code = binary.read_bytes()
    if not 0 < len(code) <= LIMIT-CODE:
        raise ValueError('complete core exceeds original RX reservation')
    for label, record in displaced_windows.items():
        start = symbols[label]-CODE
        if code[start:start+16] != record['window']:
            raise ValueError('linked original trampoline preimage differs')
        load_word, branch_word = struct.unpack_from('<II', code, start+16)
        literal = start+16+(((load_word >> 5) & 0x7ffff) << 2)
        if load_word & 0xff00001f != 0x58000010 or branch_word != 0xd61f0200 or struct.unpack_from('<Q', code, literal)[0] != record['continuation']:
            raise ValueError('linked original trampoline continuation differs')
    candidate = bytearray(original); candidate[CODE-BASE:CODE-BASE+len(code)] = code
    changes = []
    for address, hook in hooks.items():
        before = original[address-BASE:address-BASE+4]; after = branch(address, symbols[hook['symbol']], hook['call'])
        candidate[address-BASE:address-BASE+4] = after
        changes.append({'address': hex(address), 'symbol': hook['symbol'], 'before': before.hex(), 'after': after.hex(),
                        'kind': hook['kind'], 'branch': 'bl' if hook['call'] else 'b', 'preimage_sha256': sha(before)})
    restored = bytearray(candidate); restored[CODE-BASE:CODE-BASE+len(code)] = bytes(len(code))
    for record in changes:
        at = int(record['address'], 16)-BASE; before = bytes.fromhex(record['before']); restored[at:at+len(before)] = before
    if restored != original or candidate[:0x40000] != original[:0x40000]:
        raise ValueError('undeclared original application mutation')
    (out/'z8-core.main').write_bytes(candidate)
    result = {'schema': 'fenestra.native-core/v1', 'target': TARGET, 'profile': 'complete', 'base_sha256': MAIN_SHA256,
        'code_bytes': len(code), 'code_sha256': sha(code), 'candidate_sha256': sha(candidate), 'symbols': symbols, 'changes': changes,
        'boot_prefix_unchanged': True, 'original_reconstruction_equal': True, 'original_mmu_unchanged': True,
        'activated': False, 'qemu_qualified': False, 'physical_qualified': False, 'flashable': False,
        'navigation_hook_version': 1, 'ui_dispatch_hook_version': 1, 'font_scope_hook_version': 2, 'ptp_hook_version': 1, 'rtos_hook_version': 1,
        'ptp_capabilities': 355, 'system_events': events, 'native_namespace': ui_checks['checks'][1],
        'debug_build_identity': identity.hex(), 'debug_identity_scheme': 2, 'debug_identity_configuration': identity_config,
        'debug_identity_header': identity_header, 'debug_identity_rebuilt': identity_rebuilt, 'compiler_dependencies': graph, 'commands': commands,
        'source_sha256': {name: sha((ROOT/name).read_bytes()) for name in sorted(set(all_inputs))},
        'native_correspondence_sha256': sha(mapping_raw), 'native_abi_qualified': False,
        'named_manifest_source': NAMED_MANIFEST.relative_to(ROOT).as_posix(),
        'native_binding_evidence': {hex(key): value for key, value in address_evidence.items()},
        'trampolines': {label: {'address': hex(record['address']), 'continuation': hex(record['continuation']),
            'original': record['window'].hex(), 'sha256': sha(record['window'])} for label, record in displaced_windows.items()}}
    for name, count in {'module_load_buffer': 15, 'module_load_owner': 3, 'worker_wait': 14,
        'native_font_index': 14, 'native_ui_dispatch': 10, 'native_ui_topology': 10, 'ui_controls': 15, 'native_ui_pager': 19}.items():
        symbol = 'fx_'+('native_load_buffer' if name == 'module_load_buffer' else 'native_load_owner' if name == 'module_load_owner' else name)+'_metadata'
        result[name+'_layout'] = list(struct.unpack_from('<'+str(count)+'I', code, symbols[symbol]-CODE))
    (out/'build.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True)
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('--llvm-bin')
    args = parser.parse_args()
    result = build(args.firmware, args.output_dir, llvm_bin=args.llvm_bin)
    print(json.dumps({key: result[key] for key in ('target', 'profile', 'code_bytes', 'candidate_sha256', 'flashable')}))


if __name__ == '__main__':
    main()
