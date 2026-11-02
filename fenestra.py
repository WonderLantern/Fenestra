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
    zf_build = commands.add_parser('build-zf', help='reproduce the hardware-tested Zf C3.01 link81 EG')
    zf_build.add_argument('--application', type=Path, required=True)
    zf_build.add_argument('--output-dir', type=Path, required=True)
    zf_make = commands.add_parser('make-zf-package', help='repack Zf C3.01 using external local material')
    for flag in ('firmware', 'body', 'application', 'k8', 'stream', 'output-dir'):
        zf_make.add_argument('--' + flag, type=Path, required=True)
    args = p.parse_args()
    if args.command == 'build-zf':
        from build_zf_link81 import build as build_link81
        print(json.dumps(build_link81(args.application, args.output_dir), indent=2))
        return
    if args.command == 'make-zf-package':
        from zf_c301_repack import repack
        print(json.dumps(repack(args.firmware, args.body, args.application, args.k8,
                                args.stream, args.output_dir), indent=2))
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
