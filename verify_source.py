"""Verify a source distribution inventory, without reading private build inputs."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--distribution',action='store_true',help='also reject unlisted files, including local build outputs')
    args=parser.parse_args()
    root=Path(__file__).resolve().parent
    manifest=json.loads((root/'SOURCE-INVENTORY.json').read_text(encoding='utf-8'))
    if manifest.get('schema')!='fenestra.source-inventory/v1':raise ValueError('unknown inventory')
    names=set()
    for row in manifest['files']:
        name=row['path'];relative=PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name or name in names:
            raise ValueError('unsafe or duplicate inventory path')
        names.add(name);path=(root/name).resolve(strict=True)
        if not path.is_relative_to(root):raise ValueError('source link escapes distribution')
        data=path.read_bytes()
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('source changed: '+name)
    if args.distribution:
        actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
        if actual!=names|{'SOURCE-INVENTORY.json'}:raise ValueError('unlisted files in distribution')
    print(json.dumps(dict(passed=True,files=len(names),strict_distribution=args.distribution)))

if __name__=='__main__':main()
