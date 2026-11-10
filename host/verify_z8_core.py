"""Independently reconstruct stock Z8 and check every linked full-core hook.

This verifier does not import the builder or accept its hook inventory as the
expected inventory. Original UI prerequisites, all native slots and compiler
input identities are separately checked. No camera is accessed.
"""
import argparse,hashlib,json,shutil,struct,subprocess
from pathlib import Path
from platform_profiles import ROOT,BASE,load
from z8_full_profile import mapped_address
from z8_ui_profile import get_profile,verify_ui_fields

def require(ok,message):
 if not ok:raise ValueError(message)
def sha(data):return hashlib.sha256(data).hexdigest()
def expected_hooks():
 fixed={0x403d6618:'fx_z8_menu_page',0x4032e57c:'fx_z8_menu_text',
        0x40d65a98:'fx_z8_system_dispatch',0x403afa44:'fx_z8_menu_navigate',
        0x407e2e0c:'fx_z8_ptp_lookup',0x407e1834:'fx_z8_ptp_session',
        0x4035705c:'fx_z8_ptp_operations',0x40c05714:'fx_z8_rtos_printf'}
 hooks={mapped_address(a):symbol for a,symbol in fixed.items()}
 ui=get_profile();calls={a:'fx_native_ui_'+kind+'_native'for pair in ui['ui_dispatch_hooks']['loops']for a,kind in zip(pair,('wait','poll'))}
 font=ui['font_hooks'];calls.update({a+offset:'fx_z8_font_'+name for a in font['enter']for offset,name in ((0,'enter'),(28,'leave'))})
 calls[font['line_probe']]='fx_z8_font_line_metrics'
 require(not(set(hooks)&set(calls))and len(hooks)==8 and len(calls)==29,'overlapping or missing expected full-core hook')
 return hooks|calls,set(calls)

def verify(firmware,build,nm=None):
 profile,original=load('z8-c311',firmware);ui=verify_ui_fields(original)
 build=Path(build).resolve(strict=True)
 require(build.is_relative_to(ROOT/'build'),'owned full-core build required')
 report=json.loads((build/'build.json').read_text())
 require(report.get('schema')=='fenestra.native-core/v1'and report.get('target')=='z8-c311','complete Z8 core report required')
 for version in ('navigation_hook_version','ptp_hook_version','rtos_hook_version','ui_dispatch_hook_version'):
  require(report.get(version)==1,'missing complete-core hook version: '+version)
 require(report.get('font_scope_hook_version')==2,'complete native font scope version required')
 for name,digest in report['source_sha256'].items():
  source=(ROOT/name).resolve(strict=True)
  require(source.is_relative_to(ROOT)and sha(source.read_bytes())==digest,'compiled source changed: '+name)
 code=(build/'core.bin').read_bytes();candidate=(build/'z8-core.main').read_bytes()
 require(len(candidate)==len(original)and 0<len(code)<=0x60000,'wrong full-core image size')
 require(sha(candidate)==report.get('candidate_sha256'),'candidate identity differs')
 if not nm:nm=shutil.which('llvm-nm')
 if not nm:
  local=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/llvm-nm.exe')
  if local.is_file():nm=str(local)
 require(nm,'LLVM symbol reader required')
 symbols={r[2]:int(r[0],16)for line in subprocess.check_output([str(nm),'--defined-only',str(build/'core.elf')],text=True).splitlines()if len(r:=line.split())==3}
 expected,call_sites=expected_hooks();changes=report['changes']
 require(len(changes)==len(expected)and {int(c['address'],16):c['symbol']for c in changes}==expected,'full-core hook inventory differs')
 require(not any(original[0x40000:0xa0000]),'original extension reservation is occupied')
 require(candidate[0x40000:0x40000+len(code)]==code,'linked extension binary differs')
 restored=bytearray(candidate);restored[0x40000:0x40000+len(code)]=bytes(len(code))
 for change in changes:
  address=int(change['address'],16);at=address-BASE
  before=bytes.fromhex(change['before'])
  require(len(before)==4 and before==original[at:at+4],'native patch preimage differs')
  word=struct.unpack_from('<I',candidate,at)[0]
  require(word&0xfc000000==(0x94000000 if address in call_sites else 0x14000000),'native branch kind differs')
  delta=(word&0x3ffffff)<<2
  if delta&(1<<27):delta-=1<<28
  require(address+delta==symbols[change['symbol']],'native branch does not target linked ELF symbol')
  restored[at:at+len(before)]=before
 require(restored==original,'unaccounted mutation outside full-core hook set')
 trampoline_sources={0x40d65a98:'fx_z8_system_original',0x403afa44:'fx_z8_navigate_original',
                     0x407e2e0c:'fx_z8_ptp_original_lookup',0x407e1834:'fx_z8_ptp_original_session',
                     0x4035705c:'fx_z8_ptp_original_operations',0x40c05714:'fx_z8_rtos_original_printf'}
 for old,symbol in trampoline_sources.items():
  address=mapped_address(old);offset=symbols[symbol]-0x401a0000
  require(offset>=0 and code[offset:offset+16]==original[address-BASE:address-BASE+16],'displaced native entry differs: '+symbol)
  literal,indirect=struct.unpack_from('<II',code,offset+16)
  require(literal&0xff00001f==0x58000010 and indirect==0xd61f0200,'native continuation instruction differs: '+symbol)
  displacement=(literal>>5)&0x7ffff
  if displacement&(1<<18):displacement-=1<<19
  pointer=offset+16+4*displacement
  require(0<=pointer<=len(code)-8 and struct.unpack_from('<Q',code,pointer)[0]==address+16,'native continuation destination differs: '+symbol)
 from verify_build_identity import verify as verify_identity
 identity=verify_identity(ROOT,build,'z8-c311',report,nm)
 from verify_native_namespace import verify as verify_namespace
 namespace=verify_namespace('z8-c311',firmware,build)
 return dict(schema='fenestra.z8-core-verification/v1',passed=True,target='z8-c311',application_sha256=profile['sha256'],candidate_sha256=sha(candidate),
             code_sha256=sha(code),code_bytes=len(code),patched_entries=len(expected),original_ui=ui,native_namespace=namespace,debug_identity=identity,
             checks=['entire original image reconstructed','all '+str(len(expected))+' branches checked against linked ELF symbols','all displaced native entry preimages checked','all original UI metadata/caller prerequisites checked','compiler dependency and embedded build identities checked','compiled page getter and native navigation exercised'],
             physical_qualified=False,qemu_test=False,runtime_ready=False)

def main(argv=None):
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--firmware',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--llvm-nm',type=Path)
 a=p.parse_args(argv);out=a.output.resolve()
 require(out.is_relative_to(ROOT/'build')and not out.exists(),'new owned validation output required')
 result=verify(a.firmware,a.build,a.llvm_nm);out.parent.mkdir(parents=True,exist_ok=True)
 with out.open('x')as f:json.dump(result,f,indent=2)
 print(json.dumps(dict(passed=True,target='z8-c311',candidate_sha256=result['candidate_sha256'],patched_entries=result['patched_entries'])))
 return result
if __name__=='__main__':main()
