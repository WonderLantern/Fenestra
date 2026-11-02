"""Prepare the hash-pinned Zf C3.01 native binding inventory.

The Zf builder uses its own profile, alongside the original Z9/ZR builders.
The two memory wrappers and Zf table values are explicitly rebound; original
instruction windows are verified before any local headers are emitted.
"""
import argparse
import hashlib
import json
from pathlib import Path
from zf_c301_card_profile import PREIMAGES as CARD_PREIMAGES
from zf_c301_sdk_profile import FUNCTIONS as SDK_FUNCTIONS, TABLES as SDK_TABLES, verify as verify_sdk

from platform_profiles import PROFILES
from zf_c301_profile import load_profile
from zf_native_menu import MENU_HOOKS

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x40160000

# The normalized-window audit found 58 Z9 RTOS entries at the same Zf address.
# Copy the names/addresses into a private map, then override the only two
# wrappers whose address changed in the Zf image.
RTOS_FUNCTIONS = {name: address for name, address in PROFILES['z9-c532']['functions'].items()
                  if name.startswith('rtos.')}
RTOS_FUNCTIONS.update({
    'rtos.memory.allocate': 0x40D31898,
    'rtos.memory.release': 0x40D318D8,
})
if len(RTOS_FUNCTIONS) != 60:
    raise RuntimeError('Z9 RTOS inventory no longer has the expected 60 entries')

RTOS_TABLES = {
    'rtos.mmu.context': dict(address=0xA04D53580, bytes=0x58,
                             source='Zf static wrapper decode; live ownership unverified'),
    'rtos.irq.controller': dict(address=0xA04D4C0A0, bytes=None,
                                source='Zf static IRQ base decode; table extent unverified'),
    'rtos.memory.pools': dict(address=0xA033504F0, bytes=64,
                              source='Zf pool geometry and runtime snapshot; allocator ABI unqualified'),
}

MENU_FUNCTIONS = {
    'camera.menu.page.get': MENU_HOOKS['page'],
    'camera.menu.text.get': MENU_HOOKS['text'],
    'camera.menu.navigate': MENU_HOOKS['navigate'],
}

# Four anchors guard the high-risk corrections against an accidental profile
# edit. The remaining entries are still checked for range/non-zero windows.
ANCHOR_WINDOWS = {
    'rtos.memory.allocate': 'b241e1c41aaea6b57fa90c8daeb7083d6cb3ee9afacc031e256e06aa69805784',
    'rtos.memory.release': '5f565821f1e9e00cf1ed34aa990cd23d6ccb92170442a477da0505b9d009021e',
    'rtos.mmu.context.map': '94b8f18dbe20550e7207323ee3d2039c5c9f823b42035e45d84f2bb76f23a87d',
    'rtos.irq.register': '48a8495120a47779f9f179739221a915becee992925b0cf6fc1c76556a1a436b',
}


def _payload(path):
    profile = load_profile()
    raw = Path(path).read_bytes()
    module = profile['original_eg_module']
    if len(raw) == module['bytes_with_crc']:
        if hashlib.sha256(raw).hexdigest() != module['sha256_with_crc']:
            raise ValueError('Zf EG with CRC differs')
        raw = raw[:-2]
    elif len(raw) != module['payload_bytes'] or hashlib.sha256(raw).hexdigest() != module['payload_sha256']:
        raise ValueError('Zf EG payload differs')
    return raw, profile


def verify_application(path):
    payload, profile = _payload(path)
    sdk=verify_sdk(payload)
    rows = []
    for name, address in RTOS_FUNCTIONS.items():
        at = address - BASE
        if at < 0 or at + 16 > len(payload):
            raise ValueError(f'{name} is outside the pinned Zf EG')
        window = payload[at:at + 16]
        if not any(window):
            raise ValueError(f'{name} has an empty original window')
        digest = hashlib.sha256(window).hexdigest()
        if name in ANCHOR_WINDOWS and digest != ANCHOR_WINDOWS[name]:
            raise ValueError(f'{name} anchor window differs')
        rows.append(dict(name=name, address=hex(address), window_sha256=digest,
                         abi_qualified=False, runtime_qualified=False))
    menu_rows = []
    card_rows = []
    for key in ('open','read','write','seek64','close','rename','directory_open','directory_next','directory_close'):
        address,size,expected=CARD_PREIMAGES[key]
        window=payload[address-BASE:address-BASE+size]
        if hashlib.sha256(window).hexdigest()!=expected:
            raise ValueError('Zf card binding differs: '+key)
        card_rows.append(dict(name='camera.card.'+key.replace('_','.'),address=hex(address),bytes=size,
                              sha256=expected))
    for name, spec in MENU_FUNCTIONS.items():
        at = spec['address'] - BASE
        raw = payload[at:at + spec['bytes']]
        digest = hashlib.sha256(raw).hexdigest()
        if digest != spec['sha256']:
            raise ValueError(f'{name} menu entry window differs')
        menu_rows.append(dict(name=name, address=hex(spec['address']), bytes=spec['bytes'],
                              window_sha256=digest, abi_qualified=False, runtime_qualified=False,
                              complete_function_verified=(name != 'camera.menu.navigate')))
    return dict(schema='fenestra.zf-c301-bindings/v1', target='zf-c301',
                application_sha256=hashlib.sha256(payload).hexdigest(),
                application_bytes=len(payload),
                model=profile['model'], firmware=profile['firmware'],
                functions=rows, menu_functions=menu_rows, card_functions=card_rows,tables=RTOS_TABLES,sdk=sdk,
                same_address_from_z9=58, zf_specific_rebindings=2,
                native_bindings_verified=False, runtime_qualified=False,
                firmware_output_generated=False,
                limitations=['static entry windows do not prove complete functions or ABI',
                             'MMU/IRQ table ownership and extent are not qualified',
                             'no runtime registration, camera operation or package output'])


def _header(result, payload):
    lines = [
        '/* Generated from the complete, hash-pinned Zf C3.01 EG. */',
        '#ifndef FENESTRA_ZF_C301_BINDINGS_H',
        '#define FENESTRA_ZF_C301_BINDINGS_H',
        '#include "fenestra_platform.h"',
        '#include "fenestra_native_context.h"',
        '#include "fenestra_translation.h"',
        '#include "fenestra_native_editor.h"',
        'extern int64_t fx_native_irq_mailbox(uintptr_t,uintptr_t);',
        'extern int64_t fx_native_event_signal(uintptr_t,uint32_t);',
        'extern FxResult fx_account_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);',
        'extern FxResult fx_capacity_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);',
        '#ifdef FX_NATIVE_SCENE',
        'extern FxResult fx_native_scene_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);',
        '#endif',
        '/* This inventory is not a production target binding. */',
        'static const FxNamedBinding fx_zf_c301_rtos_bindings[] = {',
    ]
    for row in result['functions']:
        address = int(row['address'], 16)
        original = payload[address - BASE:address - BASE + 16]
        lines.append('    {"%s",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,UINT64_C(0x%x),{%s}},' %
                     (row['name'], address, ','.join(str(byte) for byte in original)))
    for row in result.get('card_functions',[]):
        address=int(row['address'],16);original=payload[address-BASE:address-BASE+16]
        lines.append('    {"%s",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,UINT64_C(0x%x),{%s}},' %
                     (row['name'],address,','.join(str(byte) for byte in original)))
    # The target exposes one inventory. Include the checked native menu
    # entries here as well as in the standalone inspection inventory below.
    for row in result['menu_functions']:
        address=int(row['address'],16);original=payload[address-BASE:address-BASE+16]
        lines.append('    {"%s",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_MENU,0,UINT64_C(0x%x),{%s}},' %
                     (row['name'],address,','.join(str(byte) for byte in original)))
    # Same context layout consumed by the shared mapping and hook providers.
    # Its own-Zf address is decoded from the checked context.map wrapper.
    table=RTOS_TABLES['rtos.mmu.context']
    lines.append('{"rtos.mmu.context",FX_BIND_TABLE,FX_CALL_BYTES,1,FX_BIND_READ|FX_BIND_ESCAPE,FX_SDK_BINDING,%du,UINT64_C(0x%x),{0}},' %
                 (table['bytes'],table['address']))
    for name,address in SDK_FUNCTIONS.items():
        original=payload[address-BASE:address-BASE+16]
        lines.append('    {"%s",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,UINT64_C(0x%x),{%s}},' %
                     (name,address,','.join(str(byte) for byte in original)))
    for name,(address,size) in SDK_TABLES.items():
        lines.append('    {"%s",FX_BIND_TABLE,FX_CALL_BYTES,1,FX_BIND_READ|FX_BIND_ESCAPE,FX_SDK_BINDING,%du,UINT64_C(0x%x),{0}},' %
                     (name,size,address))
    lines.append('{"camera.ui.editor.page.build",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_editor_build,{0}},')
    # Same shared builtins as Z9. These are core functions, not firmware
    # addresses; retaining the original binding ABI needs no model mapping.
    for name, symbol in [('core.context','fx_native_context'),
                         ('core.translation','fx_native_translation'),
                         ('rtos.irq.mailbox','fx_native_irq_mailbox'),
                         ('rtos.event.signal','fx_native_event_signal')]:
        lines.append('{"'+name+'",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)'+symbol+',{0}},')
    # Same shared providers as Z9: these contain no model addresses.
    lines.append('{"core.resources",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_QUERY,0,(uintptr_t)fx_account_resource,{0}},')
    lines.append('{"core.capacity",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_QUERY,0,(uintptr_t)fx_capacity_resource,{0}},')
    for name,kind,abi,cap in [('core.schedule','DEVICE','DEVICE','QUERY'),('core.critical','DEVICE','DEVICE','NATIVE'),('rtos.mappings','DEVICE','DEVICE','NATIVE'),('rtos.events','DEVICE','DEVICE','NATIVE'),('core.machine','DEVICE','DEVICE','NATIVE'),('core.memory','DEVICE','DEVICE','MEMORY'),('card.files','DEVICE','DEVICE','FILES'),('core.hooks','HOOK','EVENTS','NATIVE'),('core.tick','HOOK','EVENTS','QUERY'),('core.services','DEVICE','DEVICE','BINDING')]:
        lines.append('{"'+name+'",FX_BIND_'+kind+',FX_CALL_'+abi+',1,0,FX_SDK_'+cap+',0,1,{0}},')
    lines += ['#ifdef FX_NATIVE_SCENE',
              '    {"camera.display.scene",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_MENU,0,(uintptr_t)fx_native_scene_resource,{0}},',
              '#endif']
    # Declarations belong outside the initializer. Keep optional providers
    # behind the same build guards used by the original target generator.
    for guard, name, symbol, kind, abi, cap, declaration in [
        ('FX_NATIVE_UI_INPUT','camera.ui.input.text','fx_native_ui_text_input','FUNCTION','U64_8','NATIVE',
         'extern FxResult fx_native_ui_text_input(uint32_t,const char *,uint32_t);'),
        ('FX_NATIVE_UI_INPUT','core.ui.document','fx_ui_document_resource','DEVICE','DEVICE','MENU',
         'extern FxResult fx_ui_document_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);'),
        ('FX_NATIVE_TASKS','core.tasks','fx_native_tasks_resource','DEVICE','DEVICE','NATIVE',
         'extern FxResult fx_native_tasks_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);')]:
        lines.insert(lines.index('/* This inventory is not a production target binding. */'),
                     '#ifdef '+guard+'\n'+declaration+'\n#endif')
        flags='FX_BIND_ESCAPE' if kind=='FUNCTION' else 'FX_BIND_PROVIDER'
        lines += ['#ifdef '+guard,
                  '{"'+name+'",FX_BIND_'+kind+',FX_CALL_'+abi+',1,'+flags+',FX_SDK_'+cap+',0,(uintptr_t)'+symbol+',{0}},',
                  '#endif']
    lines += ['};', 'static const FxNamedBinding fx_zf_c301_menu_bindings[] = {']
    for row in result['menu_functions']:
        address = int(row['address'], 16)
        original = payload[address - BASE:address - BASE + 16]
        lines.append('    {"%s",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_MENU,0,UINT64_C(0x%x),{%s}},' %
                     (row['name'], address, ','.join(str(byte) for byte in original)))
    lines += [
        '};',
        '#define FX_ZF_C301_RTOS_BINDING_COUNT (sizeof(fx_zf_c301_rtos_bindings)/sizeof(fx_zf_c301_rtos_bindings[0]))',
        '#define FX_ZF_C301_MENU_BINDING_COUNT (sizeof(fx_zf_c301_menu_bindings)/sizeof(fx_zf_c301_menu_bindings[0]))',
        '#define fx_target_bindings fx_zf_c301_rtos_bindings',
        '#define FX_TARGET_IDENTITY {' + ','.join(str(byte) for byte in bytes.fromhex(result['application_sha256'])) + '}',
        '#define FX_TARGET_MODULE_IDENTITY FX_TARGET_IDENTITY',
        '#define FX_TARGET_MODEL "Nikon Zf"',
        '#define FX_TARGET_VERSION "C 3.01"',
        '#define FX_TARGET_CARD_STREAM_LIMIT 0u',
        '#define FX_TARGET_CARD_STREAM_EXHAUSTED UINT32_C(0)',
        '#define FX_ZF_C301_MMU_CONTEXT UINT64_C(0xa04d53580)',
        '#define FX_ZF_C301_IRQ_CONTROLLER UINT64_C(0xa04d4c0a0)',
        '#define FX_ZF_C301_POOL_TABLE UINT64_C(0xa033504f0)',
        '#endif',
    ]
    return '\n'.join(lines) + '\n'


def prepare(application, output):
    output = Path(output).resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('choose a new output directory below build/')
    payload, _ = _payload(application)
    result = verify_application(application)
    output.mkdir(parents=True)
    (output / 'platform_bindings.h').write_text(_header(result, payload), encoding='utf-8')
    result['header'] = 'platform_bindings.h'
    (output / 'bindings.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.application, args.output_dir), indent=2))


if __name__ == '__main__':
    main()
