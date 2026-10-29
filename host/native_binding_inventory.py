"""Source-declared Nikon call inventory; whole application identity stays pinned.

Version 1 reproduces historical receipts. Version 2 includes literal FN calls
and named function bindings instead of only the FN(UINT64_C(...)) spelling.
It is an inventory of these declarations, not arbitrary binary call discovery.
"""
import re
from platform_profiles import ROOT,PROFILES

def addresses(build,target='zr-c110',resolve_source=None):
    version=build.get('native_binding_inventory_version',1)
    if version not in (1,2):raise ValueError('unknown native binding inventory version')
    result=set()
    if build.get('sensor_trace_hook_version'):result.update((0x413521d4,0x41352298,0x40eafab4))
    if build.get('event_trace_hook_version'):
        from c110_event_sites import POINTS
        result.update(p[0] for p in POINTS);result.update((0x4018569c,0x401865f0,0x40189174))
    pattern=r'FN\(UINT64_C\((0x4[0-9a-fA-F]+)\)' if version==1 else r'FN\(\s*(?:UINT64_C\(\s*)?(0x4[0-9a-fA-F]+)'
    for name in build['source_sha256']:
        if name.endswith(('.c','.S')):
            path=resolve_source(name) if resolve_source else (ROOT/name).resolve(strict=True)
            if resolve_source is None and not path.is_relative_to(ROOT):raise ValueError('source inventory escapes project')
            result.update(int(value,16) for value in re.findall(pattern,path.read_text()))
    if version==2:result.update(PROFILES[target]['functions'].values())
    return result
