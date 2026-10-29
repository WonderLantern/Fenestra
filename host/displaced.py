"""Materialize local-only assembly includes from an already pinned original image."""
from pathlib import Path
SITES = {'zr-c110': {'fx_c110_system_original': 1089954020, 'fx_c110_navigate_original': 1077656184, 'fx_c110_ptp_original_lookup': 1082362848, 'fx_c110_ptp_original_session': 1082357264, 'fx_c110_ptp_original_operations': 1077295332, 'fx_c110_rtos_original_printf': 1088444176, 'fx_c110_original_text': 1077126764}, 'z9-c532': {'fx_z9_system_original': 1087789720, 'fx_z9_navigate_original': 1077606980, 'fx_z9_ptp_original_lookup': 1082011148, 'fx_z9_ptp_original_session': 1082005556, 'fx_z9_ptp_original_operations': 1077243996, 'fx_z9_rtos_original_printf': 1086347028}}
def prepare(target,original,out):
    generated=[]
    for label,address in SITES[target].items():
        data=original[address-0x40160000:address-0x40160000+16]
        if len(data)!=16:raise ValueError('displaced window out of bounds')
        path=Path(out)/(label+'.inc')
        path.write_text('.byte '+','.join(str(b) for b in data)+'\n',encoding='ascii')
        generated.append(path)
    return generated
