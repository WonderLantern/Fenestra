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
        c.add_argument('--target', choices=('zr-c110', 'z9-c532'), required=True)
        c.add_argument('--firmware', type=Path, required=True)
    build.add_argument('--output', type=Path, required=True)
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
    probe.add_argument('--menu', action='store_true', help='install the experimental manual test menu')
    probe.add_argument('--module', type=Path, help='optional pinned headless module experiment')
    args = p.parse_args()
    if args.command == 'build-z63-probe':
        from build_z63_probe import build as build_probe
        print(json.dumps(build_probe(args.application, args.output_dir, args.linker, args.llvm_bin, args.module, args.menu), indent=2))
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
