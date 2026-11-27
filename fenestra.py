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
    loader = commands.add_parser('card-loader', help='build an experimental payload-independent card loader')
    payload = commands.add_parser('card-payload', help='wrap a flat AArch64 image for the card loader')
    ranges = commands.add_parser('range-substitution', help='pack multi-version read-only range replacements')
    ranges.add_argument('--manifest', type=Path, required=True)
    ranges.add_argument('--output', type=Path, required=True)
    core = commands.add_parser('card-core', help='build one shared Fenestra BOOT.BIN without firmware inputs')
    port = commands.add_parser('card-port', help='build a camera Port for a shared card Core')
    for c in (build, make, loader, port):
        c.add_argument('--target', choices=('zr-c110', 'z9-c532'), required=True)
        c.add_argument('--firmware', type=Path, required=True)
    build.add_argument('--output', type=Path, required=True)
    make.add_argument('--key', type=Path, required=True, help='private local body key/mask file; never printed')
    make.add_argument('--digest-suffix', type=Path, help='ZR only: private 8-byte digest suffix file')
    make.add_argument('--envelope-key', type=Path, help='ZR r3: user RSA public/private PEM for exact envelope reproduction')
    make.add_argument('--release', choices=('r3', 'custom'), default='r3', help='r3 requires exact reference bytes; custom builds a different, unqualified result')
    make.add_argument('--work', type=Path, required=True)
    make.add_argument('--output', type=Path, required=True)
    loader.add_argument('--output', type=Path, required=True)
    loader.add_argument('--emit-main', action='store_true')
    payload.add_argument('--image', type=Path, required=True)
    payload.add_argument('--entry-offset', type=lambda value:int(value,0), default=0)
    payload.add_argument('--output', type=Path, required=True)
    core.add_argument('--output', type=Path, required=True)
    port.add_argument('--core', type=Path, required=True)
    port.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.command == 'range-substitution':
        from range_substitution import write
        print(json.dumps(write(args.manifest, args.output), indent=2))
        return
    if args.command == 'card-loader':
        from build_card_loader import build
        print(json.dumps(build(args.target,args.firmware,args.output,emit_main=args.emit_main),indent=2))
        return
    if args.command == 'card-payload':
        from card_payload import write
        print(f'{write(args.image,args.entry_offset,args.output)} bytes written to {args.output}')
        return
    if args.command in ('card-core','card-port'):
        from build_card_fenestra import build_core, build_port
        report = (build_core(args.output) if args.command=='card-core'
                  else build_port(args.target,args.firmware,args.core,args.output))
        print(json.dumps(report,indent=2))
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
