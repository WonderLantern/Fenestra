"""Generate owned C2.01 bindings only after complete original verification."""
import json
from pathlib import Path


def generate(profile, original, out):
    card={row['name']:row['address'] for row in profile['bindings']}
    Path(out,'z63_startup_bindings.h').write_text('/* Verified C2.01 normal filesystem entries. */\n'+
        ''.join('#define FX_Z63_STARTUP_'+name.upper()+' '+hex(card['camera.card.'+name])+'ull\n'
                for name in ('open','read','write','close','remove')))
    lines = ['/* Generated locally from the pinned C2.01 input. */',
             '#include "fenestra_accounting_service.h"',
             '#include "fenestra_capacity.h"',
             'static const FxNamedBinding fx_target_bindings[]={']
    for row in profile['bindings']:
        at = row['address'] - profile['image_base']
        span = original[at:at + 16]
        lines.append('{'+json.dumps(row['name'])+',FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,'+
                     hex(row['address'])+'ull,{'+','.join(map(str,span))+'}},')
    for row in profile.get('native_tables', []):
        lines.append('{'+json.dumps(row['name'])+',FX_BIND_TABLE,FX_CALL_BYTES,1,FX_BIND_READ|FX_BIND_ESCAPE,FX_SDK_BINDING,'+
                     str(row['bytes'])+','+hex(row['address'])+'ull,{0}},')
    for name, kind, abi, cap in [('core.schedule','DEVICE','DEVICE','QUERY'),
            ('core.critical','DEVICE','DEVICE','NATIVE'),('rtos.mappings','DEVICE','DEVICE','NATIVE'),('rtos.events','DEVICE','DEVICE','NATIVE'),('core.machine','DEVICE','DEVICE','NATIVE'),
            ('core.memory','DEVICE','DEVICE','MEMORY'),('card.files','DEVICE','DEVICE','FILES'),
            ('core.hooks','HOOK','EVENTS','NATIVE'),('core.tick','HOOK','EVENTS','QUERY'),
            ('core.services','DEVICE','DEVICE','BINDING')]:
        lines.append('{'+json.dumps(name)+',FX_BIND_'+kind+',FX_CALL_'+abi+',1,0,FX_SDK_'+cap+',0,1,{0}},')
    lines.insert(0,'#include "fenestra_tasks.h"\n#include "fenestra_native_scene.h"\n#include "fenestra_ui_document.h"\n#include "fenestra_native_ui_callbacks.h"\n#include "fenestra_native_editor.h"')
    lines.extend(['#ifdef FX_NATIVE_UI_INPUT',
        '{"core.ui.document",FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_MENU,0,(uintptr_t)fx_ui_document_resource,{0}},',
        '{"camera.ui.input.text",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_ui_text_input,{0}},',
        '{"camera.ui.editor.page.build",FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)fx_native_editor_build,{0}},',
        '#endif'])
    lines.insert(0,'#include "fenestra_irq.h"\nextern int64_t fx_native_event_signal(uintptr_t,uint32_t);')
    for name, symbol in [('rtos.irq.mailbox','fx_native_irq_mailbox'),('rtos.event.signal','fx_native_event_signal')]:
        lines.append('{'+json.dumps(name)+',FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)'+symbol+',{0}},')
    for condition, name, symbol, cap in [('FX_NATIVE_TASKS','core.tasks','fx_native_tasks_resource','NATIVE'),('FX_NATIVE_SCENE','camera.display.scene','fx_native_scene_resource','MENU')]:
        lines.extend(['#ifdef '+condition, '{'+json.dumps(name)+',FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_'+cap+',0,(uintptr_t)'+symbol+',{0}},','#endif'])
    for name, symbol in [('core.context' ,'fx_native_context'),('core.translation','fx_native_translation')]:
        lines.append('{'+json.dumps(name)+',FX_BIND_FUNCTION,FX_CALL_U64_8,1,FX_BIND_ESCAPE,FX_SDK_NATIVE,0,(uintptr_t)'+symbol+',{0}},')
    for name, symbol in [('core.resources','fx_account_resource'),('core.capacity','fx_capacity_resource')]:
        lines.append('{'+json.dumps(name)+',FX_BIND_DEVICE,FX_CALL_DEVICE,1,FX_BIND_PROVIDER,FX_SDK_QUERY,0,(uintptr_t)'+symbol+',{0}},')
    lines += ['};', '#define FX_TARGET_IDENTITY {'+','.join(map(str,bytes.fromhex(profile['application_sha256'])))+'}',
              '#define FX_TARGET_MODULE_IDENTITY {'+','.join(map(str,bytes.fromhex(profile['payload_sha256'])))+'}',
              '#define FX_TARGET_MODEL "Nikon Z6III"', '#define FX_TARGET_VERSION "C 2.01"',
              '#define FX_TARGET_CARD_STREAM_LIMIT 0u', '#define FX_TARGET_CARD_STREAM_EXHAUSTED 0u']
    Path(out, 'platform_bindings.h').write_text('\n'.join(lines)+'\n')
