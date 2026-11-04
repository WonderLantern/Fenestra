"""Own-Zf serialized card wrappers; module staging remains read-only."""
import hashlib,json
from pathlib import Path
from zf_c301_ui_dispatch import _payload
ROOT=Path(__file__).resolve().parents[1]
PREIMAGES={
 'open':(0x402e661c,96,'eeca831b6b537621f1647bafe23adfec7e3f3ff7b3417b4659f519fbfb4cb743'),
 'read':(0x402e67ec,196,'92103dc89361b71ca6c4fa4526e0ea0250f84d156de66c4d352e0b11a6ac0f65'),
 # Complete original wrappers: write returns status and an accumulated count;
 # seek64 returns BOOL and preserves the full offset/output-position ABI.
 'write':(0x402e68b0,248,'41abf7457d78fe49adefdcfcbcc1e43b1b12f01846605985c6636985d355d157'),
 'seek64':(0x402e6a14,108,'01607d1d13d080db06c781f497ad68cd8ae5f68ffce10b29435b2b6e07157e49'),
 'close':(0x402e667c,72,'7b673f30579c96292919a4d1b0fd4a9ef60a02ff5f2d306f6831f41f5e30edbe'),
 'remove':(0x402e6b78,72,'f569c1ffb0287ea01029a142c47885ce9b47671909152ef481407856d1e30c6d'),
 'rename':(0x402e6b28,80,'c2a4fd66d65f97b385e8a1ce02bb79f5860c3144bb10a4a2ca03eb00f737937f'),
 'open_mode':(0x408cad14,676,'8a170c584732e580863342dbc54cd4fc596f4f57fba4fc99c0494627c4cc3d24'),
 'directory_open':(0x402e6d98,80,'e0bb2c3e9165ef3b645e6851c7991587a53ac1bc1fbcd490fc577b6b1ec0b4e3'),
 'directory_next':(0x402e6e48,80,'b8fb253b9654bdfabb99f40166403f3c19690a0af2df5a3f808ca3fe47229271'),
 'directory_close':(0x402e6e98,72,'d5a14dfd57f16141be78ef7ce30be9b421e4fad23c27e31ad926f3c715df9f83'),
 'directory_open_body':(0x408c0e1c,1116,'bc50caeefb79278fe0da097e7f23eeed969b3cf05915b0076a170a414472c24f'),
 'directory_next_body':(0x408c1458,792,'9289320c6a943d5bd5d49308aeee65c8de8271d580bb3ba001a46c80821b9859'),
}
def prepare(application,output):
    output=Path(output).resolve()
    if not output.is_relative_to(ROOT/'build') or output==ROOT/'build' or output.exists():
        raise ValueError('choose a new output directory below build/')
    data=_payload(application)
    for name,(a,n,h) in PREIMAGES.items():
        if hashlib.sha256(data[a-0x40160000:a-0x40160000+n]).hexdigest()!=h:
            raise ValueError('Zf card helper differs: '+name)
    output.mkdir(parents=True)
    lines=['/* Checked complete Zf C3.01 wrapper preimages; no runtime claim. */',
           '#ifndef FX_ZF_CARD_PROFILE_H','#define FX_ZF_CARD_PROFILE_H']
    lines += [f'#define FX_ZF_CARD_{name.upper()} UINT64_C(0x{a:x})' for name,(a,_,_) in PREIMAGES.items() if name!='open_mode']
    (output/'fx_zf_card_profile.h').write_text('\n'.join(lines+['#endif','']),encoding='ascii')
    result=dict(schema='fenestra.zf-c301-card-profile/v1',helpers={name:dict(address=hex(a),bytes=n,sha256=h) for name,(a,n,h) in PREIMAGES.items()},
        native_open_flags=[0,0],read_only_paths='A:\\FENESTRA\\[a-z0-9_-]+.wlm',
        runtime_qualified=False,firmware_output_generated=False,
        limitations=['This receipt verifies static wrappers and emits build definitions only.'])
    (output/'card-profile.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
