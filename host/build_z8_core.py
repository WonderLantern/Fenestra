"""Compile a Z8 C3.11 core candidate in the original RX padding.

v0 configuration: hooks = the ten verified UI main-loop sites
(emulation/native_ui_dispatch_sites.py:z8-c311), the sysctl dispatcher gate,
the native menu trio (page.get/text.get/navigate funnel, all pinned against
the C311 image) and the font request sites (emulation/native_font_sites.py:
z8-c311, 13 paired requests + the line-metrics probe). All Fenestra logic
stays dormant while the platform state is uninitialised, so the patched
image remains behaviour-equivalent to stock until a Fenestra page is
requested. Display restore, the render-profile cluster and the PTP/Linux/USB
bridges stay out of this build until their Z8 anchors are independently
pinned.
No updater envelope is produced here; repack.py owns the container step.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from platform_profiles import generate, load, verify_page_namespace, ROOT, BASE
from compiler_dependencies import dependencies, identity_configuration, finalize_identity
sys.path.insert(0, str(ROOT / 'emulation'))
from native_ui_dispatch_sites import sites as ui_sites, verify_original as verify_ui, call as ui_call
from native_font_sites import sites as font_sites, verify_original as verify_font

CODE = 0x401A0000
# System-event dispatcher entry (FUN_40d60ec8; event id read from message+0x20,
# see docs/validation-z8-c311.md). The 16-byte window is four stack-push STPs,
# replayed verbatim by the generated system_trampoline.S; hooking the entry
# covers every pump path that funnels through this dispatcher.
SYSTEM_HOOK = 0x40D60EC8
# Native menu trio (T12, pinned against the C311 image; see
# docs/validation-z8-c311.md): page.get FUN_403db694 computes
# root[group] + (id&0xffff)*0x60 over the page-roots table; text.get
# FUN_4032e130 serves (lang,id,&utf16,&bytes) from the text bank; navigate
# FUN_4039e810 is the 2973-site open-request funnel. page.get/text.get are
# full replacements (the stock logic is reimplemented in menu.c, so no
# trampoline is needed); navigate replays its 16-byte window (all
# position-independent: str/stp/mov pair) through navigate_trampoline.S.
MENU_PAGE_HOOK = 0x403DB694
MENU_TEXT_HOOK = 0x4032E130
MENU_NAV_HOOK = 0x4039E810
# Window pinned against the original image; the builder regenerates the
# trampoline from these bytes on every run and refuses to build on drift.
MENU_NAV_WINDOW_SHA256 = 'd62ce25104f7bed9d5177799f23a4523dcbedecfb164e3979c9d17be913784b7'
SYSTEM_CONT = SYSTEM_HOOK + 16
LIMIT = 0x40200000
TARGET = 'z8-c311'

# v0 source list: the full common set minus display/font/PTP/Linux/USB
# (capabilities unavailable). The platform layer is a Z9 reference
# transposition with Z8 addresses where independently pinned; the remaining
# Z9 reference values and their exercise status are documented in
# docs/validation-z8-c311.md (boundaries).
PLATFORM = [f'src/platform/z8_c311/{s}' for s in (
    'menu.c', 'menu_construct.c', 'menu_route.c', 'services.c', 'task.c', 'card.c', 'module_stage.c', 'module_session.c',
    'module_worker.c', 'address_space.c', 'exec_context.c', 'ui_dispatch_profile.c',
    'lifecycle.c',
    'startup_files.c', 'rtos.c', 'private_call.S', 'user_call.S', 'v0_stubs.c', 'ptp_stub.c', 'system_trampoline.S',
    'navigate_trampoline.S')]
COMMON = ['src/panic.c', 'src/native_panic.c', 'src/module_bus.c', 'src/native_resources.c',
          'src/worker_wait.c', 'src/capacity.c', 'src/slot_pool.c', 'src/machine.c',
          'src/hooks.c', 'src/native_hook_pages.c', 'src/module_manager.c', 'src/native_modules.c',
          'src/platform.c', 'src/binding_store.c', 'src/records.c', 'src/video_configuration.c',
          'src/capture_profile.c', 'src/native_call.c', 'src/native_call_a64.S',
          'src/platform_native.c', 'src/native_editor.c', 'src/native_menu.c', 'src/native_font.c', 'src/native_ui.c',
          'src/native_ui_routes.c', 'src/native_ui_pager.c', 'src/native_ui_editor.c',
          'src/ui_document.c', 'src/native_ui_reader.c', 'src/text_page.c',
          'src/native_visibility.c', 'src/native_title.c', 'src/native_ui_callbacks.c',
          'src/native_text.c', 'src/text_regions.c', 'src/native_apps.c', 'src/text_fit.c',
          'src/services.c', 'src/memory.c', 'src/sha256.c', 'src/module.c', 'src/loader.c',
          'src/card.c', 'src/exec.c', 'src/module_load_buffer.c', 'src/accounting.c',
          'src/file_card.c', 'src/runtime.c', 'src/ui_dispatch.c', 'src/native_ui_dispatch.c',
          'src/tasks.c', 'src/native_tasks.c', 'src/startup.c', 'src/startup_files.c',
          'src/startup_service.c', 'src/native_rtos.c', 'src/native_rtos_monitor.c']
COLD = {'src/platform.c', 'src/binding_store.c', 'src/native_ui.c', 'src/native_ui_routes.c',
        'src/native_ui_pager.c', 'src/native_ui_editor.c', 'src/tasks.c',
        'src/native_resources.c', 'src/worker_wait.c', 'src/capacity.c', 'src/slot_pool.c',
        'src/module_manager.c', 'src/native_modules.c', 'src/startup.c', 'src/startup_files.c',
        'src/startup_service.c', 'src/platform/z8_c311/menu.c'}
NAMES = COMMON + PLATFORM


def build(firmware, output, menu_enabled=False):
    profile, original = load(TARGET, firmware, require_condition=False)
    out = Path(output).resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('new owned evidence output required')
    verify_ui(TARGET, original)          # structural check of the ten UI main-loop sites
    if any(original[CODE - BASE:LIMIT - BASE]):
        raise ValueError('original RX padding not empty')
    # Z8 system event table startup evidence (the Z9 C5.32 table check equivalent)
    sysctl = profile['sysctl']
    table = sysctl['table'] - BASE
    for event, name in ((19, b'eSyscCommandId_AppFirstStartComplete'),
                        (7, b'eSyscCommandId_BSShutdown'),
                        (23, b'eSyscCommandId_CardRemove'),
                        (24, b'eSyscCommandId_CardMountComplete')):
        ident, pointer = struct.unpack_from('<I4xQ', original, table + event * sysctl['stride'])
        if ident != event or original[pointer - BASE:pointer - BASE + len(name) + 1] != name + b'\0':
            raise ValueError('Z8 system event table differs')
    out.mkdir(parents=True)
    generate(TARGET, firmware, out / 'bindings', require_condition=False)
    # System trampoline: replay the pinned dispatcher-entry window verbatim
    # (four stack-push STPs, no relocations) and tail-branch to the body.
    window = original[SYSTEM_HOOK - BASE:SYSTEM_HOOK + 16 - BASE]
    words = struct.unpack_from('<4I', window)
    if hashlib.sha256(window).hexdigest() != sysctl['prefix_sha256'] \
            or hashlib.sha256(original[SYSTEM_HOOK - BASE:SYSTEM_HOOK + sysctl['span_bytes'] - BASE]).hexdigest() != sysctl['span_sha256']:
        raise ValueError('Z8 system dispatch window differs')
    tramp = (
        '.text\n'
        '.global fx_z8_system_original\n'
        '.type fx_z8_system_original,%function\n'
        'fx_z8_system_original:\n'
        f'    .inst 0x{words[0]:08x}\n'
        f'    .inst 0x{words[1]:08x}\n'
        f'    .inst 0x{words[2]:08x}\n'
        f'    .inst 0x{words[3]:08x}\n'
        '    ldr x16,1f\n'
        '    br x16\n'
        '    .balign 8\n'
        f'1: .quad 0x{SYSTEM_CONT:x}\n')
    # Regenerated from the verified window on every build; the committed
    # copy matches the pinned original.
    (Path('src/platform/z8_c311') / 'system_trampoline.S').write_text(tramp)

    # Native menu navigate funnel (T12): the 16-byte entry window is
    # position-independent (str x20,[sp,#-0x10]! / stp x19,x30,[sp,#0x10] /
    # mov x20,x1 / mov w19,w0) and replays verbatim; control resumes at
    # entry+16. menu.c owns the full replacement and forwards non-Fenestra
    # pages through this trampoline with (page, context).
    nav_window = original[MENU_NAV_HOOK - BASE:MENU_NAV_HOOK + 16 - BASE]
    if hashlib.sha256(nav_window).hexdigest() != MENU_NAV_WINDOW_SHA256:
        raise ValueError('Z8 navigate funnel window differs')
    nav_words = struct.unpack_from('<4I', nav_window)
    nav_tramp = (
        '.text\n'
        '.global fx_z8_navigate_original\n'
        '.type fx_z8_navigate_original,%function\n'
        'fx_z8_navigate_original:\n'
        f'    .inst 0x{nav_words[0]:08x}\n'
        f'    .inst 0x{nav_words[1]:08x}\n'
        f'    .inst 0x{nav_words[2]:08x}\n'
        f'    .inst 0x{nav_words[3]:08x}\n'
        '    ldr x16,1f\n'
        '    br x16\n'
        '    .balign 8\n'
        f'1: .quad 0x{MENU_NAV_HOOK + 16:x}\n')
    (Path('src/platform/z8_c311') / 'navigate_trampoline.S').write_text(nav_tramp)

    import shutil
    tools = [shutil.which(n) for n in ('clang', 'ld.lld', 'llvm-objcopy', 'llvm-nm')]
    if not all(tools):
        raise RuntimeError('LLVM tools required (clang, ld.lld, llvm-objcopy, llvm-nm on PATH)')
    cc, ld, objcopy, nm = tools
    hooks = ui_sites(TARGET)
    # The sysctl gate is installed at the dispatcher entry with a plain
    # branch (not a BL): the displaced window saves the gate's resume point,
    # so the original's epilogue returns into the gate and the gate restores
    # the caller's LR.
    hooks[SYSTEM_HOOK] = 'fx_z8_system_dispatch'
    # The native menu trio: page.get/text.get are full replacements at their
    # entry (stock semantics reimplemented in menu.c), navigate keeps its
    # displaced window through navigate_trampoline.S. All three use the
    # plain-branch encoding for the same LR-preservation reason as the gate.
    verify_font(TARGET, original)      # structural check of the 13 font request sites
    hooks[MENU_PAGE_HOOK] = 'fx_z8_menu_page'
    hooks[MENU_TEXT_HOOK] = 'fx_z8_menu_text'
    hooks[MENU_NAV_HOOK] = 'fx_z8_menu_navigate'
    hooks.update(font_sites(TARGET))
    identity_inputs = sorted(set(NAMES + [
        'host/build_z8_core.py', 'host/platform_profiles.py', 'host/compiler_dependencies.py',
        'emulation/z8_core.ld', 'emulation/native_ui_dispatch_sites.py']
        + [pp.relative_to(ROOT).as_posix() for pp in (ROOT / 'include').rglob('*.h')]
        + [pp.relative_to(ROOT).as_posix() for pp in (ROOT / 'src/platform/z8_c311').glob('*.h')]))
    debug_configuration = {}
    def write_debug_identity(inputs):
        nonlocal debug_configuration
        identity, debug_configuration = identity_configuration(ROOT, out, inputs, dict(
            target=TARGET, application=profile['sha256'], kind='source-and-build-configuration/v2',
            compiler=subprocess.check_output([cc, '--version'], text=True)))
        def arr(value):
            return ','.join(str(b) for b in value)
        (out / 'z8_debug_identity.h').write_text(
            'const unsigned char fx_z8_debug_build[32]={' + arr(identity) + '};\n'
            'const unsigned char fx_z8_debug_application[32]={' + arr(bytes.fromhex(profile['sha256'])) + '};\n')
        return identity
    debug_identity = write_debug_identity(identity_inputs)
    identity_header = (out / 'z8_debug_identity.h').relative_to(ROOT).as_posix()
    objects = []
    commands = []
    dependency_graph = {}
    compilations = {}
    def run(cmd):
        subprocess.run(cmd, cwd=ROOT, check=True, timeout=60)
        commands.append(cmd)
    for i, name in enumerate(NAMES):
        obj = out / f'{i:02}.o'
        objects.append(str(obj))
        optimization = '-Os' if name in COLD else '-O2'
        flags = ['--target=aarch64-none-elf', '-std=c11', optimization, '-Wall', '-Wextra',
                 '-Werror', '-Wconversion', '-Wshadow', '-ffreestanding', '-fno-builtin',
                 '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector',
                 '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-ffunction-sections',
                 '-fdata-sections', '-DFX_FREESTANDING=1', '-DFX_PLATFORM_PROFILE=1',
                 '-DFX_NATIVE_TASKS=1', *([] if menu_enabled else ['-DFX_Z8_HEADLESS=1']), '-I', 'include', '-I', str(out / 'bindings'), '-I', str(out)]
        if name.endswith('.S'):
            flags = ['--target=aarch64-none-elf', '-I', 'include', '-I', str(out)]
        dep = obj.with_suffix('.d')
        command = [cc, *flags, '-MMD', '-MF', str(dep), '-MT', 'fx_core', '-c', name, '-o', str(obj)]
        run(command)
        compilations[name] = command
        dependency_graph[name] = dependencies(dep, ROOT)
    debug_identity, complete_identity_inputs, identity_rebuilt = finalize_identity(
        identity_inputs, dependency_graph, identity_header, debug_identity, write_debug_identity,
        lambda source: run(compilations[source]))
    elf = out / 'core.elf'
    binary = out / 'core.bin'
    subprocess.run([ld, '-m', 'aarch64elf', '-T', 'emulation/z8_core.ld', '--gc-sections',
                    *['--undefined=' + n for n in
                      [*hooks.values(), 'fx_z8_menu_metadata', 'fx_z8_panic_metadata',
                       'fx_z8_startup_metadata', 'fx_native_rtos_metadata',
                       'fx_native_modules_metadata', 'fx_native_load_buffer_metadata',
                       'fx_native_load_owner_metadata', 'fx_worker_wait_metadata',
                       'fx_native_ui_dispatch_metadata', 'fx_z8_debug_identity_anchor', 'fx_z8_wait_site0', 'fx_z8_wait_site1', 'fx_z8_wait_site2', 'fx_z8_wait_site3', 'fx_z8_wait_site4']],
                    *objects, '-o', str(elf)], cwd=ROOT, check=True)
    subprocess.run([objcopy, '-O', 'binary', str(elf), str(binary)], cwd=ROOT, check=True)
    symbols = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        [nm, '--defined-only', str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    extension = bytearray(binary.read_bytes())
    extension = bytes(extension)
    if CODE + len(extension) > LIMIT:
        raise ValueError('core exceeds RX padding')
    candidate = bytearray(original)
    candidate[CODE - BASE:CODE - BASE + len(extension)] = extension
    def branch(address, target):
        # Plain B (LR untouched): required at the dispatcher entry, where the
        # displaced window must save the gate's resume point in the original
        # stack frame. UI sites keep ui_call (BL) as before.
        delta = target - address
        if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
            raise ValueError('branch out of range')
        return struct.pack('<I', 0x14000000 | ((delta // 4) & 0x3FFFFFF))
    changes = []
    for address, name in hooks.items():
        if name not in symbols:
            raise ValueError('hook symbol missing: ' + name)
        plain = address in (SYSTEM_HOOK, MENU_PAGE_HOOK, MENU_TEXT_HOOK, MENU_NAV_HOOK)
        encode = branch if plain else ui_call
        before = original[address - BASE:address - BASE + 4]
        candidate[address - BASE:address - BASE + 4] = encode(address, symbols[name])
        changes.append(dict(address=hex(address), symbol=name,
                            before=before.hex(), after=encode(address, symbols[name]).hex()))
    restored = bytearray(candidate)
    restored[CODE - BASE:CODE - BASE + len(extension)] = bytes(len(extension))
    for change in changes:
        at = int(change['address'], 16) - BASE
        restored[at:at + 4] = bytes.fromhex(change['before'])
    if bytes(restored) != original:
        raise ValueError('unexpected firmware mutation')
    import binascii
    candidate[-2:] = binascii.crc_hqx(bytes(candidate[:-2]), 0).to_bytes(2, 'big')
    if binascii.crc_hqx(bytes(candidate), 0) != 0:
        raise ValueError('patched MAIN CRC residue nonzero')
    (out / 'z8-core.main').write_bytes(candidate)
    receipt = {n: hashlib.sha256((ROOT / n).read_bytes()).hexdigest()
               for n in list(NAMES) + list(identity_inputs)}
    for group in dependency_graph.values():
        for n in group:
            if n not in receipt and n != identity_header:
                receipt[n] = hashlib.sha256((ROOT / n).read_bytes()).hexdigest()
    (out / 'build.json').write_text(json.dumps(dict(
        target=TARGET, code_bytes=len(extension),
        candidate_sha256=hashlib.sha256(candidate).hexdigest(),
        source_sha256=receipt,
        ui_dispatch_hook_version=1, system_hook_version=1, menu_hook_version=1, font_hook_version=1, changes=changes,
        commands=commands, compiler_dependencies=dependency_graph,
        debug_identity_configuration=debug_configuration,
        debug_identity_header=identity_header,
        debug_build_identity=debug_identity.hex(),
        debug_identity_scheme=1), indent=1) + '\n')
    (out / 'core-report.json').write_text(json.dumps(dict(
        target=TARGET, sources=len(NAMES), core_bytes=len(extension),
        hooks=changes, hook_count=len(changes),
        configuration=('menu-enabled: ' if menu_enabled else 'v0: ') + 'UI-loop + sysctl + native-menu + font-request hooks; '
                      'dormant until platform state initialises; render rectangle/modal, '
                      'modal id, font fallback, display, ptp/linux/usb unavailable',
        ui_dispatch=verify_ui(TARGET, original)), indent=1) + '\n')
    print(json.dumps(dict(status='BUILT', core_bytes=len(extension), hooks=len(changes),
                          output=str(out / 'z8-core.main'))))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware', required=True)
    p.add_argument('--output-dir', required=True)
    p.add_argument('--menu-enabled', action='store_true',
                   help='build the menu-enabled image (no FX_Z8_HEADLESS: install-time '
                        'menu construction + UI-pump loader; default stays v0 headless)')
    a = p.parse_args()
    build(a.firmware, a.output_dir, a.menu_enabled)
