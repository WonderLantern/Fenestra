"""Independent image reconstruction and linked-entry checks, without a camera."""
import argparse,hashlib,json,shutil,struct,subprocess,sys
from pathlib import Path
from platform_profiles import ROOT,BASE,load

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--target',required=True);p.add_argument('--firmware',required=True)
    p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    profile,original=load(a.target,a.firmware);build=a.build.resolve(strict=True)
    namespace_output=a.output.with_suffix('.namespace.json')
    if not build.is_relative_to(ROOT) or not a.output.resolve().is_relative_to(ROOT) or a.output.exists() or namespace_output.exists():raise ValueError('new owned verification outputs required')
    report=json.loads((build/'build.json').read_text())
    for name,digest in report['source_sha256'].items():
        path=(ROOT/name).resolve(strict=True)
        if not path.is_relative_to(ROOT) or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('source changed '+name)
    if a.target=='z9-c532':
        code=(build/'core.bin').read_bytes();candidate=(build/'z9-core.main').read_bytes()
        expected={0x403d6618:'fx_z9_menu_page',0x4032e57c:'fx_z9_menu_text',
                  0x40d65a98:'fx_z9_system_dispatch',0x403afa44:'fx_z9_menu_navigate'}
        sys.path.insert(0,str(ROOT/'emulation'))
        from native_ui_dispatch_sites import sites as ui_sites,verify_original as verify_ui
        ui_hooks=ui_sites('z9-c532') if report.get('ui_dispatch_hook_version')==1 else {}
        if ui_hooks:verify_ui('z9-c532',original)
        from native_font_sites import sites as font_sites,verify_original as verify_font
        if report.get('font_scope_hook_version',0) not in (0,1,2):raise ValueError('unknown font scope version')
        font_hooks=font_sites('z9-c532',report['font_scope_hook_version']) if report.get('font_scope_hook_version') else {}
        if font_hooks:verify_font('z9-c532',original)
        expected.update(ui_hooks);expected.update(font_hooks)
        if report.get('ptp_hook_version')==1:
            expected.update({0x407e2e0c:'fx_z9_ptp_lookup',0x407e1834:'fx_z9_ptp_session',
                             0x4035705c:'fx_z9_ptp_operations'})
        if report.get('rtos_hook_version')==1:expected[0x40c05714]='fx_z9_rtos_printf'
        if (len(candidate)!=len(original) or not 0<len(code)<=0x60000 or
            len(report['changes'])!=len(expected) or
            {int(c['address'],16):c['symbol'] for c in report['changes']}!=expected):
            raise ValueError('only independently bound Z9 entries may change')
        if hashlib.sha256(candidate).hexdigest()!=report['candidate_sha256']:raise ValueError('candidate differs')
        nm=shutil.which('llvm-nm')
        if not nm:raise RuntimeError('LLVM symbol reader required')
        symbols={r[2]:int(r[0],16) for line in subprocess.check_output([nm,'--defined-only',str(build/'core.elf')],text=True).splitlines() if len(r:=line.split())==3}
        if candidate[0x40000:0x40000+len(code)]!=code:raise ValueError('linked code differs')
        if report.get('ptp_hook_version')==1:
            for address,name in ((0x407e2e0c,'fx_z9_ptp_original_lookup'),(0x407e1834,'fx_z9_ptp_original_session'),
                                 (0x4035705c,'fx_z9_ptp_original_operations')):
                offset=symbols[name]-0x401a0000
                if code[offset:offset+16]!=original[address-BASE:address-BASE+16]:raise ValueError('PTP trampoline preimage differs')
            if report.get('rtos_hook_version')==1:
                offset=symbols['fx_z9_rtos_original_printf']-0x401a0000
                if code[offset:offset+16]!=original[0x40c05714-BASE:0x40c05724-BASE]:raise ValueError('RTOS printf preimage differs')
        restored=bytearray(candidate);restored[0x40000:0x40000+len(code)]=bytes(len(code))
        for change in report['changes']:
            at=int(change['address'],16)-BASE;word=struct.unpack_from('<I',candidate,at)[0]
            if bytes.fromhex(change['before'])!=original[at:at+4]:raise ValueError('native original branch preimage differs')
            if word&0xfc000000!=(0x94000000 if int(change['address'],16) in {**ui_hooks,**font_hooks} else 0x14000000):raise ValueError('not the expected direct branch/call')
            delta=(word&0x3ffffff)<<2
            if delta&(1<<27):delta-=1<<28
            if BASE+at+delta!=symbols[change['symbol']]:raise ValueError('branch/ELF symbol mismatch')
            restored[at:at+4]=bytes.fromhex(change['before'])
        if restored!=original or candidate[:0x40000]!=original[:0x40000]:raise ValueError('unaccounted firmware mutation')
        checks=['full original reconstruction','unchanged boot prefix/MMU/data','all patched branches match actual linked ELF','entire extension equals linked binary']
        digest=report['candidate_sha256']
    elif a.target=='zr-c110':
        sys.path.insert(0,str(ROOT/'emulation'));from c110_menu_image import verify
        verify((build/'c110-menu.snapshot').read_bytes(),build/'build.json')
        # Both direct official-input builds and historical analysis transfers
        # reconstruct the exact independently pinned original image.
        checks=['existing exact native image/source verification','explicit original C1.10 application identity']
        if report.get('source_kind')=='official-c110-eg':
            from native_binding_check import binding_evidence,verify_binding_windows
            if (build/'candidate.json').is_file():
                binding_evidence(report,json.loads((build/'candidate.json').read_text()),original)
            else:
                verify_binding_windows(report,original)
                code=(build/'menu.bin').read_bytes()
                if (len(code)!=report['code_bytes'] or
                    (build/'c110-menu.snapshot').read_bytes()[0x40000:0x40000+len(code)]!=code):
                    raise ValueError('compiled extension differs from official image')
            checks.append('source-declared native call inventory independently rebuilt against original bytes')
        digest=report['sha256']
    else:raise ValueError('unknown target')
    from verify_build_identity import verify as verify_identity
    nm=shutil.which('llvm-nm')
    if not nm:raise RuntimeError('LLVM symbol reader required')
    identity_check=verify_identity(ROOT,build,a.target,report,nm)
    checks.append('actual compiler inputs and every linked debug identity copy independently checked')
    from verify_native_namespace import verify as verify_namespace
    namespace=verify_namespace(a.target,a.firmware,build)
    checks.append('all original page slots and special selectors pass through compiled native entries')
    result=dict(passed=True,target=a.target,application_sha256=profile['sha256'],candidate_sha256=digest,
                native_namespace=namespace,debug_identity=identity_check,
                native_namespace_file=namespace_output.name,
                checks=checks,qemu_test=False,physical_qualified=False,flashable=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with namespace_output.open('x') as f:json.dump(namespace,f,indent=2)
    with a.output.open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps(result))

if __name__=='__main__':main()
