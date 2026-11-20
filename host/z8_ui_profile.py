"""Exact Z8 C3.11 native UI metadata, original bytes and bounded ABI checks.

This module is independent of address correspondence generation. It verifies
native layout and call-site prerequisites; it does not claim physical camera
validation. All addresses refer to the SHA-256-pinned unmodified MAIN image.
"""
import copy
import hashlib
import json
import struct
from pathlib import Path

BASE = 0x40160000
SHA256 = '2b43a31c9de838f84b12416de4348a7004e65be07f853ec56ec2b10f3a8e3853'
INIT_FILE, INIT_RAM, INIT_BYTES = 0x4b13000, 0xa02808000, 0x2ad000

FONT_HOOKS = dict(lock=0x402e5550, unlock=0x402e5564,
    line_probe=0x402eeb38, measure=0x4088c620, set_layout=0x4088bdac,
    enter=[0x402ede28,0x402ee174,0x402ee28c,0x402ef13c,0x402ef284,
           0x402ef3e0,0x402ef598,0x402ef7e4,0x402ef8a0])
UI_DISPATCH_HOOKS = dict(poll=0x403d71d4,wait=0x403d7350,
    event_wait=0x403dba70,event_group=0x403db984,current_task=0x401865d8,
    event_set=0x403dba50,event_set_native=0x40d2bc8c,wake_reference=0x403dccc8,wake_mask=0x200000,
    argument_setups={0x404012b0:0x40401290,0x405072b8:0x4050729c},
    loops=[(0x403fe284,0x403fe2dc),(0x40401264,0x404012b0),(0x40507274,0x405072b8),
           (0x40616888,0x4061694c),(0x40618b84,0x40618be0)])
EDITOR_EVENTS = [
    (0x283,0x4050d034,0,0),(0x284,0x4050d038,0,0),
    (0x285,0x4050d03c,0,0),(0x286,0x4050d040,0,0),
    (0x29d,0x4050d04c,0,0),(0x29e,0x4050d050,0,0),
    (0x29f,0x4050d050,0x403b1c64,0),(0x2a0,0x4050d04c,0x403b1c64,0),
    (0x2ae,0x4050d088,0,0),(0x2b1,0x4050d08c,0,0),
    (0x302,0x4050d094,0,0),(0x304,0x4050d098,0,0)]
PROFILE = dict(schema='fenestra.z8-ui-profile/v1',target='z8-c311',application_sha256=SHA256,
    setup=dict(id=663,count=44,descriptor=0xa028ca040,header=0x43d595c8,
        rows=0x43d596d8,events=0x43d59e68,template_row_index=42,first_text=640,template_text=889),
    text=dict(base=3136,languages=36,count_address=0xa0281cd5c,font_root=0xa02f2ac40,
        offset_root=0xa02f2ac48,pool_root=0xa02f2ac38,attribute_root=0xa02f2ac50),
    navigation=dict(address=0x43bff8e8,terminal=0x3bb,back_event=0x285,
        select_events=[0x286,0x28b,0x29f,0x2ae],stay=0x68c,back=663,
        native_bound_compare=0x403af5e8,ordinary_table_records=15),
    native_pages=dict(first=0x4bffd20,stop=0x68c,kinds={2:1360,0:3,3:131,5:181},
        gaps=[1361,1493,1494],page_zero=0xa028f4cc0,page_roots=0xa032b50c8),
    condition_abi=dict(address=0x40508be8,length=168,
        sha256='affdf1feff0582e1b1805e48124f757ecb71be4cd77b1809b3195166084302c0',
        source_page='x20',selected_event='x22',condition_call=0x40508c8c,return_address=0x40508c90),
    editor=dict(template=0xa029054c0,descriptor=0xa028cad10,source_page=704,
        source_events=0x43d9c360,accept=0x2af,cancel=0x27e,back=0x306,events=EDITOR_EVENTS),
    dialog=dict(descriptor=0xa028b7e40,rows=0x43a8731c,header=0x43a871c8,header_bytes=340,row_count=2),
    widgets=dict(value=0x4ba7,container=0x4ba8,title=0x4bac,
        row_object_bytes=0x140,text_object_bytes=0x118,row_children_offset=0x128),
    font=dict(bytes=170765,resource=0x42d93856,
        sha256='7a0e540470c463af61c02fce080acbbb306a83e65a300a9995a8c1e0e3513b9a'),
    font_hooks=FONT_HOOKS,ui_dispatch_hooks=UI_DISPATCH_HOOKS,
    address_map={
        '0xa028d1a10':0xa028ca040,'0x4317e310':0x43d595c8,'0x4317e420':0x43d596d8,'0x4317ebe0':0x43d59e68,
        '0xa02822824':0xa0281cd5c,'0xa02f4ab30':0xa02f2ac40,'0xa02f4ab38':0xa02f2ac48,
        '0xa02f4ab28':0xa02f2ac38,'0xa02f4ab40':0xa02f2ac50,
        '0xa0290c7f0':0xa029054c0,'0xa028be460':0xa028b7e40,'0x42e9b81c':0x43a8731c,
        '0x43014bb8':0x43bff8e8,'0x42271338':0x42d93856},
    runtime_ready=False)

HOOK_RECORDS = [
    dict(address=0x403fe284, symbol='fx_native_ui_wait_native', kind='ui-main-loop',
         native_target=0x403d7350,
         preimage_sha256='d0872f2bc0f121e59e3dd82301d5b48c8a3e73bc40baeb4d26ea5b6ecef262ee'),
    dict(address=0x403fe2dc, symbol='fx_native_ui_poll_native', kind='ui-main-loop',
         native_target=0x403d71d4,
         preimage_sha256='1cc7084d24a09034f68775ff7b268ac6787d254215817f87bdc8e85ef2070797'),
    dict(address=0x40401264, symbol='fx_native_ui_wait_native', kind='ui-main-loop',
         native_target=0x403d7350,
         preimage_sha256='4e01a37474183ee1d8c1f86240524e47804c545aed5dd848618ddd1f273345d4'),
    dict(address=0x404012b0, symbol='fx_native_ui_poll_native', kind='ui-main-loop',
         native_target=0x403d71d4,
         preimage_sha256='2dd96baa9c8cf29d82ae668a734fbf22aee92398059e174a36fb89ae89185e8f'),
    dict(address=0x40507274, symbol='fx_native_ui_wait_native', kind='ui-main-loop',
         native_target=0x403d7350,
         preimage_sha256='ff387779e2fac2daa4d40f4087a37e0d513c0e2b30370d4110d393e839475456'),
    dict(address=0x405072b8, symbol='fx_native_ui_poll_native', kind='ui-main-loop',
         native_target=0x403d71d4,
         preimage_sha256='f6c6eb03fa77ff335d32931482a52a2ecf3ed465867289e251c1667178ea5f51'),
    dict(address=0x40616888, symbol='fx_native_ui_wait_native', kind='ui-main-loop',
         native_target=0x403d7350,
         preimage_sha256='d4070865dfd95398b7a073bacff8c6632b7191db131898140605fb717b22a001'),
    dict(address=0x4061694c, symbol='fx_native_ui_poll_native', kind='ui-main-loop',
         native_target=0x403d71d4,
         preimage_sha256='b9c34fd4d4ed5db8907609c241b9a5e364f2cece2f664140073379c3a68ddc2f'),
    dict(address=0x40618b84, symbol='fx_native_ui_wait_native', kind='ui-main-loop',
         native_target=0x403d7350,
         preimage_sha256='940c81c3da1f1b693212602b22c0bd1ff841f0e452e07e70c64bdc126b2109c0'),
    dict(address=0x40618be0, symbol='fx_native_ui_poll_native', kind='ui-main-loop',
         native_target=0x403d71d4,
         preimage_sha256='2b07e0abf95b4ee8de0f9021890d31809df14a9f4239f09eb186de0430e47ab8'),
    dict(address=0x402ede28, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='f7b68e30f223a09310bed82a149a2ca0be916b0a3602dd5de482d9e782d182e6'),
    dict(address=0x402ede44, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='facdd5a14b55edb69c44409b6d687dfdf17b1b27818b62dfc531e4a689f652d8'),
    dict(address=0x402ee174, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='090008496721f92fac0a12b5037d71ac8c1af4a677173702a8304aeb84408f28'),
    dict(address=0x402ee190, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='a8f6ec4c3669652a874a194b330c535e5bf64282429e9ba1d7f7a11f1837116a'),
    dict(address=0x402ee28c, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='29ce9ec550985fc1b4ca2e7201db20398dfad755e56d504e36b29c965bb5cc60'),
    dict(address=0x402ee2a8, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='745184dee95914408e94596f66f16f5298d05049a1e5d4824d8bc56884778323'),
    dict(address=0x402ef13c, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='f00ac1ffc2ed9e9d0df068abab6b253dd7e77c4368e56c8fe8033c9e06b81091'),
    dict(address=0x402ef158, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='97ccd01b44236462775456cc0d6d7cc012a2072ef372179edc47d5f9a19ea146'),
    dict(address=0x402ef284, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='eff7d1494006c2996ebbc16c0dae2010b4b2432b515dbdbfb3533dc3a47ac662'),
    dict(address=0x402ef2a0, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='f75fe5c020d87bf809cf39cbdfc1e76532e4bef17e1fea52bf6ae7dc37037aaf'),
    dict(address=0x402ef3e0, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='45b60595da0b880b524cf3d525374a08ede7160897af5e3f82f0f4099432abe8'),
    dict(address=0x402ef3fc, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='50bfbe2f60ff671bf9bc0452aaaa41fd3c9ed7f712ec628eca2b2d7bb36249ac'),
    dict(address=0x402ef598, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='646e990d570e16a321b51c4e2a040fc0e0ac1eefe0027409095d2e20730b25d2'),
    dict(address=0x402ef5b4, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='96e828dd2962668b1871766bdb1590634c438f15d1e9eacfaf63b8d1d8f26d1f'),
    dict(address=0x402ef7e4, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='11302f747afd4d66149eb973cd34853dd0db44843bc3baab4d47b9e030f6822f'),
    dict(address=0x402ef800, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='9bda30b18ae85e04c7b548c76a24d52012a3d3147b8bf61100f9b20b7a435090'),
    dict(address=0x402ef8a0, symbol='fx_z8_font_enter', kind='font-request',
         native_target=0x402e5550,
         preimage_sha256='f44a380b9019bf8d42392113fe4ce2e1b1d10000377c54c28e5f0a0b98d01a41'),
    dict(address=0x402ef8bc, symbol='fx_z8_font_leave', kind='font-request',
         native_target=0x402e5564,
         preimage_sha256='48c7db7fccc2a3e8041366d65f0dd72eefd80555256d1e9d81095fb3e94d4e26'),
    dict(address=0x402eeb38, symbol='fx_z8_font_line_metrics', kind='font-line-probe',
         native_target=0x4088c620,
         preimage_sha256='9574f7285fd96df12f002de94eac8315a5018252a65a6b8cab24fabedba9bb82'),
]

PROFILE["hook_records"]=HOOK_RECORDS
PROFILE["ui_hooks"]={r["address"]:r["symbol"]for r in HOOK_RECORDS if r["kind"]=="ui-main-loop"}
PROFILE["font_sites"]={r["address"]:r["symbol"]for r in HOOK_RECORDS if r["kind"]!="ui-main-loop"}
PROFILE["navigation"].update(navigate=0x403af53c,apply=0x403ad6a4,current_page=0x403dbdb4)
PROFILE["text"].update(table_directory_file=0x311c5a0,blob_directory_file=0x387af58,attribute_directory_file=0x387ae38)


def get_profile():
    return copy.deepcopy(PROFILE)


def read_original(data,address,size):
    if BASE <= address < BASE+len(data):
        offset=address-BASE
    elif INIT_RAM <= address and address+size <= INIT_RAM+INIT_BYTES:
        offset=INIT_FILE+address-INIT_RAM
    else:
        raise ValueError('uninitialized original UI address '+hex(address))
    chunk=data[offset:offset+size]
    if len(chunk)!=size:raise ValueError('short original UI read')
    return chunk


def _call(address,target,link=True):
    delta=target-address
    if delta%4 or not -(1<<27)<=delta<(1<<27):raise ValueError('native branch out of range')
    return struct.pack('<I',(0x94000000 if link else 0x14000000)|((delta//4)&0x3ffffff))


def verify_ui_fields(data):
    if hashlib.sha256(data).hexdigest()!=SHA256:raise ValueError('wrong original Z8 UI identity')
    checks=[]
    def need(value,message):
        if not value:raise ValueError(message)
    def raw(a,n):return read_original(data,a,n)
    def u64(a):return struct.unpack('<Q',raw(a,8))[0]
    def u32(a):return struct.unpack('<I',raw(a,4))[0]
    p=PROFILE;s=p['setup'];pages=p['native_pages'];nav=p['navigation'];editor=p['editor']
    page=pages['page_zero']+s['id']*96
    need(u32(page)==(s['id']<<16)|2,'Z8 Setup identity differs')
    need(u64(page+80)==s['descriptor'],'Z8 Setup descriptor differs')
    need(struct.unpack('<IIQQQH',raw(s['descriptor'],34))==(0x80001002,0,s['header'],s['rows'],s['events'],s['count']),'Z8 Setup fields differ')
    need(u32(s['rows'])==s['first_text'] and u32(s['rows']+44*s['template_row_index'])==s['template_text'],'Z8 Setup row style differs')
    checks.append(dict(name='setup.layout',page=hex(page),descriptor_bytes=64,page_bytes=96,row_bytes=44,count=s['count']))
    counts={};gaps=[]
    for page_id in range(1,pages['stop']):
        identity=struct.unpack_from('<I',data,pages['first']+(page_id-1)*96)[0]
        kind=identity&65535;counts[kind]=counts.get(kind,0)+1
        if not identity:gaps.append(page_id)
        else:need(identity>>16==page_id and kind in (2,3,5),'Z8 native page identity differs')
    need(counts==pages['kinds'] and gaps==pages['gaps'],'Z8 native page namespace differs')
    need(u32(nav['native_bound_compare'])==0x711a329f,'Z8 native navigator stop selector differs')
    checks.append(dict(name='native.page.namespace',first_file=pages['first'],stop=pages['stop'],kinds=counts,gaps=gaps))
    records=[struct.unpack('<QQQQ',raw(nav['address']+i*32,32))for i in range(nav['ordinary_table_records'])]
    need([r[0]for r in records]==[0x27e,0x27f,0x283,0x284,0x285,0x286,0x28b,0x29d,0x29e,0x29f,0x2a0,0x2ae,0x281,0x306,0x3bb],'Z8 ordinary navigation events differ')
    need(records[0]==(0x27e,0x403b1bac,0,1) and records[1]==(0x27f,0x403b1bb0,0x403b1c7c,0x68c),'Z8 ordinary MENU/application return differs')
    need(all(next(r for r in records if r[0]==event)[1]==0x403b1ba8 for event in nav['select_events']),'Z8 select callbacks differ')
    need(records[-1]==(0x3bb,0,0,0x68c),'Z8 event terminal differs')
    checks.append(dict(name='navigation.ordinary',address=hex(nav['address']),records=len(records)))
    condition=p['condition_abi'];window=raw(condition['address'],condition['length'])
    need(hashlib.sha256(window).hexdigest()==condition['sha256'],'Z8 condition caller ABI differs')
    need(raw(condition['condition_call'],4)==bytes.fromhex('00013fd6'),'Z8 condition indirect branch differs')
    checks.append(dict(name='event.condition.caller',**condition))
    need(u32(editor['template'])==(editor['source_page']<<16)|2 and u64(editor['template']+80)==editor['descriptor'],'Z8 editor template differs')
    need(u32(editor['descriptor'])==0x8000c010,'Z8 keyboard descriptor kind differs')
    source={}
    for i in range(21):
        record=struct.unpack('<QQQQ',raw(editor['source_events']+i*32,32));source[record[0]]=record
    for event,callback,condition,_ in EDITOR_EVENTS:
        need(source[event][1:3]==(callback,condition),'Z8 editor callback differs')
        need(source[event][3]==nav['stay'],'Z8 editor event transition differs')
    need(source[editor['accept']][3]==nav['stay']+1 and source[editor['back']][3]==nav['stay']+1,'Z8 editor accept/back differs')
    checks.append(dict(name='editor.original.events',page=editor['source_page'],events=len(EDITOR_EVENTS)))
    d=p['dialog'];need(u32(d['descriptor'])==0x80001108 and u64(d['descriptor']+8)==d['header'] and u64(d['descriptor']+16)==d['rows'],'Z8 dialog fields differ')
    need(struct.unpack('<H',raw(d['descriptor']+32,2))[0]==2 and u32(d['header']+4)==30 and u32(d['header']+72)==31,'Z8 dialog shape differs')
    checks.append(dict(name='dialog.original.layout',descriptor=hex(d['descriptor']),rows=2,header_bytes=340))
    # Each language has 3137 offset endpoints. All 36 tables are contiguous
    # with exact 0x3104 strides; each blob ends at the next language blob.
    text=p['text'];languages=text['languages'];count=text['base']
    tables=struct.unpack_from('<36Q',data,text['table_directory_file'])
    blobs=struct.unpack_from('<36Q',data,text['blob_directory_file'])
    need(len(set(tables))==languages and len(set(blobs))==languages,'Z8 locale directory aliases differ')
    locale=[]
    for language,(table,blob) in enumerate(zip(tables,blobs)):
        offsets=struct.unpack('<3137i',raw(table,(count+1)*4))
        need(offsets[0]==0 and all(0<=a<=b and a%2==b%2==0 for a,b in zip(offsets,offsets[1:])),'Z8 UTF16 offsets differ')
        if language+1<languages:
            need(table+(count+1)*4==tables[language+1],'Z8 text count/table stride differs')
            need(blob+offsets[-1]==blobs[language+1],'Z8 text language blob boundary differs')
        raw(blob,offsets[-1]).decode('utf-16le')
        locale.append(dict(language=language,table=hex(table),blob=hex(blob),strings=count,blob_bytes=offsets[-1]))
    need(not any(data[text['blob_directory_file']+languages*8:text['blob_directory_file']+(languages+1)*8]),'Z8 language directory tail differs')
    checks.append(dict(name='locale.original.tables',languages=languages,strings=count,offset_endpoints=count+1,language_tables=locale))
    for record in HOOK_RECORDS:
        preimage=raw(record['address'],4)
        need(hashlib.sha256(preimage).hexdigest()==record['preimage_sha256'],'Z8 UI hook preimage differs')
        need(preimage==_call(record['address'],record['native_target']),'Z8 UI hook destination differs')
    f=FONT_HOOKS
    for a in f['enter']:
        need(raw(a,4)==_call(a,f['lock']) and raw(a+28,4)==_call(a+28,f['unlock']),'Z8 paired font mutex calls differ')
        need(raw(a+4,4)==bytes.fromhex('e0031f32') and raw(a+20,4)==bytes.fromhex('e0031f32'),'Z8 font completion bit differs')
    need(raw(f['line_probe'],4)==_call(f['line_probe'],f['measure']) and raw(f['line_probe']-16,4)==_call(f['line_probe']-16,f['set_layout']),'Z8 font line probe differs')
    font=p['font'];need(hashlib.sha256(raw(font['resource'],font['bytes'])).hexdigest()==font['sha256'],'Z8 embedded font resource differs')
    checks.append(dict(name='font.paired.requests',requests=9,font_bytes=font['bytes'],line_probe=hex(f['line_probe'])))
    u=UI_DISPATCH_HOOKS
    for wait,poll in u['loops']:
        need(raw(wait,4)==_call(wait,u['wait']) and raw(poll,4)==_call(poll,u['poll']),'Z8 UI main-loop calls differ')
        need(raw(wait-8,4)==_call(wait-8,u['event_group']) and raw(wait+16,4)==_call(wait+16,u['event_wait']),'Z8 UI event wait chain differs')
        need(raw(wait+4,4)==bytes.fromhex('e203002a'),'Z8 UI timeout argument differs')
        start=u['argument_setups'].get(poll,poll-4)
        need(u32(start)&0xffc003ff==0x910003e0,'Z8 UI flags pointer differs')
        for address in range(start+4,poll,4):
            word=u32(address)
            need(word&0xffc003ff in (0x390003ff,0x790003ff,0xf90003ff) or word&0xffc07fff==0xa9007fff,'Z8 original UI flags pointer clobbered')
    need(hashlib.sha256(raw(u['current_task'],8)).hexdigest()=='42810809c1288bee7e5f0b8743a134e2486a4b93ea7041006c6c220d13d02298','Z8 current-task TPIDR ABI differs')
    wake=u['wake_reference'];setter=u['event_set']
    need(raw(wake,12)==_call(wake,u['event_group'])+bytes.fromhex('e1030b32')+_call(wake+8,setter),'Z8 UI wake producer differs')
    need(hashlib.sha256(raw(setter,8)).hexdigest()=='22755fbc7b9ff15cbf25d362629d4fac4657bb7440248e22574b914627c4506d' and raw(setter+8,4)==_call(setter+8,u['event_set_native'],False) and u32(setter+12)==0xd65f03c0,'Z8 UI event setter ABI differs')
    checks.append(dict(name='ui.main.loops',loops=5,replaced_calls=10,wake_mask=u['wake_mask'],nested_timers_replaced=False))
    return dict(schema='fenestra.z8-ui-original-checks/v1',target='z8-c311',application_sha256=SHA256,
        checks=checks,runtime_ready=False,limitations=['Static original metadata and caller ABI prerequisites only.',
        'No physical camera menu, display, input, USB, power-off or warm boot was exercised.'])


def header():
    p=PROFILE;s=p['setup'];nav=p['navigation'];ed=p['editor'];w=p['widgets']
    values=dict(SETUP_ID=s['id'],TEXT_BASE=p['text']['base'],LANGUAGES=p['text']['languages'],
        SETUP_ROW_COUNT=s['count'],TEMPLATE_ROW_INDEX=s['template_row_index'],
        SETUP_FIRST_TEXT=s['first_text'],TEMPLATE_TEXT=s['template_text'],NATIVE_STOP=p['native_pages']['stop'],
        NAV_TERMINAL=nav['terminal'],BACK_EVENT=nav['back_event'],STAY=nav['stay'],FONT_BYTES=p['font']['bytes'],
        EDITOR_ACCEPT_ID=ed['accept'],EDITOR_CANCEL_ID=ed['cancel'],EDITOR_BACK_ID=ed['back'],
        EDITOR_EVENTS_COUNT=len(EDITOR_EVENTS),WIDGET_VALUE_ID=w['value'],WIDGET_CONTAINER_ID=w['container'],WIDGET_TITLE_ID=w['title'])
    values.update({'SELECT_EVENT_'+str(i):x for i,x in enumerate(nav['select_events'])})
    lines=['/* Generated, independently checked Z8 C3.11 UI semantics. */','#ifndef FX_Z8_FULL_UI_H','#define FX_Z8_FULL_UI_H']
    lines += ['#define FX_Z8_UI_'+name+' '+hex(value)+'u' for name,value in values.items()]
    entries=['{'+','.join(hex(value)+'ull' for value in event)+'}' for event in EDITOR_EVENTS]
    lines.append('#define FX_Z8_UI_EDITOR_EVENTS_INIT {'+','.join(entries)+'}')
    lines += ['#endif','']
    return '\n'.join(lines)


def generate(application,output):
    data=Path(application).read_bytes();result=verify_ui_fields(data);out=Path(output)
    out.mkdir(parents=True,exist_ok=True)
    (out/'profile.json').write_text(json.dumps(get_profile(),indent=2)+'\n')
    (out/'original-checks.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'z8_full_ui.h').write_text(header())
    return result


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(generate(args.firmware,args.output),indent=2))
