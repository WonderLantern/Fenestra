"""Compile the frozen Zf Core layer without producing a target image.

The link81 builder preserves this base layout and applies its checked adapter
replacements afterwards. Feature macro names are retained from the port bringup
to reproduce the tested image. Compile receipts describe this build step only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from zf_c301_bindings import prepare
from zf_c301_sysctl import prepare as prepare_sysctl
from zf_c301_menu_displaced import prepare as prepare_menu
from zf_c301_ui_dispatch import prepare as prepare_ui_dispatch
from zf_c301_dispatch_profile import prepare as prepare_dispatch_profile
from zf_c301_display_profile import prepare as prepare_display_profile
from zf_c301_ui_flush_owner import prepare as prepare_ui_flush_owner
from zf_c301_rtos_profile import prepare as prepare_rtos_profile
from zf_c301_linux_profile import prepare as prepare_linux_profile
from zf_c301_card_profile import prepare as prepare_card_profile
from zf_c301_special_ui_profile import prepare as prepare_special_ui_profile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/platform/zf_c301'))
from ptp_displaced import prepare as prepare_ptp

SHARED_SOURCES = [
    'src/scene.c', 'src/native_scene.c', 'src/native_display.c', 'src/native_vg_call.S',
    'src/platform_native.c', 'src/module_bus.c', 'src/native_resources.c',
    'src/worker_wait.c', 'src/capacity.c', 'src/slot_pool.c', 'src/machine.c',
    'src/hooks.c', 'src/native_hook_pages.c', 'src/module_manager.c',
    'src/native_modules.c', 'src/platform.c', 'src/binding_store.c',
    'src/records.c', 'src/video_configuration.c', 'src/capture_profile.c',
    'src/native_call.c', 'src/native_call_a64.S', 'src/native_editor.c', 'src/native_menu.c',
    'src/native_ui.c', 'src/native_ui_routes.c', 'src/native_ui_pager.c',
    'src/native_ui_editor.c', 'src/ui_document.c', 'src/native_ui_reader.c',
    'src/text_page.c', 'src/native_visibility.c', 'src/native_title.c',
    'src/native_ui_callbacks.c', 'src/native_text.c', 'src/native_font.c',
    'src/text_regions.c', 'src/native_apps.c', 'src/text_fit.c',
    'src/services.c', 'src/memory.c', 'src/sha256.c', 'src/module.c',
    'src/loader.c', 'src/card.c', 'src/exec.c', 'src/module_load_buffer.c',
    'src/accounting.c', 'src/file_card.c', 'src/runtime.c',
    'src/native_rtos.c', 'src/native_rtos_monitor.c', 'src/native_linux.c',
    'src/ptp.c', 'src/ptp_device_info.c', 'src/native_ptp.c',
    'src/transfers.c', 'src/tasks.c', 'src/native_tasks.c', 'src/startup.c',
    'src/startup_files.c', 'src/startup_service.c', 'src/ui_dispatch.c',
    'src/native_ui_dispatch.c', 'src/panic.c', 'src/native_panic.c',
]

EXPERIMENTAL_SOURCES = [
    ('src/platform/zf_c301/menu_setup.c', ('FX_ZF_EXPERIMENTAL_MENU_SETUP=1',)),
    ('src/platform/zf_c301/sysctl_gate.c', ('FX_ZF_EXPERIMENTAL_SYSCTL_GATE=1', 'FX_ZF_EXPERIMENTAL_OWNER_BOOTSTRAP=1')),
    ('src/platform/zf_c301/lifecycle.c', ('FX_ZF_EXPERIMENTAL_LIFECYCLE=1',)),
    ('src/platform/zf_c301/owner_storage.c', ('FX_ZF_EXPERIMENTAL_OWNER_STORAGE=1',)),
    ('src/platform/zf_c301/owner_publish.c', ('FX_ZF_EXPERIMENTAL_OWNER_PUBLISH=1',)),
    ('src/platform/zf_c301/owner_native.c', ('FX_ZF_EXPERIMENTAL_OWNER_NATIVE=1',)),
    ('src/platform/zf_c301/owner_task.c', ('FX_ZF_EXPERIMENTAL_OWNER_TASK=1',)),
    ('src/platform/zf_c301/owner_fault.c', ('FX_ZF_EXPERIMENTAL_OWNER_FAULT=1',)),
    ('src/platform/zf_c301/rtos_profile.c', ('FX_ZF_EXPERIMENTAL_RTOS_PROFILE=1',)),
    ('src/platform/zf_c301/linux_profile.c', ('FX_ZF_EXPERIMENTAL_LINUX_PROFILE=1',)),
    ('src/platform/zf_c301/apps_flush.c', ('FX_ZF_EXPERIMENTAL_APPS_FLUSH=1',)),
    ('src/platform/zf_c301/loaded_modules.c', ('FX_ZF_EXPERIMENTAL_LOADED_MODULES=1',)),
    ('src/platform/zf_c301/apps_domain.c', ('FX_ZF_EXPERIMENTAL_APPS_DOMAIN=1',)),
    ('src/platform/zf_c301/task.c', ('FX_ZF_C301_EXPERIMENTAL_TASK=1',)),
    ('src/platform/zf_c301/ptp.c', ('FX_ZF_C301_PTP_EXPERIMENT=1',)),
    ('src/platform/zf_c301/ptp_admission.c', ('FX_ZF_EXPERIMENTAL_PTP_ADMISSION=1',)),
    ('src/platform/zf_c301/services.c', ('FX_ZF_C301_EXPERIMENTAL_SERVICES=1',)),
    ('src/platform/zf_c301/card.c', ()),
    ('src/platform/zf_c301/module_stage.c', ()),
    ('src/platform/zf_c301/module_runtime.c', ('FX_ZF_EXPERIMENTAL_MODULE_RUNTIME=1',)),
    ('src/platform/zf_c301/exec_context.c', ('FX_ZF_EXPERIMENTAL_EXEC_CONTEXT=1',)),
    ('src/platform/zf_c301/address_space.c', ('FX_ZF_EXPERIMENTAL_ADDRESS_SPACE=1',)),
    ('src/platform/zf_c301/private_call.S', ()),
    ('src/platform/zf_c301/user_call.S', ()),
    ('src/platform/zf_c301/card_native.c', ('FX_ZF_EXPERIMENTAL_CARD=1',)),
    ('src/platform/zf_c301/ui_dispatch_candidates.c', ('FX_ZF_EXPERIMENTAL_UI_DISPATCH=1',)),
    ('src/platform/zf_c301/ui_dispatch_profile.c', ('FX_ZF_EXPERIMENTAL_DISPATCH_PROFILE=1',)),
    ('src/platform/zf_c301/display_profile.c', ('FX_ZF_EXPERIMENTAL_DISPLAY_PROFILE=1',)),
    ('src/platform/zf_c301/menu_flush.c', ('FX_ZF_EXPERIMENTAL_UI_FLUSH_OWNER=1',)),
    ('src/platform/zf_c301/startup_trampoline.S', ('FX_ZF_EXPERIMENTAL_SYSCTL_GATE=1',)),
    ('src/platform/zf_c301/ptp_trampoline.S', ()),
]


def toolchain():
    gcc = shutil.which('aarch64-linux-gnu-gcc')
    if gcc:
        return gcc, 'gnu-aarch64'
    clang = shutil.which('clang')
    if clang:
        return clang, 'llvm-aarch64'
    raise RuntimeError('GNU AArch64 or LLVM cross-compiler is required')


def identity(path):
    data = Path(path).read_bytes()
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def _classify_unresolved_symbol(name):
    if name.startswith('__aarch64_'):
        return 'compiler-runtime'
    if name.startswith('fx_zf_'):
        return 'zf-lifecycle'
    if name.startswith('fx_ptp_') or name.startswith('fx_native_ptp_'):
        return 'zf-ptp-boundary'
    if name.startswith('fx_native_ui_dispatch_') or name.startswith('fx_native_ui_flush_'):
        return 'zf-ui-dispatch-boundary'
    if name.startswith('fx_native_'):
        return 'shared-native-boundary'
    return 'other-target-boundary'
def link_audit(objects, output, kind):
    """Combine objects only to enumerate unresolved target edges; never a runtime image."""
    if kind == 'gnu-aarch64':
        linker, nm = shutil.which('aarch64-linux-gnu-ld'), shutil.which('aarch64-linux-gnu-nm')
    else:
        linker, nm = shutil.which('ld.lld'), shutil.which('llvm-nm')
    result = dict(available=bool(linker and nm), relocatable_object=None,
                  unresolved_symbols=[], compiler_runtime_symbols=[], target_symbols=[],
                  runtime_linkable=False)
    if not linker or not nm:
        return result
    relocatable = Path(output) / 'core-relocatable.audit.o'
    subprocess.run([linker, '-r', '--noinhibit-exec', *objects, '-o', str(relocatable)],
                   cwd=ROOT, check=True, timeout=45)
    unresolved = sorted({line.split()[-1] for line in subprocess.check_output(
        [nm, '-u', str(relocatable)], text=True, timeout=10).splitlines() if line.split()})
    result.update(relocatable_object=relocatable.name, unresolved_symbols=unresolved,
                  compiler_runtime_symbols=[name for name in unresolved if name.startswith('__aarch64_')],
                  target_symbols=[name for name in unresolved if not name.startswith('__aarch64_')])
    return result


def build(application, output, experimental_adapters=False, owner_worker=False, native_menu=False,
          module_stage_probe=False, module_executor=False):
    if owner_worker and not experimental_adapters:
        raise ValueError('owner worker requires experimental adapters')
    if native_menu and not experimental_adapters:
        raise ValueError('native menu requires experimental adapters')
    if module_stage_probe and not owner_worker:
        raise ValueError('module stage probe requires owner worker')
    if module_executor and not owner_worker:
        raise ValueError('module executor requires owner worker')
    output = Path(output).resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('choose a new output directory below build/')
    compiler, kind = toolchain()
    output.mkdir(parents=True)
    bindings_dir = output / 'bindings'
    binding_receipt = prepare(application, bindings_dir)
    sysctl_receipt = None
    menu_receipt = None
    ptp_receipt = None
    ui_dispatch_receipt = None
    dispatch_profile_receipt = None
    display_profile_receipt = None
    ui_flush_owner_receipt = None
    rtos_profile_receipt = None
    linux_profile_receipt = None
    card_profile_receipt = None
    include_dirs = [bindings_dir]
    if experimental_adapters:
        sysctl_dir = output / 'sysctl-preimages'
        sysctl_receipt = prepare_sysctl(application, sysctl_dir)
        menu_dir = output / 'menu-preimages'
        menu_receipt = prepare_menu(application, menu_dir)
        ptp_dir = output / 'ptp-preimages'
        ptp_receipt = prepare_ptp(application, ptp_dir, allow_build=True)
        ui_dispatch_dir = output / 'ui-dispatch-preimages'
        ui_dispatch_receipt = prepare_ui_dispatch(application, ui_dispatch_dir)
        dispatch_profile_dir = output / 'dispatch-profile'
        dispatch_profile_receipt = prepare_dispatch_profile(application, dispatch_profile_dir)
        display_profile_dir = output / 'display-profile'
        display_profile_receipt = prepare_display_profile(application, display_profile_dir)
        ui_flush_dir = output / 'ui-flush-owner'
        ui_flush_owner_receipt = prepare_ui_flush_owner(application, ui_flush_dir)
        rtos_profile_dir = output / 'rtos-profile'
        rtos_profile_receipt = prepare_rtos_profile(application, rtos_profile_dir)
        linux_profile_dir = output / 'linux-profile'
        linux_profile_receipt = prepare_linux_profile(application, linux_profile_dir)
        card_profile_dir = output / 'card-profile'
        card_profile_receipt = prepare_card_profile(application, card_profile_dir)
        include_dirs.append(sysctl_dir)
        include_dirs.append(menu_dir)
        include_dirs.append(ptp_dir)
        include_dirs.append(ui_dispatch_dir)
        include_dirs.append(dispatch_profile_dir)
        include_dirs.append(display_profile_dir)
        include_dirs.append(ui_flush_dir)
        include_dirs.append(rtos_profile_dir)
        include_dirs.append(linux_profile_dir)
        include_dirs.append(card_profile_dir)
        special_ui_dir = output / 'special-ui-profile'
        prepare_special_ui_profile(application, special_ui_dir)
        include_dirs.append(special_ui_dir)
    objects = []
    commands = []
    compiler_version = subprocess.check_output([compiler, '--version'], text=True,
                                                timeout=10).splitlines()[0]
    source_specs = [(name, ()) for name in SHARED_SOURCES]
    if experimental_adapters:
        source_specs.extend(EXPERIMENTAL_SOURCES)
    if native_menu:
        source_specs.extend([
            ('src/platform/zf_c301/special_ui_profile.c', ()),
            ('src/platform/zf_c301/menu_model.c', ()),
            ('src/platform/zf_c301/menu_apps.c', ()),
            ('src/platform/zf_c301/menu_native.c', ('FX_ZF_EXPERIMENTAL_MENU_NATIVE=1',)),
            ('src/platform/zf_c301/menu_trampoline.S', ())])
        source_specs=[(name,(*defines,'FX_ZF_EXPERIMENTAL_MENU_NATIVE=1') if name=='src/platform/zf_c301/owner_task.c' else defines)
                      for name,defines in source_specs]
    if module_executor:
        source_specs.append(('src/platform/zf_c301/module_execution.c',
                             ('FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1',)))
        source_specs.extend((f'src/platform/zf_c301/{name}.c',()) for name in
                            ('module_session','module_worker','module_controller','worker_task','startup_owner'))
    for index, (name, defines) in enumerate(source_specs):
        if native_menu and name == 'src/platform/zf_c301/sysctl_gate.c':
            defines = (*defines, 'FX_ZF_EXPERIMENTAL_MENU_NATIVE=1')
            source_specs[index] = (name, defines)
        if owner_worker and name == 'src/platform/zf_c301/sysctl_gate.c':
            defines = (*defines, 'FX_ZF_EXPERIMENTAL_OWNER_WORKER=1')
            source_specs[index] = (name, defines)
        if module_stage_probe:
            # owner_storage.h conditionally changes FxZfOwnerStorage's size and
            # offsets. Every translation unit in this experimental image must
            # see the same definition, especially allocation/publication code.
            defines = (*defines, 'FX_ZF_EXPERIMENTAL_MODULE_STAGE_PROBE=1')
            source_specs[index] = (name, defines)
        if module_executor and name == 'src/platform/zf_c301/owner_task.c':
            defines = (*defines, 'FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1')
            source_specs[index] = (name, defines)
        if module_executor:
            defines = (*defines, 'FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1')
            source_specs[index] = (name, defines)
        if module_executor and name in ('src/platform/zf_c301/owner_storage.c',
                                       'src/platform/zf_c301/owner_publish.c',
                                       'src/platform/zf_c301/owner_native.c'):
            defines = (*defines, 'FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR=1')
            source_specs[index] = (name, defines)
        source = ROOT / name
        obj = output / f'{index:02d}.o'
        dep = output / f'{index:02d}.d'
        flags = [compiler]
        if kind == 'llvm-aarch64':
            flags.append('--target=aarch64-none-elf')
        if source.suffix == '.S':
            flags += ['-DFX_FREESTANDING=1', *['-D' + define for define in defines],
                      '-I', str(ROOT / 'include'), *sum((['-I', str(path)] for path in include_dirs), []), '-MMD', '-MF', str(dep),
                      '-MT', 'fx_zf_core_shared', '-c', str(source), '-o', str(obj)]
        else:
            flags += ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wshadow',
                      '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-pic',
                      '-mgeneral-regs-only', '-mstrict-align', '-mno-outline-atomics',
                      '-fno-stack-protector', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
                      '-ffunction-sections', '-fdata-sections', '-DFX_FREESTANDING=1',
                      '-DFX_PLATFORM_PROFILE=1', *(['-DFX_ZF_MODULE_WORKERS=1'] if module_executor else ['-DFX_MODULE_SINGLE_OWNER=1']), '-DFX_NATIVE_UI_INPUT=1', '-DFX_NATIVE_TASKS=1', '-DFX_NATIVE_SCENE=1',
                      *['-D' + define for define in defines], '-I', str(ROOT / 'include'),
                      *sum((['-I', str(path)] for path in include_dirs), []), '-MMD', '-MF', str(dep), '-MT', 'fx_zf_core_shared',
                      '-c', str(source), '-o', str(obj)]
        subprocess.run(flags, cwd=ROOT, check=True, timeout=45)
        commands.append(flags)
        objects.append(str(obj))
    link_result = link_audit(objects, output, kind)
    result = dict(schema='fenestra.zf-c301-core-compile/v1', target='zf-c301',
                  kind='shared-core-compile-only', compiler=kind,
                  compiler_path=compiler, compiler_version=compiler_version,
                  application=binding_receipt['application_sha256'],
                  binding_receipt='bindings/bindings.json', source_count=len(source_specs),
                  shared_source_count=len(SHARED_SOURCES), experimental_adapters=experimental_adapters,
                  native_menu_policy=(dict(setup=672, stock_rows=47, copied_rows=49,
                      module_execution=module_executor) if native_menu else None),
                  owner_worker_policy=(dict(priority=27, native_mode=1, delay_ticks=100,
                      module_execution=module_executor, camera_policy_qualified=False,
                      module_stage_probe=module_stage_probe,
                      module_executor=module_executor) if owner_worker else None),
                  sysctl_receipt=sysctl_receipt, menu_receipt=menu_receipt,
                  ptp_receipt=ptp_receipt,
                  ui_dispatch_receipt=ui_dispatch_receipt,
                  dispatch_profile_receipt=dispatch_profile_receipt,
                  display_profile_receipt=display_profile_receipt,
                  ui_flush_owner_receipt=ui_flush_owner_receipt,
                  rtos_profile_receipt=rtos_profile_receipt,
                  linux_profile_receipt=linux_profile_receipt,
                  card_profile_receipt=card_profile_receipt,
                  link_audit=link_result,
                  sources=[dict(path=name, defines=list(defines), **identity(ROOT / name)) for name, defines in source_specs],
                  objects=[Path(path).name for path in objects], commands=commands,
                  runtime_build_available=False, linked_image_generated=False,
                  firmware_output_generated=False, runtime_qualified=False,
                  limitations=['compile receipt only; use build_zf_link81.py for the complete tested image'])
    (output / 'build.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--experimental-adapters', action='store_true',
                        help='also compile guarded Zf menu/SYSCTL/task/PTP adapter sources')
    parser.add_argument('--owner-worker', action='store_true',
                        help='Zf lifecycle consumer policy: priority 27, mode 1, delay 100 ticks')
    parser.add_argument('--module-stage-probe', action='store_true',
                        help='optional one-shot DEMO.WLM read/validate probe; never activates')
    parser.add_argument('--module-executor', action='store_true',
                        help='optional Zf owner executor: prepare, bind, activate, invoke and stop')
    args = parser.parse_args()
    print(json.dumps(build(args.application, args.output_dir, args.experimental_adapters, args.owner_worker,
                           module_stage_probe=args.module_stage_probe,
                           module_executor=args.module_executor), indent=2))


if __name__ == '__main__':
    main()
