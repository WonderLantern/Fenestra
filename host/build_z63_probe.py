"""Build a preimage-checked C2.01 SYSCTL entry/return probe for emulation."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from build_z63_adapter import ROOT, load_application


def build(application, output, linker, llvm_bin, module=None, menu=False):
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
    if any(original[begin-base:end-base]):
        raise ValueError('probe region is not empty')
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
    if module is not None:
        from module_header import validate_package
        package = Path(module).read_bytes()
        validate_package(package, bytes.fromhex(profile['payload_sha256']))
        digest = hashlib.sha256(package).digest()
        (out / 'z63_probe_module.h').write_text('#define FX_Z63_PROBE_BYTES %du\nstatic const unsigned char fx_z63_probe_digest[32]={%s};\n' % (len(package), ','.join(str(x) for x in digest)))
        flags += ['-DFX_Z63_MODULE_PROBE=1']
        sources += ['src/platform/z63_c201/' + name for name in ('module_probe.c', 'module_session.c', 'module_stage.c', 'address_space.c', 'exec_context.c', 'task.c', 'card.c', 'private_call.S', 'user_call.S')]
        sources += ['src/' + name for name in ('memory.c', 'sha256.c', 'module.c', 'loader.c', 'card.c', 'exec.c', 'runtime.c', 'file_card.c', 'module_load_buffer.c', 'accounting.c')]
    hooks = {hook['dispatcher']: 'fx_z63_system_dispatch'}
    if menu:
        if module is None:
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
    objects = []
    for index, name in enumerate(sources):
        obj = out / ('%02d.o' % index)
        subprocess.run([cc, *[f for f in flags if not (name.endswith('.S') and f == '-mgeneral-regs-only')], '-c', str(ROOT / name), '-o', str(obj)], check=True)
        objects.append(str(obj))
    elf = out / 'probe.elf'
    subprocess.run([str(linker), '-m', 'aarch64elf', '--gc-sections', *['--undefined=' + name for name in hooks.values()], '-T', str(ROOT / 'emulation/z63_probe.ld'), *objects, '-o', str(elf)], check=True)
    binary = out / 'probe.bin'
    subprocess.run([str(Path(llvm_bin) / 'llvm-objcopy'), '-O', 'binary', str(elf), str(binary)], check=True)
    symbols = {}
    for line in subprocess.check_output([str(Path(llvm_bin) / 'llvm-nm'), '--defined-only', str(elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    code = binary.read_bytes()
    if not code or begin + len(code) > end:
        raise ValueError('probe overflow')
    destination = symbols['fx_z63_system_dispatch']
    delta = destination - hook['dispatcher']
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('hook branch out of range')
    candidate = bytearray(original)
    candidate[begin-base:begin-base+len(code)] = code
    for address, symbol in hooks.items():
        delta = symbols[symbol] - address
        if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
            raise ValueError('hook branch out of range')
        candidate[address-base:address-base+4] = struct.pack('<I', 0x14000000 | ((delta // 4) & 0x3ffffff))
    # The emulator consumes the EG directly. No member CRC or update package
    # is emitted; retained CRC bytes remain original and must not be mistaken
    # for a package-integrity result.
    image = out / 'probe.eg.bin'
    image.write_bytes(candidate)
    # Independently reverse declared edits and prove all other bytes unchanged.
    restored = bytearray(candidate)
    restored[begin-base:begin-base+len(code)] = original[begin-base:begin-base+len(code)]
    for address in hooks:
        restored[address-base:address-base+4] = original[address-base:address-base+4]
    if restored != original:
        raise ValueError('undeclared image change')
    receipt = dict(target='z63-c201', artifact='emulator-sysctl-probe',
                   application_sha256=profile['application_sha256'],
                   candidate_sha256=hashlib.sha256(candidate).hexdigest(),
                   symbols=symbols, hook=hook, hooks={hex(a): n for a,n in hooks.items()}, menu=menu, original_reconstruction=True,
                   firmware_package=False, runtime_qualified=False, do_not_flash=True,
                   compiler=subprocess.check_output([cc, '--version'], text=True).splitlines()[0],
                   linker=subprocess.check_output([str(linker), '--version'], text=True).splitlines()[0],
                   sources={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in
                            [*sources, 'host/build_z63_probe.py', 'host/build_z63_adapter.py',
                             'targets/z63-c201.json', 'emulation/z63_probe.ld',
                             *[p.relative_to(ROOT).as_posix() for p in sorted((ROOT / 'include').rglob('*.h'))],
                             *[p.relative_to(ROOT).as_posix() for p in sorted((ROOT / 'src/platform/z63_c201').glob('*.h'))]]},
                   module_sha256=hashlib.sha256(Path(module).read_bytes()).hexdigest() if module is not None else None)
    (out / 'probe.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt
