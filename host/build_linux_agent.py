"""Cross-compile the owned Linux RAM agent without selecting a host environment."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='aarch64-linux-gnu-gcc', help='AArch64 Linux cross-compiler executable')
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    compiler = shutil.which(args.cc)
    if not compiler:
        raise ValueError('AArch64 Linux compiler not found; set --cc')
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    if not output.is_relative_to(root/'build') or output.exists():
        raise ValueError('choose a new directory below build/')
    source = root/'linux/fenestra_agent.c'
    output.mkdir(parents=True)
    binary = output/'fenestra-agent'
    subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                    str(source), '-o', str(binary)], check=True)
    data = binary.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or data[18:20] != b'\xb7\0':
        raise ValueError('compiler did not produce AArch64 little-endian ELF64')
    report = dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  agent_sha256=hashlib.sha256(data).hexdigest(), agent_bytes=len(data),
                  compiler=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0])
    (output/'build.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
