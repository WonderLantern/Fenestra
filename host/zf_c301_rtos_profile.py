"""Verify own Zf native RTOS debug helper preimages, without patching firmware."""
import hashlib
import json
from pathlib import Path
from zf_c301_ui_dispatch import _payload
ROOT=Path(__file__).resolve().parents[1]
PREIMAGES={
 'command':(0x40c018a8,452,'7cd5a4a36a604289ee1939d46a5549eda9a716b4c8232b01863f538385f45f3e'),
 'format':(0x4016a918,92,'66b6dffe9bbcb2a49a37ff236f40343fc4a98d0f3f5bcdb5f44ccd1043d2eca9'),
 'create':(0x40185064,16,'779f945a18359468198e50bc455e267dc7b95a38ca438b2b5a4b958c067dda1a'),
 'start':(0x401855e0,16,'2cf1702ed32e92dc76bb8d294ac198d1c292f0609a5c74a59e0c589e95405106'),
 'delay':(0x40187b00,16,'ed1e5ffea9164db1ea314fd29f40c97e3fbce8291fec64c59892ff1e7af80995'),
}
def prepare(application,output):
    output=Path(output).resolve()
    if not output.is_relative_to(ROOT/'build') or output==ROOT/'build' or output.exists():
        raise ValueError('choose a new output directory below build/')
    data=_payload(application)
    for name,(address,length,digest) in PREIMAGES.items():
        at=address-0x40160000
        if hashlib.sha256(data[at:at+length]).hexdigest()!=digest:
            raise ValueError('Zf RTOS helper differs: '+name)
    output.mkdir(parents=True)
    lines=['/* Hash-checked Zf C3.01 helpers. Command is NOT the registration wrapper. */',
           '#ifndef FX_ZF_RTOS_PROFILE_H','#define FX_ZF_RTOS_PROFILE_H']
    lines += [f'#define FX_ZF_RTOS_{name.upper()} UINT64_C(0x{address:x})' for name,(address,_,_) in PREIMAGES.items()]
    (output/'fx_zf_rtos_profile.h').write_text('\n'.join(lines+['#endif','']),encoding='ascii')
    result=dict(schema='fenestra.zf-c301-rtos-helpers/v1',helpers={name:dict(address=hex(a),bytes=n,sha256=h) for name,(a,n,h) in PREIMAGES.items()},
                runtime_qualified=False,ptp_policy_qualified=False,firmware_output_generated=False)
    (output/'rtos-profile.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result
