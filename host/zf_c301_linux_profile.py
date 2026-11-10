"""Hash-check own Zf Linux transport helpers. No transport activation."""
import hashlib
import json
from pathlib import Path
from zf_c301_ui_dispatch import _payload
ROOT=Path(__file__).resolve().parents[1]
PREIMAGES={
    'receive':(0x40ca00cc,32,'bf9fe11f2633038dbc9a1f2168ee550a6258f9414d73ae90f538f71595e1048c'),
    'allocate':(0x40ca0b24,272,'b38a3cb063338bfe5ad2b1f66a6fa64ec345011b879c0f02b9835889a37f471f'),
    'release':(0x40ca0cd4,380,'ed89ad6bb675d58f9d83f9728144aef8dbac59f8249ed6d896c18b4923260313'),
    'offset':(0x40ca0e50,60,'e8bc7c2b7893c4a6f7c15f1f7f829f9a10c5bb5dace31f5f634d4c928fe45b57'),
    'try_lock':(0x4123b3e0,192,'205e780c58b5726d5dfa78d69899a97cc7e9d38125f9d0aa3694e561badc8cbc'),
    'unlock':(0x4123b4a0,116,'f4272efd7eff1af76025af753dbbb8437d8bf1fea51e668453d6d5f7dedcb23e'),
    'enqueue':(0x4123ae78,400,'f0e09ded8ac6769fbf60c9409ae437d0da40e0953101867a493616bb72d7bc05'),
    'notify':(0x4123a470,148,'73dbbd78405d07b222a569bf504f4bc6d9b3b341f9ee4475801417b598da1cda'),
}
def prepare(application,output):
    output=Path(output).resolve()
    if not output.is_relative_to(ROOT/'build') or output==ROOT/'build' or output.exists():
        raise ValueError('choose a new output directory below build/')
    data=_payload(application)
    for name,(address,length,digest) in PREIMAGES.items():
        at=address-0x40160000
        if hashlib.sha256(data[at:at+length]).hexdigest()!=digest:
            raise ValueError('Zf Linux helper differs: '+name)
    output.mkdir(parents=True)
    lines=['/* Own Zf checked helper windows; not complete ABI/runtime qualification. */',
           '#ifndef FX_ZF_LINUX_PROFILE_H','#define FX_ZF_LINUX_PROFILE_H']
    lines += [f'#define FX_ZF_LINUX_{name.upper()} UINT64_C(0x{address:x})' for name,(address,_,_) in PREIMAGES.items()]
    (output/'fx_zf_linux_profile.h').write_text('\n'.join(lines+['#endif','']),encoding='ascii')
    result=dict(schema='fenestra.zf-c301-linux-helpers/v1',helpers={name:dict(address=hex(a),bytes=n,sha256=h) for name,(a,n,h) in PREIMAGES.items()},
                runtime_qualified=False,transport_activated=False,firmware_output_generated=False)
    (output/'linux-profile.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result
