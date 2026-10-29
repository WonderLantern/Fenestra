"""Read one compiler-generated -MMD rule with a fixed -MT fx_core target."""
import re
import hashlib
import json
from pathlib import Path


def dependencies(path,root):
    root=Path(root).resolve(strict=True)
    body=Path(path).read_text(encoding='utf-8').replace('\\\n','')
    if not body.startswith('fx_core:'):raise ValueError('unexpected dependency target')
    names=[]
    for token in re.findall(r'(?:\\.|[^\s])+',body[len('fx_core:'):]):
        name=re.sub(r'\\([ #:\\])',r'\1',token).replace('$$','$')
        candidate=(root/name).resolve(strict=True)
        if not candidate.is_relative_to(root):raise ValueError('compiler input outside owned project: '+name)
        names.append(candidate.relative_to(root).as_posix())
    if not names:raise ValueError('empty compiler dependency rule')
    return sorted(set(names))


def identity_configuration(root,output,inputs,configuration):
    """Canonical generated paths; the output directory is not build identity."""
    root=Path(root).resolve(strict=True);output=Path(output).resolve(strict=True)
    hashes={}
    for name in sorted(set(inputs)):
        path=(root/name).resolve(strict=True)
        if not path.is_relative_to(root):raise ValueError('identity input outside project')
        key='generated/'+path.relative_to(output).as_posix() if path.is_relative_to(output) else path.relative_to(root).as_posix()
        if key in hashes:raise ValueError('duplicate canonical identity input')
        hashes[key]=hashlib.sha256(path.read_bytes()).hexdigest()
    result=dict(configuration,sources=hashes)
    return hashlib.sha256(json.dumps(result,sort_keys=True,separators=(',',':')).encode()).digest(),result


def finalize_identity(inputs,graph,header,initial,emit,recompile):
    """Discover before hashing, then rebuild every identity consumer once.

    Both firmware builders use this same path. Generated identity is a leaf
    containing constants, never a configuration include or its own input.
    """
    complete=sorted((set(inputs)|{n for group in graph.values() for n in group})-{header})
    identity=emit(complete)
    rebuilt=[]
    if identity!=initial:
        for source,group in graph.items():
            if header in group:
                recompile(source);rebuilt.append(source)
    return identity,complete,rebuilt
