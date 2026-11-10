"""Z8 C3.11 complete native profile, from explicit reviewed correspondences.

Missing native addresses are errors. A Z9 address is never a fallback.
Static correspondence and compiled support do not imply hardware qualification.
"""
import copy,json
from pathlib import Path
from z8_ui_profile import get_profile,verify_ui_fields
ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/'targets/z8-c311-native.json'
SHA256='2b43a31c9de838f84b12416de4348a7004e65be07f853ec56ec2b10f3a8e3853'

def native_manifest():
 p=json.loads(MANIFEST.read_text())
 if p.get('target')!='z8-c311' or p.get('inputs',{}).get('z8')!=SHA256:raise ValueError('wrong Z8 complete native address manifest')
 return p

def mapped_address(address, manifest=None):
 p=native_manifest() if manifest is None else manifest;key=hex(address)
 record=p['mappings'].get(key)
 if not record:raise ValueError('unresolved complete Z8 native address '+key)
 return int(record['address'],16)

def build_profile(z9):
 ui=get_profile();p=copy.deepcopy(z9);manifest=native_manifest()
 p.update(model='Nikon Z8',version='C 3.11',bytes=81807709,sha256=SHA256,module_identity=SHA256,
          port_scope='complete-native-core',physical_qualified=False,runtime_ready=False,
          blockers=['Physical camera qualification remains outstanding.'],
          abi_evidence=['host/z8_ui_profile.py','host/verify_z8_core.py','host/verify_native_namespace.py'])
 p['functions']={name:mapped_address(a,manifest)for name,a in z9['functions'].items()}
 p['tables']={name:(ui['address_map'].get(hex(a),0) or mapped_address(a,manifest),size)for name,(a,size)in z9['tables'].items()}
 p['native_pages']=dict(first=ui['native_pages']['first'],stop=ui['native_pages']['stop'],kinds=ui['native_pages']['kinds'])
 return p

def card_capacity_profile():
 # These call sites are independently rechecked against the original image by
 # platform_profiles.verify_card_capacity; symbol correspondence is only input.
 calls=[(0x402dc960,0x3c,0x408b94f8),(0x408b94f8,0x40,0x408bb1f8),(0x408bb1f8,0x50,0x408bcd54)]
 chain=[(mapped_address(a)+offset,mapped_address(b))for a,offset,b in calls]
 return chain,mapped_address(0x408bcd54)+0x50,mapped_address(0x408bb1f8)+0x78
