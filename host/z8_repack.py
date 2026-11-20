"""Build a Z8 ciphertext patch from an independently verified complete core.

The explicitly selected external NikonFirmwarePatch reader/writer remains a
separate, source-hash-pinned local dependency. Its algorithms are not copied.
This tool never accesses USB, storage cards or a camera.
"""
import argparse,hashlib,json,tempfile
from pathlib import Path
import binascii
from cipher_patch import atomic_new,make_patch
import z8_container
ROOT=Path(__file__).resolve().parents[1]

def sha(data):return hashlib.sha256(data).hexdigest()
def require(ok,message):
 if not ok:raise ValueError(message)

def produce(firmware,build,validation,external_project,work,output):
 raw,kind=z8_container.read_input(firmware)
 require(kind=='package','exact original encrypted Z8 C3.11 package required')
 build=Path(build).resolve(strict=True);work=Path(work).resolve();output=Path(output).resolve()
 require(build.is_relative_to(ROOT/'build') and work.is_relative_to(ROOT/'build')and not work.exists(),'new owned work path required')
 require(not output.exists()and not output.is_symlink()and output.parent.is_dir(),'new patch output with existing parent required')
 report=json.loads((build/'build.json').read_text());checked=json.loads(Path(validation).read_text())
 require(report.get('target')=='z8-c311'and report.get('schema')=='fenestra.native-core/v1','complete Z8 core build required; diagnostic core is not admitted')
 require(checked.get('passed')is True and checked.get('target')=='z8-c311','complete Z8 independent verification required')
 replacement=(build/'z8-core.main').read_bytes()
 require(len(replacement)==z8_container.IDENTITIES['application']['bytes']and sha(replacement)==report.get('candidate_sha256')==checked.get('candidate_sha256'),'candidate/verification identity differs')
 for name,digest in report['source_sha256'].items():
  source=(ROOT/name).resolve(strict=True)
  require(source.is_relative_to(ROOT)and sha(source.read_bytes())==digest,'compiled source changed: '+name)
 reader,provenance=z8_container._load_external_reader(external_project)
 original=reader(raw)
 z8_container.verify_identity(bytes(original.eg),z8_container.IDENTITIES['component'],'original MAIN component')
 require(binascii.crc_hqx(original.eg,0)==0,'original component CRC')
 component=replacement+binascii.crc_hqx(replacement,0).to_bytes(2,'big')
 packet,decoded=original.replace_eg(component,z8_container.TARGET['module_name'])
 require(len(packet)==len(raw)and bytes(decoded.eg)==component,'container round trip differs')
 # Independently compare every directory record and non-MAIN byte range.
 require(len(decoded.modules)==len(original.modules)==5,'component count changed')
 for index,(before,after)in enumerate(zip(original.modules,decoded.modules)):
  require((before['name'],before['start'],before['bytes'])==(after['name'],after['start'],after['bytes']),'component directory changed')
  if index!=2:
   start,end=before['start'],before['start']+before['bytes']
   require(before==after and original.body[start:end]==decoded.body[start:end],'non-MAIN component changed')
 # All plaintext outside MAIN and final package CRC must be byte-identical.
 start=original.modules[2]['start'];end=start+original.modules[2]['bytes']
 require(original.body[:start]==decoded.body[:start]and original.body[end:-16]==decoded.body[end:-16]and original.body[-14:]==decoded.body[-14:],'undeclared plaintext change')
 # Rerun the independent verifier before producing a patch; a JSON flag alone
 # cannot admit an image. The extracted stock MAIN is temporary local input.
 from verify_z8_core import verify as verify_core
 with tempfile.TemporaryDirectory(prefix='z8-repack-verify-',dir=ROOT/'build')as temporary:
  stock=Path(temporary)/'stock-main.bin';stock.write_bytes(bytes(original.eg[:-2]))
  independent=verify_core(stock,build)
 require(json.loads(json.dumps(independent))==checked,'independent full-core validation receipt differs')
 work.mkdir(parents=True)
 manifest=make_patch(raw,packet,'z8-c311',output)
 provenance['invocation']='Package(original); replace_eg(verified MAIN plus CRC, original module name); complete readback'
 receipt=dict(schema='fenestra.z8-repack/v1',target='z8-c311',original_sha256=sha(raw),result_sha256=sha(packet),application_sha256=sha(replacement),bytes=len(packet),external_dependency=provenance,non_main_components_unchanged=True,plaintext_outside_main_unchanged=True,full_core_validation_sha256=sha(Path(validation).read_bytes()),physical_qualified=False,installed=False,patch=str(output),manifest=manifest)
 atomic_new(work/'repack.json',(json.dumps(receipt,indent=2)+'\n').encode())
 return receipt

def produce_from_original(firmware,external_project,work,output):
 from repack import build_core
 work=Path(work).resolve()
 require(work.is_relative_to(ROOT/'build')and not work.exists(),'new owned full-build work directory required')
 raw,kind=z8_container.read_input(firmware)
 require(kind=='package','exact original encrypted Z8 C3.11 package required')
 application,_=z8_container.application_from_bytes(raw,external_project=external_project)
 work.mkdir(parents=True)
 original=work/'original-application.bin';original.write_bytes(application)
 build_core('z8-c311',original,work/'core')
 return produce(firmware,work/'core',work/'core/verification.json',external_project,work/'repack',output)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in('firmware','build','validation','external-project','work','output'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();r=produce(a.firmware,a.build,a.validation,a.external_project,a.work,a.output);print(json.dumps({k:r[k]for k in('target','result_sha256','application_sha256','bytes','patch','installed')}))
if __name__=='__main__':main()
