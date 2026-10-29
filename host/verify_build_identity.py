"""Independently check compiler inputs and every linked debug identity copy."""
import hashlib,json,subprocess
from pathlib import Path,PureWindowsPath
from compiler_dependencies import dependencies


def verify(root,build,target,report,nm):
    root=Path(root).resolve(strict=True);build=Path(build).resolve(strict=True)
    config=report.get('debug_identity_configuration')
    if not isinstance(config,dict):raise ValueError('debug identity configuration missing')
    header=report.get('debug_identity_header',(build/('z9_debug_identity.h' if target=='z9-c532' else 'c110_debug_identity.h')).relative_to(root).as_posix())
    graph={}
    for command in report['commands']:
        if '-c' not in command or '-MF' not in command:continue
        source=command[command.index('-c')+1]
        dep=command[command.index('-MF')+1]
        filename=PureWindowsPath(dep).name if '\\' in dep else Path(dep).name
        graph[source]=dependencies(build/filename,root)
    if not graph or graph!=report.get('compiler_dependencies'):raise ValueError('compiler dependency receipt differs')
    def canonical(name):
        path=(root/name).resolve(strict=True)
        if not path.is_relative_to(root):raise ValueError('foreign identity input')
        return 'generated/'+path.relative_to(build).as_posix() if path.is_relative_to(build) else path.relative_to(root).as_posix()
    required={canonical(n) for group in graph.values() for n in group if n!=header}
    missing=required-set(config['sources'])
    if missing:raise ValueError('debug identity omits compiled inputs: '+', '.join(sorted(missing)))
    if canonical(header) in config['sources']:raise ValueError('identity hashes its own output')
    for name,digest in config['sources'].items():
        path=(build/name[len('generated/'):] if name.startswith('generated/') else root/name).resolve(strict=True)
        if not path.is_relative_to(root) or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise ValueError('debug identity input changed '+name)
        if report['source_sha256'].get(path.relative_to(root).as_posix())!=digest:
            raise ValueError('identity input missing from source receipt '+name)
    identity=hashlib.sha256(json.dumps(config,sort_keys=True,separators=(',',':')).encode()).digest()
    if identity.hex()!=report['debug_build_identity']:raise ValueError('debug identity digest differs')
    elf=build/('core.elf' if target=='z9-c532' else 'menu.elf')
    code=(build/('core.bin' if target=='z9-c532' else 'menu.bin')).read_bytes()
    wanted='fx_z9_debug_build' if target=='z9-c532' else 'fx_c110_debug_identity'
    # Static symbols repeat across translation units. A dict silently drops
    # all but one copy, so preserve every address independently from llvm-nm.
    addresses=[]
    for line in subprocess.check_output([nm,'--defined-only',str(elf)],text=True).splitlines():
        row=line.split()
        if len(row)==3 and row[2]==wanted:addresses.append(int(row[0],16))
    if not addresses:raise ValueError('linked debug identity missing')
    for address in addresses:
        offset=address-0x401a0000
        if offset<0 or code[offset:offset+32]!=identity:raise ValueError('linked debug identity differs at '+hex(address))
    return dict(passed=True,scheme=report.get('debug_identity_scheme',1),identity=identity.hex(),
                compiled_inputs=len(required),identity_inputs=len(config['sources']),
                linked_copies=[hex(a) for a in addresses],generated_paths_canonical=True)
