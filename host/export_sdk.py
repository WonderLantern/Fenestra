"""Export a standalone module SDK from public sources; no firmware required."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
FILES=('LICENSE','EXCEPTIONS.md','NOTICE','src/memory.c','src/stream.c','src/audio.c',
       'host/build_sdk_module.py','host/pack_module.py','host/module_header.py','host/symbolize_module.py',
       'host/target_c110.py','host/platform_profiles.py','targets/nikon-zr.json')

def export(output):
    output=Path(output).resolve()
    if output.exists():raise ValueError('SDK output must be a new directory')
    names=set(FILES)
    names.update(p.relative_to(ROOT).as_posix() for p in (ROOT/'include').glob('*.h'))
    contents={name:(ROOT/name).read_bytes() for name in sorted(names)}
    contents['build_module.py']=b'import sys\nfrom pathlib import Path\nsys.path.insert(0,str(Path(__file__).resolve().parent/"host"))\nfrom build_sdk_module import main\nif __name__=="__main__":main()\n'
    contents['README.md']=(
        '# Fenestra standalone module SDK\n\n'
        'Requires Python 3.11+ and LLVM clang/ld.lld. No original firmware or keys are needed.\n\n'
        'Build an external project:\n\n'
        '    python build_module.py /path/to/project/module.c --target zr-c110 --id my_module --output-dir /path/to/new/build\n\n'
        'Add `--native` for `fenestra_native_entry`, `--source` for additional C translation units, '
        '`--library` for static archives, `--include-dir` for external headers, and `--debug` for local symbols. '
        'Use `src/stream.c` and `src/audio.c` as optional module-side libraries. '
        'Native callbacks and code remain resident until restart.\n'
    ).encode()
    output.mkdir(parents=True)
    for name,data in contents.items():
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    manifest={'schema':'fenestra.sdk/v1','files':[{'path':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()} for name,data in sorted(contents.items())]}
    (output/'SDK-INVENTORY.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return manifest

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv);result=export(args.output);print(json.dumps({'files':len(result['files'])}))

if __name__=='__main__':main()
