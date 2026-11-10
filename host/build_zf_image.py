"""Link the frozen Zf base layout and install checked native hooks locally.

Used by build_zf_link81.py before its four Zf corrections. This command only
builds files; it does not launch an emulator or connect to a camera.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from build_zf_core import build, ROOT
from zf_c301 import BASE, EG_SIZE, EG_SHA256, SYSCTL_DISPATCH
from zf_c301_core_plan import MAPPED_CODE_VA, MAPPED_CODE_BYTES, validate_core_elf
from zf_c301_sysctl import PREFIX_SHA256, SPAN_SHA256
from zf_native_menu import MENU_HOOKS
from zf_c301_ptp_profile import PTP_ENTRIES
import sys
sys.path.insert(0, str(ROOT/'emulation'))
from native_ui_dispatch_sites import verify_original as verify_ui_sites, sites as ui_sites, call as ui_call


def sha(data):
    return hashlib.sha256(data).hexdigest()


def owner_layout_defines(module_stage_probe=False, module_executor=False):
    defines = []
    if module_stage_probe:
        defines.append('-DFX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE=1')
    if module_executor:
        defines.append('-DFX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1')
        defines.append('-DFX_ZF_MODULE_WORKERS=1')
    return defines


def branch(source, target):
    delta = target - source
    if delta & 3 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('unaligned or out-of-range branch')
    return struct.pack('<I', 0x14000000 | ((delta >> 2) & 0x03ffffff))


def patch_image(original, code, symbols, elf_entry, native_menu=False, ui_dispatch=False, native_ptp=False):
    if ui_dispatch and not native_menu:
        raise ValueError('UI dispatch requires the checked menu task capture')
    if len(original) != EG_SIZE or sha(original) != EG_SHA256:
        raise ValueError('original Zf EG identity differs')
    target = symbols.get('fx_zf_system_dispatch')
    if target is None or elf_entry != target:
        raise ValueError('ELF entry must name fx_zf_system_dispatch')
    if not 0 < len(code) <= MAPPED_CODE_BYTES or not MAPPED_CODE_VA <= target < MAPPED_CODE_VA + len(code):
        raise ValueError('code or SYSCTL symbol outside mapped RX window')
    at = MAPPED_CODE_VA - BASE
    hook = SYSCTL_DISPATCH - BASE
    if original[at:at + MAPPED_CODE_BYTES] != bytes(MAPPED_CODE_BYTES):
        raise ValueError('Zf mapped code window is not empty')
    if sha(original[hook:hook + 16]) != PREFIX_SHA256 or sha(original[hook:hook + 268]) != SPAN_SHA256:
        raise ValueError('Zf SYSCTL original instruction window differs')
    patched = bytearray(original)
    # The displaced trampoline replays all 16 original bytes, then jumps to
    # SYSCTL_DISPATCH+16. No mid-function storage hook is installed.
    changes = []
    patches = [(at, code, 'core'), (hook, branch(SYSCTL_DISPATCH, target), 'sysctl')]
    if native_menu:
        for name, spec in MENU_HOOKS.items():
            offset = spec['address'] - BASE
            if sha(original[offset:offset + spec['bytes']]) != spec['sha256']:
                raise ValueError('native menu window differs: ' + name)
            destination = symbols['fx_zf_menu_' + name]
            if not MAPPED_CODE_VA <= destination < MAPPED_CODE_VA + len(code):
                raise ValueError('menu symbol outside mapped code')
            patches.append((offset, branch(spec['address'], destination), 'menu-' + name))
    if ui_dispatch:
        verify_ui_sites('zf-c301',original[:-2])
        for address,symbol in ui_sites('zf-c301').items():
            destination=symbols[symbol]
            if not MAPPED_CODE_VA <= destination < MAPPED_CODE_VA+len(code):
                raise ValueError('UI dispatcher symbol outside mapped code')
            patches.append((address-BASE,ui_call(address,destination),'ui-'+hex(address)))
    if native_ptp:
        for name,(address,expected) in PTP_ENTRIES.items():
            offset=address-BASE
            if sha(original[offset:offset+16])!=expected:
                raise ValueError('native PTP entry window differs: '+name)
            destination=symbols['fx_zf_ptp_'+name]
            if not MAPPED_CODE_VA<=destination<MAPPED_CODE_VA+len(code):
                raise ValueError('PTP symbol outside mapped code')
            patches.append((offset,branch(address,destination),'ptp-'+name))
    for offset, after, name in patches:
        before = original[offset:offset + len(after)]
        patched[offset:offset + len(after)] = after
        changes.append(dict(name=name, offset=offset, bytes=len(after),
                            before_sha256=sha(before), after_sha256=sha(after)))
    restored = bytearray(patched)
    for change in changes:
        start, size = change['offset'], change['bytes']
        restored[start:start + size] = original[start:start + size]
    if restored != original:
        raise ValueError('unrecorded patch outside the declared windows')
    return bytes(patched), changes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--application', type=Path, required=True)
    ap.add_argument('--output-dir', type=Path, required=True, help='fresh build/ directory for objects/receipts')
    ap.add_argument('--candidate', type=Path, required=True, help='new local bulk-artifact path')
    ap.add_argument('--owner-worker', action='store_true', help='enable the Zf lifecycle consumer policy')
    ap.add_argument('--native-menu', action='store_true', help='copied Setup/Fenestra menu and text hooks')
    ap.add_argument('--ui-dispatch', action='store_true',help='checked main-loop calls only; requires native menu capture')
    ap.add_argument('--native-ptp',action='store_true',help='install checked lookup/session/operations wrappers; USB Debug stays off until local activation and consent')
    ap.add_argument('--module-stage-probe', action='store_true',
                    help='one-shot native demo-zf.wlm read/validate probe; no activation or entry call')
    ap.add_argument('--module-executor', action='store_true',
                    help='include the Zf owner executor; it remains idle until a verified private arena is bound')
    args = ap.parse_args()
    if args.ui_dispatch and not args.native_menu:
        raise ValueError('UI dispatch requires native menu')
    out = args.output_dir.resolve()
    if args.candidate.exists():
        raise ValueError('candidate output already exists')
    receipt = build(args.application, out, experimental_adapters=True, owner_worker=args.owner_worker,
                    native_menu=args.native_menu, module_stage_probe=args.module_stage_probe,
                    module_executor=args.module_executor)
    if receipt['link_audit']['unresolved_symbols']:
        raise ValueError('unresolved Core symbols')
    elf = out / 'zf-core.elf'
    cmd = ['aarch64-linux-gnu-ld', '--fatal-warnings', '-T', str(ROOT / 'emulation/zf_core.ld'),
           '--no-gc-sections', *[str(out / name) for name in receipt['objects']], '-o', str(elf)]
    subprocess.run(cmd, check=True, timeout=45)
    profile = validate_core_elf(elf, MAPPED_CODE_BYTES, fixed_address=True)
    symbols = {}
    for line in subprocess.check_output(['aarch64-linux-gnu-nm', '-n', str(elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    core = out / 'core.bin'
    subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', '--gap-fill=0', str(elf), str(core)], check=True)
    # Metadata is compiler-derived, not guessed from an older owner version.
    owner_fields = ['size', 'lifecycle', 'worker', 'phase', 'start_claimed', 'entered',
                    'steps', 'notices', 'last_word', 'finished', 'error', 'task_pointer',
                    'stack', 'delay_ticks']
    expressions = ['sizeof(FxZfOwnerStorage)', 'offsetof(FxZfOwnerStorage,lifecycle)',
                   'offsetof(FxZfOwnerStorage,worker)', 'offsetof(FxZfOwnerTask,scope.phase)']
    expressions += ['offsetof(FxZfOwnerTask,'+name+')' for name in owner_fields[4:]]
    layout_source = out / 'owner-layout.c'
    layout_source.write_text('#include "owner_storage.h"\nconst uint32_t layout[] = {'+
                             ','.join(expressions)+'};\n')
    subprocess.run([receipt['compiler_path'], '-std=c11', '-I', str(ROOT/'include'),
                    '-I', str(ROOT/'src/platform/zf_c301'), '-c', str(layout_source),
                    *owner_layout_defines(args.module_stage_probe, args.module_executor),
                    '-o', str(out/'owner-layout.o')], check=True)
    subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', '-j', '.rodata',
                    str(out/'owner-layout.o'), str(out/'owner-layout.bin')], check=True)
    layout_values = struct.unpack('<'+'I'*len(owner_fields), (out/'owner-layout.bin').read_bytes())
    dispatch_fields=['owner','size','task','last_task','submitted','completed','pending','running']
    dispatch_expr=['offsetof(FxZfOwnerStorage,ui_dispatch)','sizeof(FxNativeUiDispatch)']
    dispatch_expr+=['offsetof(FxNativeUiDispatch,'+n+')' for n in dispatch_fields[2:]]
    (out/'dispatch-layout.c').write_text('#include "owner_storage.h"\nconst uint32_t layout[]={'+','.join(dispatch_expr)+'};\n')
    subprocess.run([receipt['compiler_path'],'-std=c11','-I',str(ROOT/'include'),'-I',str(ROOT/'src/platform/zf_c301'),
        '-c',str(out/'dispatch-layout.c'),'-o',str(out/'dispatch-layout.o')],check=True)
    subprocess.run(['aarch64-linux-gnu-objcopy','-O','binary','-j','.rodata',str(out/'dispatch-layout.o'),str(out/'dispatch-layout.bin')],check=True)
    dispatch_values=struct.unpack('<8I',(out/'dispatch-layout.bin').read_bytes())
    menu_layout = None
    if args.native_menu:
        fields = ['size','ready','opened','lookups','texts','bank','setup_page','setup','core','wonder',
                  'modules_manager','activation_requested']
        expressions = ['sizeof(FxZfMenu)'] + ['offsetof(FxZfMenu,'+n+')' for n in fields[1:]]
        expressions += ['offsetof(FxZfOwnerStorage,menu)']
        (out/'menu-layout.c').write_text('#include "owner_storage.h"\n#include "menu_model.h"\nconst uint32_t layout[]={'+','.join(expressions)+'};\n')
        subprocess.run([receipt['compiler_path'], '-std=c11', '-I', str(ROOT/'include'),
                        '-I', str(ROOT/'src/platform/zf_c301'), '-c', str(out/'menu-layout.c'),
                        *owner_layout_defines(args.module_stage_probe, args.module_executor),
                        '-o', str(out/'menu-layout.o')], check=True)
        subprocess.run(['aarch64-linux-gnu-objcopy','-O','binary','-j','.rodata',
                        str(out/'menu-layout.o'),str(out/'menu-layout.bin')],check=True)
        values=struct.unpack('<'+'I'*(len(fields)+1),(out/'menu-layout.bin').read_bytes())
        menu_layout=dict(zip(fields+['owner_pointer'],values))
    module_stage_layout = None
    if args.module_stage_probe or args.module_executor:
        fields = ['owner_module_runtime_stage', 'size', 'attempted', 'result', 'module_path',
                  'digest', 'snapshot_digest', 'card_primary', 'card_cleanup',
                  'card_bytes', 'card_read_calls', 'card_verified', 'runtime_phase',
                  'card_observed_sha256',
                  'file', 'file_bytes', 'file_valid', 'file_result',
                  'loader_phase', 'module_prepared', 'module_bytes', 'entry_offset',
                  'package', 'image_raw', 'module_prepare_stage', 'module_prepare_result',
                  'module_publish_stage', 'module_publish_result', 'module_end',
                  'file_capacity', 'package_capacity']
        expressions = [
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)',
            'sizeof(FxZfModuleStage)',
            'offsetof(FxZfModuleStage,attempted)',
            'offsetof(FxZfModuleStage,result)',
            'offsetof(FxZfModuleStage,module_path)',
            'offsetof(FxZfModuleStage,digest)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,snapshot_digest)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,card_receipt)+offsetof(FxCardReceipt,primary)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,card_receipt)+offsetof(FxCardReceipt,cleanup)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,card_receipt)+offsetof(FxCardReceipt,bytes)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,card_receipt)+offsetof(FxCardReceipt,read_calls)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,card_receipt)+offsetof(FxCardReceipt,verified)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,runtime_phase)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,card_receipt)+offsetof(FxCardReceipt,observed_sha256)',
            'offsetof(FxZfOwnerStorage,module_file)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,file)+offsetof(FxFileCard,bytes)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,file)+offsetof(FxFileCard,valid)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,file)+offsetof(FxFileCard,result)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,loader)+offsetof(FxLoader,runtime)+offsetof(FxRuntime,phase)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,loader)+offsetof(FxLoader,module)+offsetof(FxModule,prepared)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,loader)+offsetof(FxLoader,module)+offsetof(FxModule,bytes)',
            'offsetof(FxZfOwnerStorage,module_runtime)+offsetof(FxZfModuleRuntime,stage)+offsetof(FxZfModuleStage,loader)+offsetof(FxLoader,module)+offsetof(FxModule,entry_offset)',
            'offsetof(FxZfOwnerStorage,module_package)',
            'offsetof(FxZfOwnerStorage,module_image_raw)',
            'offsetof(FxZfOwnerStorage,module_prepare_stage)',
            'offsetof(FxZfOwnerStorage,module_prepare_result)',
            'offsetof(FxZfOwnerStorage,module_publish_stage)',
            'offsetof(FxZfOwnerStorage,module_publish_result)',
            'sizeof(FxZfOwnerStorage)',
            'offsetof(FxZfOwnerStorage,module_file_capacity)',
            'offsetof(FxZfOwnerStorage,module_package_capacity)',
        ]
        (out/'module-layout.c').write_text(
            '#include "owner_storage.h"\nconst uint32_t layout[]={'+
            ','.join(expressions)+'};\n')
        subprocess.run([receipt['compiler_path'], '-std=c11', '-I', str(ROOT/'include'),
                        '-I', str(ROOT/'src/platform/zf_c301'),
                        *owner_layout_defines(args.module_stage_probe, args.module_executor),
                        '-c', str(out/'module-layout.c'),
                        '-o', str(out/'module-layout.o')], check=True)
        subprocess.run(['aarch64-linux-gnu-objcopy','-O','binary','-j','.rodata',
                        str(out/'module-layout.o'),str(out/'module-layout.bin')],check=True)
        values=struct.unpack('<'+'I'*len(fields),(out/'module-layout.bin').read_bytes())
        module_stage_layout=dict(zip(fields,values))
        module_stage_layout['buffers_are_pointers']=True
    original = args.application.read_bytes()
    candidate, changes = patch_image(original, core.read_bytes(), symbols,
                                      MAPPED_CODE_VA + profile['entry_offset'], args.native_menu,args.ui_dispatch,args.native_ptp)
    args.candidate.parent.mkdir(parents=True, exist_ok=True)
    args.candidate.write_bytes(candidate)
    result = dict(schema='fenestra.zf-c301-base-image/v1',
                  application=str(args.application.resolve()), application_sha256=sha(original),
                  candidate=str(args.candidate.resolve()), candidate_sha256=sha(candidate),
                  code_sha256=sha(core.read_bytes()), code_bytes=core.stat().st_size,
                  elf=profile, symbols={k:hex(v) for k,v in symbols.items()}, changes=changes,
                  hook_address=hex(SYSCTL_DISPATCH), entry_symbol='fx_zf_system_dispatch',
                  hook_target=hex(symbols['fx_zf_system_dispatch']), link_command=cmd,
                  owner_worker_policy=receipt['owner_worker_policy'],
                  module_stage_probe_policy=(dict(enabled=True,file='A:\\FENESTRA\\demo-zf.wlm',bytes=4703,
                      execution=bool(args.module_executor),mapping=False,publish=False,trigger_after_worker_steps=40,
                      execution_backend='verified-private-arena-required',
                      missing_backend_behavior='remain-bound-return-busy')
                      if args.module_stage_probe else None),
                  module_executor_policy=(dict(enabled=True,arena_required=True,
                      activation='only-after-fx_exec_live-and-fx_exec_publish',
                      missing_backend_behavior='remain-bound-no-owner-fault')
                      if args.module_executor else None),
                  native_menu_policy=receipt['native_menu_policy'], menu_layout=menu_layout,
                  module_stage_layout=module_stage_layout,
                  ui_dispatch_policy=(dict(main_loops=5,call_sites=10,task_capture='owned-page-open',
                      fixed_timer_slots=0,module_execution=False) if args.ui_dispatch else None),
                  ui_dispatch_layout=dict(zip(dispatch_fields,dispatch_values)),
                  owner_layout=dict(zip(owner_fields, layout_values)),
                  offline_patch_reversal_verified=True, official_update_rollback_verified=False,
                  runtime_qualified=False, flashable=False, camera_operations=0)
    (out / 'candidate.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('symbols','link_command')}, indent=2))


if __name__ == '__main__':
    main()
