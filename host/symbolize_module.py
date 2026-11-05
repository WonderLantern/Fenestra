"""Resolve a retained module PC using its matching unstripped local ELF."""
import argparse
from pathlib import Path
import shutil
import subprocess

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--base',type=lambda s:int(s,0),required=True)
    parser.add_argument('--pc',type=lambda s:int(s,0),action='append',required=True)
    args=parser.parse_args(argv)
    tool=shutil.which('llvm-addr2line') or shutil.which('addr2line')
    if not tool:parser.error('LLVM addr2line is required')
    if args.base<0 or any(pc<args.base or pc>0xffffffffffffffff for pc in args.pc):parser.error('PC must be at or above the mapped module base')
    subprocess.run([tool,'-e',str(args.elf.resolve(strict=True)),'-f','-C',
        *[hex(pc-args.base) for pc in args.pc]],check=True,timeout=30)

if __name__=='__main__':main()
