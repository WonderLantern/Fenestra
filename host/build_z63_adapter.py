"""Build the C2.01 card adapter object after checking its original bindings.

This emits a relocatable development object, not a resident core or update.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def load_application(application):
    profile = json.loads((ROOT / 'targets/z63-c201.json').read_text())
    if (profile.get('schema'), profile.get('target'), profile.get('application_bytes'), profile.get('application_sha256')) != (
            'fenestra.port-bindings/v1', 'z63-c201', 88830367,
            'd78e4d0358f550d9f3a3a9da3a8beee314a3b90de8bcc13117e029f7821cf252'):
        raise ValueError('not the pinned Z6III C2.01 profile')
    data = Path(application).read_bytes()
    if len(data) != profile['application_bytes'] or hashlib.sha256(data).hexdigest() != profile['application_sha256']:
        raise ValueError('wrong Z6III C2.01 original application')
    if hashlib.sha256(data[:-2]).hexdigest() != profile['payload_sha256']:
        raise ValueError('Z6III module payload identity differs')
    for binding in profile['bindings']:
        at = binding['address'] - profile['image_base']
        span = data[at:at + binding['bytes']]
        if hashlib.sha256(span).hexdigest() != binding['sha256']:
            raise ValueError('original binding differs: ' + binding['name'])
    return profile, data


def verify(application):
    return load_application(application)[0]


def build(application, output):
    profile = verify(application)
    compiler = shutil.which('clang')
    if not compiler:
        raise RuntimeError('Clang required')
    out = Path(output).resolve()
    if out.exists():
        raise ValueError('new output directory required')
    out.mkdir(parents=True)
    source = ROOT / 'src/platform/z63_c201/card.c'
    flags = ['--target=aarch64-none-elf', '-std=c11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Wconversion', '-Wshadow', '-ffreestanding', '-fno-builtin',
             '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector',
             '-DFX_FREESTANDING=1', '-I', str(ROOT / 'include')]
    subprocess.run([compiler, *flags, '-c', str(source), '-o', str(out / 'card.o')], check=True)
    receipt = dict(target=profile['target'], artifact='relocatable-card-adapter',
                   application_sha256=profile['application_sha256'],
                   object_sha256=hashlib.sha256((out / 'card.o').read_bytes()).hexdigest(),
                   compiler=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                   firmware_image=False, runtime_qualified=False)
    (out / 'adapter.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt
