"""Build the Linux file overlay helper with an explicit AArch64 cross compiler."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build(compiler, output):
    compiler = shutil.which(compiler)
    if not compiler:
        raise ValueError('AArch64 Linux compiler unavailable; specify --cc')
    output = output.resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('choose a new output directory below build/')
    output.mkdir(parents=True)
    binary = output / 'fenestra-file-overlay'
    inputs = ['linux/fenestra_file_overlay.c', 'src/sha256.c', 'src/memory.c']
    subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                    '-I', str(ROOT / 'include'), *(str(ROOT / p) for p in inputs),
                    '-o', str(binary)], check=True)
    data = binary.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or data[18:20] != b'\xb7\0':
        raise ValueError('compiler output must be AArch64 little-endian ELF64')
    report = dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                  source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in inputs},
                  target='aarch64-linux', minimum_kernel='4.14')
    (output / 'build.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='aarch64-linux-gnu-gcc')
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.cc, args.output_dir), indent=2))


if __name__ == '__main__':
    main()
