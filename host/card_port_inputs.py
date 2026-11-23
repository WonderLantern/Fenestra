"""Record local native link inputs without emitting a modified application image."""
import json
from pathlib import Path
import re

from cipher_patch import sha
from platform_profiles import BASE

SYMBOL = r'[A-Za-z_]\w*'
LINK_ROOT = re.compile(rf'--(?:undefined={SYMBOL}|defsym={SYMBOL}=0x[0-9a-fA-F]+)')


def write(out, target, original, compilations, link, hooks, call_sites):
    objects = []
    for source, command in compilations.items():
        path = Path(command[command.index('-o')+1])
        if path.parent != out or not path.is_file():
            raise ValueError('native object is outside its build')
        objects.append(dict(source=source, name=path.name, sha256=sha(path.read_bytes())))
    routes = [dict(site=address, symbol=name, link=address in call_sites,
                   before=original[address-BASE:address-BASE+4].hex())
              for address,name in sorted(hooks.items())]
    result = dict(target=target, application_sha256=sha(original), objects=objects, routes=routes,
                  roots=[option for option in link if LINK_ROOT.fullmatch(option)])
    (out/'port-inputs.json').write_text(json.dumps(result,indent=2)+'\n',encoding='ascii')
    return result
