"""Build the shared Fenestra boot payload and separate model Ports."""
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

from build_card_loader import LOADER, branch, native_inputs
from card_payload import ADDRESS, pack
from card_port_inputs import LINK_ROOT, SYMBOL
from cipher_patch import sha
from platform_profiles import BASE, ROOT, load

PAGE = 4096
HEADER = struct.Struct('<4I32s32s17Q')
PORT_MAGIC = 0x31504e46
SAFE_SYMBOL = re.compile(SYMBOL)
SLOTS = sorted(re.findall(r'X\((fx_\w+),(?:(fx_\w+),\w+,)?(\d+)\)',
    (ROOT/'include/fenestra_core_port.h').read_text(),re.M),key=lambda slot:int(slot[2]))
IMPORTS = tuple(name for name,_,_ in SLOTS)
PROFILES = {name for name,accessor,_ in SLOTS if accessor}
SHARED = tuple('src/'+name for name in '''
    accounting.c binding_store.c capacity.c capture_profile.c card.c exec.c
    file_card.c hooks.c loader.c machine.c memory.c module.c module_bus.c
    module_load_buffer.c module_manager.c native_apps.c native_call.c
    native_call_a64.S native_display.c native_editor.c native_font.c
    native_hook_pages.c native_linux.c native_menu.c native_modules.c native_panic.c
    native_ptp.c native_resources.c native_rtos.c native_rtos_monitor.c native_scene.c
    native_tasks.c native_text.c native_title.c native_ui.c native_ui_callbacks.c
    native_ui_dispatch.c native_ui_editor.c native_ui_pager.c native_ui_reader.c
    native_ui_routes.c native_vg_call.S native_visibility.c panic.c platform.c ptp.c
    ptp_device_info.c records.c runtime.c scene.c services.c sha256.c slot_pool.c
    startup.c startup_files.c tasks.c text_fit.c text_page.c text_regions.c
    transfers.c ui_dispatch.c ui_document.c video_configuration.c worker_wait.c
'''.split())
FEATURES = ('FX_PLATFORM_PROFILE=1','FX_NATIVE_SCENE=1','FX_NATIVE_TASKS=1','FX_NATIVE_UI_INPUT=1')


def new_output(path):
    out = path.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build':
        raise ValueError('choose a new output directory beneath build/')
    out.mkdir(parents=True)
    return out


def run(command,*args):
    executable=shutil.which(str(command))
    if not executable:
        raise RuntimeError(f'{command} is required')
    return subprocess.check_output([executable,*map(str,args)],cwd=ROOT,text=True,timeout=180)


def symbols(path):
    rows = dict()
    for line in run('llvm-nm','--defined-only','--print-size',path).splitlines():
        fields = line.split()
        if len(fields)==4:
            address,size,kind,name = fields
            rows[name] = (int(address,16),int(size,16),kind)
    return rows


def link(out,name,sources,address,entry,*,defines=(),objects=(),flags=(),tail=''):
    inputs = []
    for i,source in enumerate(sources):
        obj = out/f'{name}-{i}.o'
        path = ROOT/source
        args = ['-std=c11','-Oz','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                '-DFX_FREESTANDING=1','-ffreestanding','-fno-builtin','-mgeneral-regs-only',
                '-mstrict-align','-fno-stack-protector','-fno-unwind-tables',
                '-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections'] if path.suffix=='.c' else []
        run('clang','--target=aarch64-none-elf',*args,*['-D'+value for value in defines],
            '-I',ROOT/'include','-I',out,'-c',path,'-o',obj)
        inputs.append(obj)
    script = out/f'{name}.ld'
    script.write_text(f'''ENTRY({entry})
SECTIONS {{
  . = {address:#x};
  .blob : {{ KEEP(*(.fx_card_header)) }}
  .text : {{ *(.text*) }}
  .rodata : {{ *(.rodata*) }}
  .data : {{ *(.data*) }}
  .bss : {{ *(.bss*) *(COMMON) }}
  {tail}
  ASSERT(. <= {LOADER:#x}, "card arena exhausted")
  ASSERT(SIZEOF(.data) + SIZEOF(.bss) == 0, "card image requires mutable ELF sections")
  /DISCARD/ : {{ *(.comment) *(.eh_frame*) *(.note*) }}
}}
''',encoding='ascii')
    elf,binary,response = (out/f'{name}.{suffix}' for suffix in ('elf','bin','rsp'))
    options = ('-m','aarch64elf','-T',script,'--no-undefined',*flags,*inputs,*objects,'-o',elf)
    response.write_text('\n'.join('"'+(a.as_posix() if isinstance(a,Path) else a)+'"' for a in options),
                        encoding='utf-8')
    run('ld.lld','@'+str(response))
    run('llvm-objcopy','-O','binary',elf,binary)
    image,table = binary.read_bytes(),symbols(elf)
    if not 0<len(image)<=LOADER-address or not address<=table[entry][0]<address+len(image):
        raise ValueError('linked image or entry exceeds the card arena')
    return image,table


def build_core(destination):
    out = new_output(destination)
    image,table = link(out,'main',(*SHARED,'src/core_port.c','src/core_port_imports.S','src/card_payload.c'),
        ADDRESS,'fx_universal_core_start',defines=(*FEATURES,'FX_UNIVERSAL_CORE=1'),
        tail='__fx_native_port = ALIGN(., 4096);')
    entry = table['fx_universal_core_start'][0]
    port = table['__fx_native_port'][0]
    if port!=(ADDRESS+len(image)+PAGE-1)&-PAGE or port>=LOADER:
        raise ValueError('invalid aligned Port reservation')
    exports = {name:a for name,(a,_,kind) in table.items() if kind in ('T','R') and
               name not in IMPORTS and ADDRESS<=a<ADDRESS+len(image) and SAFE_SYMBOL.fullmatch(name)}
    card = out/'FENESTRA'
    card.mkdir()
    (card/'BOOT.BIN').write_bytes(pack(image,entry-ADDRESS))
    report = dict(abi=1, core_sha256=sha(image),core_bytes=len(image),entry=entry,
                  port_address=port,exports=exports)
    (out/'core.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
    return {key:value for key,value in report.items() if key!='exports'}


def read_core(out):
    core = json.loads((out/'core.json').read_bytes())
    payload = (out/'FENESTRA/BOOT.BIN').read_bytes()
    image = (out/'main.bin').read_bytes()
    if (core['abi']!=1 or core['core_bytes']!=len(image) or core['core_sha256']!=sha(image) or
        payload!=pack(image,core['entry']-ADDRESS) or
        core['port_address']!=(ADDRESS+len(image)+PAGE-1)&-PAGE or core['port_address']>=LOADER):
        raise ValueError('Core receipt and boot image differ')
    table = symbols(out/'main.elf')
    if sha(image)!=sha_from_elf(out) or core['entry']!=table['fx_universal_core_start'][0] or any(
        name not in table or table[name][0]!=address or not ADDRESS<=address<ADDRESS+len(image)
        or name in IMPORTS or not SAFE_SYMBOL.fullmatch(name) for name,address in core['exports'].items()
    ):
        raise ValueError('Core symbols do not match its image')
    return core,payload


def sha_from_elf(out):
    # Compare ELF load contents without changing any existing Core output.
    import tempfile
    with tempfile.TemporaryDirectory(prefix='core-link-',dir=ROOT/'build') as folder:
        binary=Path(folder)/'image.bin'
        run('llvm-objcopy','-O','binary',out/'main.elf',binary)
        return sha(binary.read_bytes())


def native_link(target,firmware,out):
    script = 'build_c110_menu.py' if target=='zr-c110' else 'build_z9_core.py'
    features = ('--native-capture','--sensor-trace','--event-trace') if target=='zr-c110' else ()
    run(sys.executable,'-B',ROOT/'host'/script,'--firmware',firmware,'--output-dir',out,
        '--card-port-inputs',*features)
    return json.loads((out/'port-inputs.json').read_bytes())


def native_objects(out,plan):
    objects = []
    for row in plan['objects']:
        if row['source'] in SHARED:
            continue
        image = (out/row['name']).resolve(strict=True)
        if image.parent!=out or image.suffix!='.o' or image in objects or sha(image.read_bytes())!=row['sha256']:
            raise ValueError('invalid or duplicate native link input')
        objects.append(image)
    if not objects or not all(LINK_ROOT.fullmatch(flag) for flag in plan['roots']):
        raise ValueError('invalid native linker roots')
    return objects


def original_flow(instruction,label,absolute):
    """Relocate one displaced instruction while preserving the native return address."""
    site,kind,args = instruction.address,instruction.mnemonic,instruction.op_str
    next_address = absolute(label+'_return',site+4)
    if kind in ('b','bl'):
        target = absolute(label+'_original',int(args.removeprefix('#'),0))
        if kind=='b':
            return [f'b {target}']
        return [f'adrp x30,{next_address}',f'add x30,x30,:lo12:{next_address}',f'b {target}']
    if kind=='blr' and args not in ('x30','lr'):
        return [f'adrp x30,{next_address}',f'add x30,x30,:lo12:{next_address}',f'br {args}']
    if kind.startswith('b.') or kind in ('cbz','cbnz','tbz','tbnz'):
        prefix,_,target = args.rpartition('#')
        target=absolute(label+'_original',int(target,0))
        return [f'{kind} {prefix}{label}_taken',f'b {next_address}',f'{label}_taken:',f'b {target}']
    if kind not in {'add','and','br','cmp','ldr','ldp','ldrb','lsl','lsr','mov','movk',
                    'orr','ret','sub','subs','str','stp','strb'} or (kind=='ldr' and '[' not in args):
        raise ValueError(f'unsupported displaced instruction at {site:#x}: {kind}')
    return [f'.inst {int.from_bytes(instruction.bytes,"little"):#x}',f'b {next_address}']


def make_port_source(out,plan,core,original,boot_site):
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    decode=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
    routes=plan['routes']
    [dispatch]=[row['symbol'] for row in routes if row['site']==boot_site]
    assembly=['.section .fx_card_header,"a",%progbits','.p2align 3',
              f'.word {PORT_MAGIC:#x},1,{HEADER.size},0',
              '.byte '+','.join(str(v) for v in bytes.fromhex(plan['application_sha256']+core['core_sha256'])),
              f'.quad fx_card_port_install,fx_card_port_attach,{dispatch}',
              '.quad '+','.join(IMPORTS),'.global fx_card_port_phase','fx_card_port_phase:','.word 0',
              '.section .text.card_port_routes,"ax",%progbits']
    rows=[]
    definitions=[]
    def absolute(name,value):
        definitions.append(f'--defsym={name}={value:#x}')
        return name
    loader=absolute('fx_card_port_loader_fallback',LOADER)
    observed=set()
    for index,row in enumerate(routes):
        site,extension,link=row['site'],row['symbol'],row['link']
        before=bytes.fromhex(row['before'])
        if (not SAFE_SYMBOL.fullmatch(extension) or type(link)!=bool or site%4 or site in observed or
            not LOADER<site<=BASE+len(original)-4 or before!=original[site-BASE:site-BASE+4]):
            raise ValueError('native route differs from the pinned application')
        observed.add(site)
        label=f'fx_card_port_gate_{index}'
        [instruction]=decode.disasm(before,site)
        if link and instruction.mnemonic!='bl':
            raise ValueError(f'original native call differs at {site:#x}')
        assembly.extend(('.p2align 2',f'.global {label}',f'{label}:','stp x16,x17,[sp,#-16]!',
            'adrp x16,fx_card_port_phase','add x16,x16,:lo12:fx_card_port_phase','ldar w17,[x16]',
            f'cbz w17,{label}_off','ldp x16,x17,[sp],#16',f'b {extension}',f'{label}_off:',
            'ldp x16,x17,[sp],#16'))
        if site==boot_site:
            if link:
                raise ValueError('boot dispatch must be a tail branch')
            before=branch(site,LOADER)
            assembly.append(f'b {loader}')
        else:
            assembly.extend(original_flow(instruction,label,absolute))
        rows.append(f'.word {site:#x},{int.from_bytes(before,"little"):#x},{int(link)},{label}')
    assembly.extend(('.section .rodata.card_port_routes,"a",%progbits','.p2align 2',
                     '.global fx_card_port_routes','fx_card_port_routes:',*rows))
    source=out/'card_port.S'
    source.write_text('\n'.join(assembly)+'\n',encoding='ascii')
    (out/'card_port_routes.h').write_text(
        f'extern const FxPortRoute fx_card_port_routes[{len(rows)}];\n',encoding='ascii')
    imports=out/'core_exports.S'
    imports.write_text(''.join(f'.global {name}\n.set {name},{address:#x}\n'
        for name,address in core['exports'].items()),encoding='ascii')
    return source,imports,dispatch,definitions


def build_port(target,firmware,core_dir,destination):
    firmware=firmware.resolve(strict=True)
    profile,original=load(target,firmware)
    core,payload=read_core(core_dir.resolve(strict=True))
    out=new_output(destination)
    # Use the loader's exact original dispatch preimage and verified native ABI.
    site,_=native_inputs(target,profile,original,out)
    native=out/'native'
    plan=native_link(target,firmware,native)
    if (plan['target']!=target or plan['application_sha256']!=profile['sha256']
        or not 0<len(plan['routes'])<=256):
        raise ValueError('native link receipt differs from the requested target')
    objects=native_objects(native,plan)
    source,imports,dispatch,definitions=make_port_source(out,plan,core,original,site)
    at=core['port_address']
    image,table=link(out,'port',(source,imports,'src/card_boot_native.c','src/card_port_install.c'),
        at,'fx_card_port_install',objects=objects,defines=(f'FX_BOOT_ZR={int(target=="zr-c110")}',),
        flags=('--gc-sections',*plan['roots'],*definitions,f'--defsym=__fx_core_code_begin={ADDRESS:#x}'),
        tail='__fx_core_code_end = .; __fx_c110_code_end = .;')
    entries=tuple(table[name][0] for name in ('fx_card_port_install','fx_card_port_attach',dispatch,*IMPORTS))
    expected=(PORT_MAGIC,1,HEADER.size,0,bytes.fromhex(profile['sha256']),bytes.fromhex(core['core_sha256']),*entries)
    if HEADER.unpack_from(image)!=expected or any(
        pc%4 or not at+HEADER.size+4<=pc<=at+len(image)-4 for pc in entries
    ):
        raise ValueError('linked Port entry table or identity differs')
    for name in PROFILES:
        address,size,kind=table[name]
        if kind.upper()!='R' or address%8 or not size or address+size>at+len(image):
            raise ValueError('invalid read-only Port profile '+name)
    card=out/'FENESTRA'
    path=card/'PORTS'/(target.upper()+'.BIN')
    path.parent.mkdir(parents=True)
    (card/'BOOT.BIN').write_bytes(payload)
    path.write_bytes(image)
    report=dict(target=target,core_sha256=core['core_sha256'],boot_sha256=sha(payload),
                application_sha256=profile['sha256'],port_sha256=sha(image),port_bytes=len(image),
                port_address=at,port_file=path.relative_to(out).as_posix(),routes=len(plan['routes']))
    (out/'build.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
    return report
