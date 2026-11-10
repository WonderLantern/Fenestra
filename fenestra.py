"""Portable Fenestra build and local ciphertext-patch generation entry point."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent/'host'))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest='command', required=True)
    build = commands.add_parser('build', help='build/independently verify a core from an original application component')
    make = commands.add_parser('make-patch', help='decode, build, verify, re-encode and produce ciphertext replacements')
    for c in (build, make):
        c.add_argument('--target', choices=('zr-c110', 'z9-c532', 'z63-c201') if c is build else ('zr-c110', 'z9-c532'), required=True)
        c.add_argument('--firmware', type=Path, required=True)
    build.add_argument('--output', type=Path, required=True)
    build.add_argument('--llvm-bin', type=Path, help='Z6III: directory containing clang, llvm-nm and llvm-objcopy')
    build.add_argument('--linker', type=Path, help='Z6III: ELF ld.lld executable')
    build.add_argument('--experimental-ptp', action='store_true', help='Z6III: include the unqualified native USB debugging experiment')
    make.add_argument('--key', type=Path, required=True, help='private local body key/mask file; never printed')
    make.add_argument('--digest-suffix', type=Path, help='ZR only: private 8-byte digest suffix file')
    make.add_argument('--envelope-key', type=Path, help='ZR r3: user RSA public/private PEM for exact envelope reproduction')
    make.add_argument('--release', choices=('r3', 'custom'), default='r3', help='r3 requires exact reference bytes; custom builds a different, unqualified result')
    make.add_argument('--work', type=Path, required=True)
    make.add_argument('--output', type=Path, required=True)
    adapter = commands.add_parser('build-z63-adapter', help='build C2.01 card adapter object only')
    adapter.add_argument('--application', type=Path, required=True)
    adapter.add_argument('--output-dir', type=Path, required=True)
    probe = commands.add_parser('build-z63-probe', help='build an emulator-only C2.01 SYSCTL probe')
    probe.add_argument('--application', type=Path, required=True)
    probe.add_argument('--output-dir', type=Path, required=True)
    probe.add_argument('--linker', type=Path, required=True)
    probe.add_argument('--llvm-bin', type=Path, required=True)
    probe.add_argument('--ui', action='store_true', help='enable native application pages and UI task dispatch')
    probe.add_argument('--drawing', action='store_true', help='exercise the C2.01 native renderer after the module test')
    probe.add_argument('--runtime', action='store_true', help='run discovered modules using the C2.01 worker runtime')
    probe.add_argument('--ptp', action='store_true', help='enable the C2.01 native PTP debugging experiment')
    probe.add_argument('--sdk', action='store_true', help='enable shared SDK service dispatch experiment')
    probe.add_argument('--discovery', action='store_true', help='exercise native card module discovery before the pinned test')
    probe.add_argument('--menu', action='store_true', help='install the experimental manual test menu')
    probe.add_argument('--module', type=Path, help='optional pinned headless module experiment')
    package = commands.add_parser('package-z63', help='rebuild a local C2.01 research container; no camera installation')
    package.add_argument('--original-package', type=Path, required=True)
    package.add_argument('--original-body', type=Path, required=True)
    package.add_argument('--candidate', type=Path, required=True)
    package.add_argument('--receipt', type=Path, required=True)
    package.add_argument('--output-dir', type=Path, required=True)
    package.add_argument('--suffix-file', type=Path, help='optional local eight-byte authentication material, checked against the original envelope')
    package.add_argument('--unqualified-envelope', action='store_true', help='allow a complete envelope although the target is not qualified for physical installation')
    args = p.parse_args()
    if args.command == 'build' and args.target == 'z63-c201':
        if args.llvm_bin is None or args.linker is None:
            p.error('z63-c201 requires --llvm-bin and --linker')
        from build_z63_probe import build as build_z63
        from verify_z63_build import verify
        result = build_z63(args.firmware, args.output, args.linker, args.llvm_bin,
                           menu=True, sdk=True, ptp=args.experimental_ptp,
                           runtime=True, ui=True)
        checked = verify(args.firmware, args.output, args.llvm_bin)
        (args.output / 'verified.json').write_text(json.dumps(checked, indent=2)+'\n')
        print(json.dumps(dict(build=result, verification=checked), indent=2))
        return
    if args.command == 'build' and (args.llvm_bin or args.linker or args.experimental_ptp):
        p.error('the explicit toolchain and experimental PTP options apply only to z63-c201')
    if args.command == 'package-z63':
        from z63_package import build as package_z63
        result = package_z63(args.original_package, args.original_body, args.candidate, args.receipt, args.output_dir, args.suffix_file, args.unqualified_envelope)
        print(json.dumps(result, indent=2))
        return
    if args.command == 'build-z63-probe':
        from build_z63_probe import build as build_probe
        print(json.dumps(build_probe(args.application, args.output_dir, args.linker, args.llvm_bin, args.module, args.menu, args.discovery, args.sdk, args.ptp, args.runtime, args.drawing, args.ui), indent=2))
        return
    if args.command == 'build-z63-adapter':
        from build_z63_adapter import build as build_adapter
        print(json.dumps(build_adapter(args.application, args.output_dir), indent=2))
        return
    from repack import build_core, produce
    if args.command == 'build':
        result = build_core(args.target, args.firmware.resolve(), args.output)
        print(result)
    else:
        result = produce(args.target, args.firmware, args.key, args.digest_suffix, args.work, args.output,
                         release=args.release, envelope_key=args.envelope_key)
        print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
