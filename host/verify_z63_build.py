"""Independently check a local Z6III image against linked code and original bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from build_z63_adapter import ROOT, load_application
from z63_package import verify_application
from z63_placement import check as check_placement


def verify(application, build, llvm_bin):
    profile, original = load_application(application)
    build = Path(build).resolve(strict=True)
    report = json.loads((build / 'probe.json').read_text())
    candidate = (build / 'probe.eg.bin').read_bytes()
    code = (build / 'probe.bin').read_bytes()
    base = profile['image_base']
    begin, end = (profile['probe_code_region'][k] for k in ('begin', 'end'))
    placement = check_placement(profile, original)
    if report.get('placement') != placement:
        raise ValueError('receipt placement differs from the original relocation check')
    if not code or len(code) > end - begin or candidate[begin-base:begin-base+len(code)] != code:
        raise ValueError('linked resident code differs')
    from z63_placement import MARKER, marker_address
    marker = marker_address(profile)
    if begin + len(code) > marker or any(candidate[begin-base+len(code):marker-base]) or \
            candidate[marker-base:end-base] != MARKER:
        raise ValueError('unexplained resident tail')
    # Read the ELF independently; a receipt cannot authorize a different entry.
    symbols = {}
    for line in subprocess.check_output([str(Path(llvm_bin)/'llvm-nm'), '--defined-only',
                                         str(build/'probe.elf')], text=True).splitlines():
        row = line.split()
        if len(row) == 3:
            symbols[row[2]] = int(row[0], 16)
    if symbols != report['symbols']:
        raise ValueError('ELF symbols differ from receipt')
    expected = {profile['sysctl']['dispatcher']: 'fx_z63_system_dispatch'}
    for flag, group, prefix in [('menu', 'menu_hooks', 'fx_z63_menu_'),
                                 ('ptp', 'ptp_hooks', 'fx_z63_ptp_')]:
        if report.get(flag):
            expected.update({r['address']: prefix+n for n,r in profile[group].items()})
    if report.get('native_ui'):
        expected.update({r['address']: r['symbol'] for r in profile['ui_calls']})
    if {int(a,16):n for a,n in report['hooks'].items()} != expected:
        raise ValueError('missing or unexpected hooks')
    verify_application(original, candidate, report, profile)
    for name, digest in report['sources'].items():
        p = (ROOT/name).resolve(strict=True)
        if not p.is_relative_to(ROOT) or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            raise ValueError('source changed: '+name)
    for name, digest in report['generated'].items():
        p = (build/name).resolve(strict=True)
        if not p.is_relative_to(build) or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            raise ValueError('generated input changed: '+name)
    configuration = json.loads((build/'identity.json').read_text())
    identity = hashlib.sha256(json.dumps(configuration,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    if identity != report['build_identity']:
        raise ValueError('build configuration identity differs')
    inputs = {**report['sources'], **{'generated/'+n:d for n,d in report['generated'].items()}}
    if configuration['sources'] != inputs:
        raise ValueError('dependency identities differ')
    copied = [('fx_z63_system_original', profile['sysctl']['dispatcher'])]
    if report.get('menu'):
        copied.append(('fx_z63_navigate_original', profile['menu_hooks']['navigate']['address']))
    if report.get('ptp'):
        copied += [('fx_z63_ptp_original_'+n,r['address']) for n,r in profile['ptp_hooks'].items()]
        at = symbols['fx_z63_debug_build'] - begin
        if code[at:at+32] != bytes.fromhex(identity):
            raise ValueError('linked debug build identity differs')
    for name, address in copied:
        offset = symbols[name] - begin
        if code[offset:offset+16] != original[address-base:address-base+16]:
            raise ValueError('displaced original instructions differ: '+name)
    return dict(schema='fenestra.z63-independent-build/v1', target='z63-c201',
        candidate_sha256=hashlib.sha256(candidate).hexdigest(), build_identity=identity,
        hooks=len(expected), displaced_windows=len(copied), source_inputs=len(inputs),
        original_reconstruction=True, linked_code_verified=True, do_not_flash=True,
        placement=placement, guarded_hooks=len(expected),
        updater_tested=False, physical_camera_tested=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application',type=Path,required=True)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--llvm-bin',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():raise ValueError('new verification output required')
    result=verify(args.application,args.build,args.llvm_bin)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
