"""Run a disposable C2.01 full-system module smoke test with external inputs.

Requires a matching nikon-z63 QEMU and its z63log call-trace plugin. No firmware,
model, physical camera connection or update-package installation is supplied.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import struct
import sys
import tempfile
import time


def clone(source, destination):
    if sys.platform == 'darwin':
        result = subprocess.run(['cp', '-c', str(source), str(destination)], capture_output=True)
        if result.returncode == 0:
            return
    shutil.copyfile(source, destination)


def scanout_fraction(reference, candidate):
    def ppm(path):
        data = Path(path).read_bytes()
        match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
        if not match:
            raise ValueError('unsupported QEMU PPM header')
        width, height = (int(value) for value in match.groups())
        pixels = data[match.end():]
        if not 1 <= width <= 4096 or not 1 <= height <= 4096 or len(pixels) != width * height * 3:
            raise ValueError('invalid QEMU PPM extent')
        return width, height, pixels
    width, height, before = ppm(reference)
    other_width, other_height, after = ppm(candidate)
    if (width, height) != (other_width, other_height):
        return 0.0
    # Fixed synthetic Bayer control, central lower ROI. Allow a small number of
    # changing native overlays. The ROI ends before the native histogram;
    # this is a test criterion, not a camera metric.
    matched = total = 0
    for y in range(height // 2, height * 3 // 4, 4):
        for x in range(width // 4, width * 9 // 16, 4):
            at = (y * width + x) * 3
            matched += before[at:at + 3] == after[at:at + 3]
            total += 1
    return matched / total if total else 0.0


def exercise_linux_agent(bridge, executable, out):
    from linux_debug import LinuxDebug
    report=bridge.bootstrap(executable)
    debug=LinuxDebug(bridge.exchange)
    info=debug.info()
    def complete(job):
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            status=debug.status(job)
            if status['done']:return status
            time.sleep(.05)
        raise RuntimeError('Linux agent job did not complete')
    # Read back the exact uploaded executable, independently of gzip's check.
    handle,size=debug.file_open(report['directory']+'/agent')
    copied=bytearray()
    try:
        while len(copied)<size:
            block=debug.file_read(handle,len(copied),min(992,size-len(copied)))
            if not block:raise RuntimeError('Linux executable readback ended early')
            copied.extend(block)
    finally:debug.file_close(handle)
    if bytes(copied)!=executable.read_bytes():raise RuntimeError('Linux uploaded ELF differs')
    job=debug.execute(['/bin/sh','-c',"printf 'Fenestra-agent-exec'"])['id']
    status=complete(job);output=debug.read(job)['data'];debug.close_job(job)
    if status['exit_code'] or status['signal'] or output!=b'Fenestra-agent-exec':
        raise RuntimeError('Linux argv execution differs')
    job=debug.execute(['/bin/sleep','10'])['id'];debug.cancel(job)
    status=complete(job);debug.close_job(job)
    if status['signal'] not in (9,15):raise RuntimeError('Linux owned-job cancellation differs')
    path=report['directory']+'/roundtrip.bin';data=bytes(range(256))*2+b'Z63'
    handle,size=debug.file_open(path,2)
    try:
        if size or debug.file_write(handle,0,data)!=len(data):raise RuntimeError('Linux RAM file write differs')
        debug.file_sync(handle)
        if debug.file_read(handle,0,len(data))!=data:raise RuntimeError('Linux RAM file readback differs')
    finally:debug.file_close(handle)
    maps=debug.maps(info['pid'])
    match=re.match(rb'([0-9a-f]+)-[0-9a-f]+\s+r',maps)
    if not match or debug.memory_read(info['pid'],int(match[1],16),4)!=b'\x7fELF':
        raise RuntimeError('Linux agent maps/self-memory control differs')
    bridge.stop()
    deadline=time.monotonic()+5
    while bridge.status()['ready'] and time.monotonic()<deadline:time.sleep(.05)
    if bridge.status()['ready']:raise RuntimeError('Linux agent stop not acknowledged')
    result=dict(agent_tested=True,agent_sha256=report['agent_sha256'],agent_bytes=report['agent_bytes'],
        executable_exact_readback=True,argv=True,cancellation=True,ram_file_bytes=len(data),
        self_maps_and_memory=True,stop_acknowledged=True,passed=True)
    (out/'linux-agent.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def panic_pixel_control(path):
    data=Path(path).read_bytes();match=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
    if not match:raise ValueError('panic screenshot format')
    width,height=(int(n) for n in match.groups());pixels=data[match.end():]
    if not 1<=width<=4096 or not 1<=height<=4096 or len(pixels)!=width*height*3:
        raise ValueError('panic screenshot extent')
    dark=red=total=0
    for i in range(0,len(pixels),12):
        r,g,b=pixels[i:i+3];total+=1
        dark+=max(r,g,b)<=30
        red+=r>=200 and 55<=g<=160 and abs(g-b)<=10
    return dict(dark_fraction=dark/total,red_fraction=red/total,passed=dark/total>.7 and red/total>.001)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('qemu', 'application', 'receipt', 'loader', 'snor', 'nand', 'mcu', 'card', 'raw', 'plugin', 'output-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--ptp-client', type=Path, help='matching external QEMU USB/IP PTP client, for native debug transport testing')
    parser.add_argument('--seconds', type=float, default=35)
    parser.add_argument('--module-count',type=int,default=1,help='expected inventory size for the runtime control')
    parser.add_argument('--keys', default='', help='comma-separated native MCU key presses after boot')
    parser.add_argument('--native-menu-test', action='store_true', help='navigate Setup/Fenestra, activate, Back and return to live view using MCU keys')
    parser.add_argument('--repeat-activation', action='store_true', help='press Run twice in the native menu and require a single worker attempt')
    parser.add_argument('--startup-test',choices=('manual','guard','autoload','config'),help='check native card startup policy and optional recovery/configuration actions')
    parser.add_argument('--shutdown-test',action='store_true',help='after startup acceptance, use the model power switch and require clean journal retirement')
    parser.add_argument('--panic-test',action='store_true',help='require the retained native panic screen after a deliberate module CPU fault')
    parser.add_argument('--panic-peer-test',action='store_true',help='also require the other module owner to acknowledge native stop')
    parser.add_argument('--rtos-test', action='store_true', help='exercise the RTOS monitor and dedicated native printf worker through PTP')
    parser.add_argument('--linux-test', action='store_true', help='exercise original Linux IPC shell and correlated output through PTP')
    parser.add_argument('--linux-agent',type=Path,help='owned AArch64 ELF for typed RAM bootstrap, job/file/self-memory and stop acceptance')
    parser.add_argument('--sync-test', action='store_true', help='exercise native mutex and semaphore lifetimes on the RTOS debug worker')
    parser.add_argument('--irq-test', action='store_true', help='read native IRQ controller limits and status without changing a line')
    parser.add_argument('--editor-test',action='store_true')
    parser.add_argument('--manager-test',action='store_true')
    parser.add_argument('--tasks-test', action='store_true')
    parser.add_argument('--ui-test', action='store_true')
    parser.add_argument('--dialog-test', action='store_true')
    parser.add_argument('--reader-test', action='store_true')
    parser.add_argument('--demo-test',action='store_true',help='exercise the bundled character-rain module, native Back/blur and reopen')
    parser.add_argument('--expect', choices=('success', 'fault', 'rejected', 'sdk', 'services', 'runtime', 'ui', 'timeout', 'files', 'mapping', 'global'), default='success')
    args = parser.parse_args()
    if args.panic_test and (not args.native_menu_test or args.expect!='fault'):
        parser.error('--panic-test requires --native-menu-test --expect fault')
    if args.panic_peer_test and (not args.panic_test or args.module_count!=2):
        parser.error('--panic-peer-test requires --panic-test --module-count 2')
    if args.linux_agent and not args.linux_test:parser.error('--linux-agent requires --linux-test')
    if args.demo_test and (not args.ui_test or args.expect!='ui' or args.dialog_test or args.reader_test):
        parser.error('--demo-test requires --ui-test --expect ui without another modal control')
    if not 10 <= args.seconds <= 300:
        parser.error('seconds must be 10..300')
    if not 1<=args.module_count<=32 or (args.module_count!=1 and ((args.expect!='runtime' and not args.panic_peer_test) or args.manager_test)):
        parser.error('multiple modules require the runtime control without manager navigation')
    if args.repeat_activation and not args.native_menu_test:
        parser.error('--repeat-activation requires --native-menu-test')
    if (args.rtos_test or args.linux_test) and not args.ptp_client:
        parser.error('--rtos-test and --linux-test require --ptp-client')
    if (args.sync_test or args.irq_test) and not args.rtos_test:
        parser.error('--sync-test and --irq-test require --rtos-test')
    if args.editor_test and not args.manager_test:
        parser.error('--editor-test requires --manager-test')
    if (args.dialog_test or args.reader_test) and not args.ui_test:
        parser.error('--dialog-test and --reader-test require --ui-test')
    if args.dialog_test and args.reader_test:
        parser.error('choose one modal control per run')
    if (args.manager_test or args.ui_test or args.tasks_test) and not args.native_menu_test:
        parser.error('native UI tests require --native-menu-test')
    receipt = json.loads(args.receipt.read_text())
    application = args.application.resolve(strict=True)
    if hashlib.sha256(application.read_bytes()).hexdigest() != receipt['candidate_sha256']:
        raise ValueError('candidate differs from the build receipt')
    out = args.output_dir.resolve()
    if out.exists():
        raise ValueError('new output directory required')
    out.mkdir(parents=True)
    clone(args.nand.resolve(strict=True), out / 'nand.img')
    clone(args.card.resolve(strict=True), out / 'card.img')
    symbols = receipt['symbols']
    observed = ['fx_z63_sysctl_before', 'fx_z63_sysctl_after', 'fx_z63_probe_module_result']
    if args.ptp_client:
        observed += ['fx_native_ptp_lookup','fx_ptp_hello','fx_ptp_submit','fx_ptp_fetch','execute']
    if args.reader_test:
        observed += ['fx_native_ui_page_open','fx_native_ui_refresh_owner','fx_native_ui_poll_native',
            'fx_native_ui_wait_native','fx_native_ui_flush_owner','fx_native_ui_reader_refresh',
            'fx_native_row_visibility','fx_native_row_arrow','fx_native_row_value_visibility',
            'fx_native_text_prepare','fx_native_ui_order_page','fx_native_row_reflow',
            'fx_native_backdrop_open','fx_native_ui_open','fx_ui_page_reference','fx_ui_focus_open','fx_native_ui_capture_task']
    if receipt.get('menu'):
        observed += ['activate', 'fx_z63_menu_navigate']
    if any(name not in symbols for name in observed):
        raise ValueError('receipt does not describe a module experiment')
    with tempfile.TemporaryDirectory(prefix='fx63-') as temp:
        sock = Path(temp) / 'qmp'
        machine = ','.join(['nikon-z63', 'loader=' + str(args.loader.resolve(strict=True)),
            'snor-user=' + str(args.snor.resolve(strict=True)), 'nand=' + str(out / 'nand.img'),
            'mcu-image=' + str(args.mcu.resolve(strict=True)), 'z9-capture=on', 'cfexpress=on',
            'z9-capture-raw=' + str(args.raw.resolve(strict=True)), 'sio-sink=9:8:7',
            'imu-mg=1000:0:0', 'scanout-dump=' + str(out / 'frame.ppm')])
        if args.ptp_client:
            if not receipt.get('ptp') or not args.native_menu_test:
                raise ValueError('PTP test requires a PTP candidate and native menu test')
            machine += ',usb-cable=off,usbip=' + str(Path(temp) / 'usb')
        command = [str(args.qemu.resolve(strict=True)), '-machine', machine, '-cpu', 'cortex-a53',
            '-smp', '4,maxcpus=5', '-bios', str(application), '-serial', 'file:' + str(out / 'serial.log'),
            '-monitor', 'none', '-display', 'none', '-nic', 'none', '-action', 'shutdown=pause',
            '-drive', 'file=' + str(out / 'card.img') + ',if=none,id=card,format=raw',
            '-device', 'nvme,drive=card,serial=LOCALTEST,bus=dw-pcie,addr=0',
            '-qmp', 'unix:' + str(sock) + ',server=on,wait=off',
            '-plugin', str(args.plugin.resolve(strict=True)) + ',out=' + str(out / 'calls.log') +
            ',call=' + ':'.join([*[hex(symbols[name]) for name in observed],
                *(['0x40250af8','0x402514e8','0x4032a2dc','0x4032a2e0'] if args.reader_test else []),
                *(['0x403204d0','0x40c15bfc','0x403222e0','0x40322308','0x40321d7c','0x40323028','0x40322498','0x40322900','0x40320724','0x40320820'] if args.ptp_client else [])])]
        start = time.monotonic()
        with (out / 'qemu.log').open('wb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=log)
            connection = None
            try:
                deadline = time.monotonic() + 10
                while not sock.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(.05)
                connection = socket.socket(socket.AF_UNIX)
                connection.settimeout(10)
                connection.connect(str(sock))
                stream = connection.makefile('rwb')
                stream.readline()

                def qmp(name, arguments=None):
                    value = {'execute': name}
                    if arguments is not None:
                        value['arguments'] = arguments
                    stream.write(json.dumps(value).encode() + b'\n')
                    stream.flush()
                    while True:
                        line = stream.readline()
                        if not line:
                            raise RuntimeError('QMP closed')
                        response = json.loads(line)
                        if 'return' in response:
                            return response['return']
                        if 'error' in response:
                            raise RuntimeError(response['error'])

                qmp('qmp_capabilities')
                time.sleep(args.seconds)
                qmp('screendump', {'filename': str(out / 'before-menu.ppm')})
                views = []

                def read64(address):
                    value = qmp('human-monitor-command', {'command-line': 'xp /1gx ' + hex(address)})
                    words = re.findall(r'0x([0-9a-fA-F]+)', value)
                    if not words:
                        raise RuntimeError('native view read failed')
                    return int(words[0], 16)

                def view():
                    vector = read64(0xa03420de8)
                    snapshot = {'vector': hex(vector)}
                    def save_view():
                        with (out / 'native-views.jsonl').open('a') as evidence:
                            evidence.write(json.dumps(snapshot) + '\n')
                    if not 0xa00000000 <= vector < 0xa40000000 - 24:
                        save_view();return 0
                    stride = read64(vector) & 0xffffffff
                    count = read64(vector + 8) & 0xffffffff
                    data = read64(vector + 16)
                    snapshot.update(stride=stride, count=count, data=hex(data))
                    if stride != 8 or not 1 <= count <= 40 or not 0xa00000000 <= data < 0xa40000000 - 328:
                        save_view();return 0
                    page = read64(data + (count - 1) * stride)
                    snapshot['page'] = hex(page)
                    if not 0xa00000000 <= page < 0xa40000000 - 96:
                        save_view();return 0
                    chain = []
                    for _ in range(16):
                        chain.append(hex(page))
                        child = read64(page + 16)
                        if not child:
                            break
                        if not 0xa00000000 <= child < 0xa40000000 - 96 or hex(child) in chain:
                            raise RuntimeError('invalid native view chain')
                        page = child
                    else:
                        raise RuntimeError('native view chain exceeds bound')
                    snapshot['chain'] = chain
                    identity = read64(page) & 0xffffffff
                    snapshot['identity'] = hex(identity)
                    if identity & 65535 == 2:
                        if args.ui_test:
                            chrome=read64(page+24);descriptor=read64(page+80)
                            header=read64(descriptor+8)
                            title_id=(read64(header+0x78)>>32)&0xffffffff
                            snapshot['title_id']=title_id
                            snapshot['title_resource']=read64(header+0x40)>>32
                            title_nodes=[];node=read64(chrome+0xc8);first=node
                            for _ in range(min(read64(chrome+0xd8)&0xffffffff,32)):
                                if not 0xa00000000<=node<0xa40000000-24:break
                                obj=read64(node+16)
                                if 0xa00000000<=obj<0xa40000000-0xc0:
                                    title_nodes.append(dict(object=hex(obj),id=read64(obj)&0xffffffff,
                                        destructor=hex(read64(obj+0x58)),draw=hex(read64(obj+0x48)),resource_source=hex(read64(obj+8))))
                                node=read64(node+8)
                                if node==first:break
                            snapshot['chrome_children']=title_nodes
                        surface=read64(page+32)
                        snapshot['content_surface']=hex(surface)
                        if 0xa00000000<=surface<0xa40000000-0x118:
                            snapshot['surface_draw']=hex(read64(surface+0x48))
                            snapshot['surface_extended_draw']=hex(read64(surface+0xb0))
                            snapshot['surface_list_head']=hex(read64(surface+0xc8))
                            snapshot['surface_list_count']=read64(surface+0xd8)&0xffffffff
                            nodes=[];node=read64(surface+0xc8);first=node
                            for _ in range(min(snapshot['surface_list_count'],4)):
                                if not 0xa00000000<=node<0xa40000000-24:break
                                child=read64(node+16)
                                if 0xa00000000<=child<0xa40000000-0xc0:
                                    item=dict(object=hex(child),id=read64(child)&0xffffffff,draw=hex(read64(child+0x48)),extended_draw=hex(read64(child+0xb0)),destructor=hex(read64(child+0x58)))
                                    if read64(child+0x48)==0x4032b3dc:
                                        parts=[];cursor=read64(child+0x128);head=cursor
                                        for _ in range(min(read64(child+0x138)&0xffffffff,4)):
                                            if not 0xa00000000<=cursor<0xa40000000-24:break
                                            obj=read64(cursor+16)
                                            if 0xa00000000<=obj<0xa40000000-0x70:parts.append(dict(object=hex(obj),id=read64(obj)&0xffffffff,draw=hex(read64(obj+0x48))))
                                            cursor=read64(cursor+8)
                                            if cursor==head:break
                                        item['children']=parts
                                    nodes.append(item)
                                node=read64(node+8)
                                if node==first:break
                            snapshot['surface_children']=nodes


                    save_view()
                    views.append(identity)
                    return identity

                def keypress(key):
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': key})
                    time.sleep(.5)
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': ''})
                    time.sleep(1.5)

                def touch(point):
                    qmp('qom-set',{'path':'/machine','property':'touch','value':point})
                    time.sleep(.2)
                    qmp('qom-set',{'path':'/machine','property':'touch','value':''})
                    time.sleep(2)

                ptp_result = None
                task_result = None
                manager_result = None
                reader_result = None
                startup_result = None
                panic_result = None
                demo_result = None
                menu_completed = False
                menu_exit_presses = 0
                scanout_matches = None
                menu_matches = None
                if args.native_menu_test:
                    if not receipt.get('menu') or args.keys:
                        raise ValueError('native menu test requires a menu image and no --keys')
                    keypress('menu')
                    if view() & 65535 != 2:
                        keypress('menu')
                    keypress('left')
                    keypress('left')
                    for _ in range(8):
                        if view() >> 16 == 690:
                            break
                        keypress('up')
                    if view() >> 16 != 690:
                        raise RuntimeError('native Setup tab not reached')
                    keypress('right')
                    keypress('right')
                    if view() >> 16 != 0x7000:
                        raise RuntimeError('native Fenestra page not reached')
                    qmp('screendump', {'filename': str(out / 'fenestra.ppm')})
                    if args.startup_test:
                        if not receipt.get('runtime') or args.ptp_client or not args.native_menu_test or (args.expect!='runtime' and not args.demo_test):
                            raise ValueError('startup control requires the normal discovered runtime test')
                        image=application.read_bytes();base=0x40160000
                        boot_layout=struct.unpack_from('<7I',image,symbols['fx_z63_startup_layout']-base)
                        if boot_layout[:2]!=(0x46584236,1):raise RuntimeError('Startup observation layout differs')
                        boot=read64(0xa02912e10+80)+boot_layout[2]
                        def boot_state():
                            return dict(zip(('phase','desired','error','shutdown_done'),
                                [read64(boot+offset)&0xffffffff for offset in boot_layout[3:]]))
                        def boot_wait(predicate):
                            deadline=time.monotonic()+12
                            while time.monotonic()<deadline:
                                current=boot_state()
                                if predicate(current):return current
                                time.sleep(.1)
                            raise RuntimeError('Startup state did not settle: '+repr(current))
                        expected_phase=2 if args.startup_test=='guard' else 5 if args.startup_test=='autoload' else 1
                        initial=boot_wait(lambda s:s['phase']==expected_phase)
                        startup_result=dict(mode=args.startup_test,initial=initial)
                        if args.startup_test=='guard':
                            keypress('ok')
                            if boot_state()['phase']!=2:raise RuntimeError('Guard failed to block activation')
                            trace=(out/'calls.log').read_text()
                            if re.search(r'call pc='+hex(symbols['fx_z63_probe_module_result'])+r' x0=0x4\b',trace):
                                raise RuntimeError('Guarded boot started a module controller')
                            for _ in range(5):keypress('down')
                            keypress('ok');boot_wait(lambda s:s['phase']==1 and not s['error'])
                            qmp('screendump',{'filename':str(out/'startup-cleared.ppm')})
                            for _ in range(5):keypress('up')
                            startup_result['guard_blocked_and_cleared']=True
                        elif args.startup_test=='config':
                            for _ in range(4):keypress('down')
                            transitions=[]
                            for desired in (3,2,3,2):
                                keypress('ok');transitions.append(boot_wait(lambda s:s['desired']==desired and not s['error']))
                            for _ in range(4):keypress('up')
                            startup_result['configuration_transitions']=transitions
                    if args.manager_test:
                        if not receipt.get('native_ui') or args.ptp_client:raise ValueError('manager control requires the normal native UI build')
                        keypress('down');keypress('down');keypress('down');keypress('ok')
                        if view()>>16!=0x704a:raise RuntimeError('Native module manager not reached')
                        image=application.read_bytes();base=0x40160000
                        menu_offset=struct.unpack_from('<I',image,symbols['fx_z63_menu_ui_offset']-base)[0]
                        layout=struct.unpack_from('<4I',image,symbols['fx_z63_manager_layout']-base)
                        if layout[:2]!=(0x46584d36,1):raise RuntimeError('Manager observation layout differs')
                        metadata=struct.unpack_from('<24I',image,symbols['fx_native_modules_metadata']-base)
                        if metadata[:2]!=(0x46584d47,2):raise RuntimeError('Shared manager metadata differs')
                        state=read64(0xa02912e10+80);ui=read64(state+menu_offset);manager=read64(ui+layout[2])
                        def manager_state():
                            count=read64(manager+metadata[4])&0xffffffff;error=read64(manager+metadata[6])&0xffffffff
                            entry=read64(manager+metadata[3])
                            if count!=1 or error or not entry:raise RuntimeError('Manager inventory differs')
                            return dict(enabled=read64(entry+metadata[11])&0xffffffff,valid=read64(entry+metadata[13])&0xffffffff)
                        before=manager_state()
                        editor_result=None
                        if args.editor_test:
                            keypress('ok')
                            if view()>>16!=0x705b:raise RuntimeError('Native editor not reached')
                            qmp('screendump',{'filename':str(out/'editor-initial.ppm')})
                            keypress('ok')
                            qmp('screendump',{'filename':str(out/'editor-typed.ppm')})
                            keypress('menu');time.sleep(1)
                            if view()>>16!=0x704a:raise RuntimeError('Editor cancel did not return to manager')
                            editor_meta=struct.unpack_from('<16I',image,symbols['fx_native_ui_editor_metadata']-base)
                            if editor_meta[:2]!=(0x46584544,2):raise RuntimeError('Editor observation layout differs')
                            editor=read64(manager+layout[3]+editor_meta[3])
                            opened=read64(editor+editor_meta[11])&0xffffffff
                            completed=read64(editor+editor_meta[12])&0xffffffff
                            if opened or completed!=1 or manager_state()!=before:raise RuntimeError('Editor cancel state differs')
                            def accept_touch():
                                qmp('qom-set',{'path':'/machine','property':'touch','value':'950:640'})
                                time.sleep(.2)
                                qmp('qom-set',{'path':'/machine','property':'touch','value':''})
                                time.sleep(2)
                            keypress('ok');keypress('ok');accept_touch()
                            if view()>>16!=0x704a or read64(manager+metadata[22])&65535!=65:
                                raise RuntimeError('Editor submitted text did not reach manager filter')
                            qmp('screendump',{'filename':str(out/'manager-filtered.ppm')})
                            keypress('ok');keypress('delete');accept_touch()
                            if view()>>16!=0x704a or read64(manager+metadata[22])&255 or manager_state()!=before:
                                raise RuntimeError('Editor filter clearing differs')
                            editor_result=dict(native_keyboard=True,typed_key=True,cancelled=True,
                                submitted_text='A',filter_cleared=True,touch_accept=True,passed=True)
                        qmp('screendump',{'filename':str(out/'manager-enabled.ppm')})
                        keypress('down');keypress('down');keypress('ok')
                        if view()>>16!=0x705b:raise RuntimeError('Manager switch picker not reached')
                        keypress('up');keypress('ok');time.sleep(1)
                        if view()>>16!=0x704a:raise RuntimeError('Disabled picker did not return to manager')
                        disabled=manager_state();qmp('screendump',{'filename':str(out/'manager-disabled.ppm')})
                        keypress('ok')
                        if view()>>16!=0x705b:raise RuntimeError('Disabled module picker not reached')
                        keypress('down');keypress('ok');time.sleep(1)
                        enabled=manager_state()
                        if before!=dict(enabled=1,valid=1) or disabled!=dict(enabled=0,valid=1) or enabled!=before:
                            raise RuntimeError('Manager enable/disable/validation differs')
                        manager_result=dict(editor=editor_result,before=before,disabled=disabled,reenabled=enabled,passed=True)
                        keypress('left')
                        if view()>>16!=0x7000:raise RuntimeError('Manager Back route differs')
                        keypress('up');keypress('up');keypress('up')
                    keypress('ok')
                    time.sleep(2)
                    if receipt.get('drawing'):
                        qmp('screendump', {'filename':str(out/'drawing.ppm')})
                        time.sleep(8)
                    if args.ui_test:
                        if not receipt.get('native_ui'):raise ValueError('UI test requires native UI candidate')
                        keypress('down');keypress('down');keypress('ok')
                        if view()>>16!=0x7001:raise RuntimeError('Applications page not reached')
                        qmp('screendump',{'filename':str(out/'applications.ppm')})
                        keypress('ok');time.sleep(2)
                        if view()>>16!=0x7100:raise RuntimeError('Module root page not reached')
                        qmp('screendump',{'filename':str(out/'application.ppm')})
                        if args.demo_test:
                            image=application.read_bytes();base=0x40160000
                            menu_offset=struct.unpack_from('<I',image,symbols['fx_z63_menu_runtime_offset']-base)[0]
                            runtime_meta=struct.unpack_from('<3I',image,symbols['fx_z63_runtime_layout']-base)
                            scene_meta=struct.unpack_from('<17I',image,symbols['fx_native_scene_metadata']-base)
                            display_meta=struct.unpack_from('<12I',image,symbols['fx_z63_scene_layout']-base)
                            runtime=read64(read64(0xa02912e10+80)+menu_offset)
                            owner=read64(read64(runtime+runtime_meta[2]));provider=read64(owner+scene_meta[2])
                            for _ in range(32):
                                if not provider:raise RuntimeError('Demo scene provider absent')
                                name=read64(provider+scene_meta[4])
                                value=b''.join(read64(name+i).to_bytes(8,'little') for i in range(0,24,8))
                                if value.split(b'\0',1)[0]==b'camera.display.scenes':break
                                provider=read64(provider+scene_meta[3])
                            else:raise RuntimeError('Demo provider chain differs')
                            budget=read64(provider+scene_meta[5]);scene=read64(budget+scene_meta[6]);display=scene+display_meta[2]
                            def demo_frames(minimum):
                                deadline=time.monotonic()+120
                                while time.monotonic()<deadline:
                                    frames=read64(display+display_meta[10])&0xffffffff
                                    error=read64(display+display_meta[11])&0xffffffff
                                    if error:raise RuntimeError('Demo renderer error: '+str(error))
                                    if frames>=minimum:return frames
                                    time.sleep(.1)
                                raise RuntimeError('Demo did not publish enough frames')
                            first=demo_frames(3)
                            qmp('screendump',{'filename':str(out/'demo-rain.ppm')})
                            word=demo_frames(211)
                            qmp('screendump',{'filename':str(out/'demo-word.ppm')})
                            released=demo_frames(421)
                            qmp('screendump',{'filename':str(out/'demo-released.ppm')})
                            keypress('left')
                            if view()>>16!=0x7001:raise RuntimeError('Demo Back did not return to applications')
                            keypress('ok')
                            if view()>>16!=0x7100:raise RuntimeError('Demo reopen did not return to its page')
                            second=demo_frames(released+3)
                            qmp('screendump',{'filename':str(out/'demo-reopened.ppm')})
                            demo_result=dict(first_frames=first,word_frames=word,released_frames=released,
                                reopened_frames=second,back_and_reopen=True,passed=True)
                        if args.tasks_test:
                            image=application.read_bytes();code=0x40910000;image_base=0x40160000
                            menu_offset=struct.unpack_from('<I',image,symbols['fx_z63_menu_ui_offset']-image_base)[0]
                            layout=struct.unpack_from('<6I',image,symbols['fx_z63_ui_layout']-image_base)
                            if layout[:2]!=(0x46585536,1):raise RuntimeError('UI observation layout differs')
                            descriptor=read64(0xa02912e10+80);ui=read64(descriptor+menu_offset)
                            if not 0xa00000000<=ui<0xa40000000-65536:raise RuntimeError('UI state unavailable')
                            native_ui_task=read64(ui+layout[2]+layout[3])
                            queued_call_task=read64(ui+layout[2]+layout[4]+layout[5])
                            if not native_ui_task or queued_call_task!=native_ui_task:raise RuntimeError('Copied call did not run on captured original UI task')
                            task_result=dict(original_ui_task=hex(native_ui_task),queued_call_task=hex(queued_call_task),passed=True)

                        if not args.demo_test:keypress('ok');time.sleep(2)
                        if args.dialog_test:
                            if view()>>16!=0x7111:raise RuntimeError('Native modal dialog not reached')
                            qmp('screendump',{'filename':str(out/'dialog.ppm')})
                            keypress('ok');time.sleep(2)
                            if view()>>16!=0x7100:raise RuntimeError('Native modal Accept did not return to root')
                        if args.reader_test:
                            if view()>>16!=0x7111:raise RuntimeError('Native document reader not reached')
                            image=application.read_bytes();base=0x40160000
                            meta=struct.unpack_from('<12I',image,symbols['fx_native_ui_reader_metadata']-base)
                            layout=struct.unpack_from('<8I',image,symbols['fx_z63_ui_layout']-base)
                            menu_offset=struct.unpack_from('<I',image,symbols['fx_z63_menu_ui_offset']-base)[0]
                            if meta[:2]!=(0x46585244,1) or layout[:2]!=(0x46585536,1):raise RuntimeError('Reader observation layout differs')
                            state=read64(0xa02912e10+80);ui=read64(state+menu_offset)
                            node=read64(ui+layout[6]);native_ui=node+layout[7]
                            reader=read64(native_ui+meta[2])
                            (out/'reader-before-input.json').write_text(json.dumps(
                                [read64(reader+at)&0xffffffff for at in meta[3:11]])+'\n')
                            def reader_state():
                                values=[read64(reader+at)&0xffffffff for at in meta[3:11]]
                                if values[0:2]!=[1,0] or values[4:6]!=[1,0]:
                                    qmp('screendump',{'filename':str(out/'reader-failed.ppm')})
                                    editor_meta=struct.unpack_from('<16I',image,symbols['fx_native_ui_editor_metadata']-base)
                                    trace=[read64(native_ui+at)&0xffffffff for at in editor_meta[13:16]]
                                    raise RuntimeError('Reader presentation failed: '+repr(dict(values=values,refresh=trace,ui=hex(native_ui),reader=hex(reader))))
                                return dict(offset=values[2],next=values[3],version=values[6],bytes=values[7])
                            first=reader_state()
                            if first['offset'] or not 0<first['next']<first['bytes']:raise RuntimeError('Reader first-page extent differs')
                            qmp('screendump',{'filename':str(out/'reader-first.ppm')})
                            keypress('ok');time.sleep(2)
                            second=reader_state()
                            if second['offset']!=first['next'] or second['next']<=second['offset']:raise RuntimeError('Reader Next did not advance')
                            qmp('screendump',{'filename':str(out/'reader-next.ppm')})
                            keypress('up');keypress('ok');time.sleep(2)
                            if reader_state()!=first:raise RuntimeError('Reader Previous did not restore first page')
                            reader_result=dict(first=first,second=second,previous_restored=True,passed=True)
                            (out/'reader-state.json').write_text(json.dumps(reader_result,indent=2)+'\n')
                            keypress('left')
                            if view()>>16!=0x7100:raise RuntimeError('Reader Back did not return to module')
                            keypress('down');keypress('ok');time.sleep(2)
                        qmp('screendump',{'filename':str(out/'application-finished.ppm')})
                        keypress('left');keypress('left')
                        if view()>>16!=0x7000:raise RuntimeError('Module Back route failed')
                        keypress('up');keypress('up')
                    if args.repeat_activation:
                        keypress('ok')
                        time.sleep(2)
                    if args.ptp_client:
                        keypress('down')
                        keypress('down')
                        keypress('ok')
                        keypress('up')
                    else:
                        keypress('down')
                    keypress('ok')
                    if view() >> 16 != 690:
                        raise RuntimeError('Back did not return to Setup')
                    # Native MENU closure is focus-dependent: a press can
                    # return to the tab/root before a later press leaves menus.
                    menu_exit_presses = 0
                    for _ in range(3):
                        keypress('menu')
                        menu_exit_presses += 1
                        view()  # Retained menu identity is not visibility.
                        qmp('screendump', {'filename': str(out / 'returned.ppm')})
                        scanout_matches = scanout_fraction(out / 'before-menu.ppm', out / 'returned.ppm')
                        if args.expect=='global' or args.panic_test:
                            native_view=read64(0xa03393110)
                            if 0xa00000000<=native_view<0xa40000000-0x110 and read64(native_view+0x54)&0xffffffff==3 and read64(native_view+0xd0)&0xffffffff==1:
                                break
                        elif scanout_matches >= .90:
                            break
                    menu_matches = scanout_fraction(out / 'before-menu.ppm', out / 'fenestra.ppm')
                    if (scanout_matches < .90 and args.expect!='global' and not args.panic_test) or menu_matches >= .50:
                        raise RuntimeError('live-view scanout control did not distinguish menu/return')
                    menu_completed = True
                    if args.expect=='global':
                        deadline=time.monotonic()+120
                        while time.monotonic()<deadline:
                            trace=(out/'calls.log').read_text()
                            if re.search(r'call pc='+hex(symbols['fx_z63_probe_module_result'])+r' x0=0x4b x1=0x0 x2=0x3\b',trace):break
                            time.sleep(.1)
                        else:raise RuntimeError('Global scene did not submit three frames')
                        qmp('screendump',{'filename':str(out/'global-overlay.ppm')})
                        image=application.read_bytes();base=0x40160000
                        if 'fx_z63_scene_layout' in symbols:
                            runtime_offset=struct.unpack_from('<I',image,symbols['fx_z63_menu_runtime_offset']-base)[0]
                            runtime_meta=struct.unpack_from('<3I',image,symbols['fx_z63_runtime_layout']-base)
                            scene_meta=struct.unpack_from('<17I',image,symbols['fx_native_scene_metadata']-base)
                            display_meta=struct.unpack_from('<12I',image,symbols['fx_z63_scene_layout']-base)
                            state=read64(0xa02912e10+80);runtime=read64(state+runtime_offset)
                            owner=read64(read64(runtime+runtime_meta[2]));provider=read64(owner+scene_meta[2])
                            for _ in range(32):
                                if not provider:raise RuntimeError('Global scene provider absent')
                                name=read64(provider+scene_meta[4])
                                value=b''.join(read64(name+i).to_bytes(8,'little') for i in range(0,24,8))
                                if value.split(b'\0',1)[0]==b'camera.display.scenes':break
                                provider=read64(provider+scene_meta[3])
                            else:raise RuntimeError('Global scene provider chain differs')
                            budget=read64(provider+scene_meta[5]);scene=read64(budget+scene_meta[6]);display=scene+display_meta[2]
                            diag=dict(scene=hex(scene),surface=hex(read64(display+display_meta[4])),
                                width=read64(display+display_meta[5])&0xffffffff,height=read64(display+display_meta[6])&0xffffffff,
                                callbacks=read64(display+display_meta[7])&0xffffffff,
                                target=[read64(display+display_meta[8]+i)&0xffffffff for i in (0,4,8)],
                                viewport=list(struct.unpack('<4H',read64(display+display_meta[9]).to_bytes(8,'little'))),
                                frames=read64(display+display_meta[10])&0xffffffff,error=read64(display+display_meta[11])&0xffffffff)
                            (out/'global-native-state.json').write_text(json.dumps(diag,indent=2)+'\n')
                            (out/'global-display-registers.txt').write_text(qmp('human-monitor-command',{'command-line':'xp /192wx 0x15486400'}))
                            (out/'global-graphics-target.txt').write_text(qmp('human-monitor-command',{'command-line':'xp /66gx 0xa0306b5c0'}))
                        pixels=(out/'global-overlay.ppm').read_bytes()
                        header=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',pixels)
                        if not header:raise RuntimeError('Global screenshot format differs')
                        width,height=map(int,header.groups());pixels=pixels[header.end():]
                        green=total=0
                        for y in range(height*25//480,height*55//480,4):
                            for x in range(width*30//720,width*160//720,4):
                                at=(y*width+x)*3;red,g,b=pixels[at:at+3]
                                # The native YUV scanout round trip produces
                                # (0,255,2) for this (0,255,0) RGB control.
                                green+=red<=3 and g>=252 and b<=3;total+=1
                        if not total or green/total<.70:raise RuntimeError('Global frames were accepted but the green control was not visible')
                        deadline=time.monotonic()+120
                        while time.monotonic()<deadline:
                            trace=(out/'calls.log').read_text()
                            if re.search(r'call pc='+hex(symbols['fx_z63_probe_module_result'])+r' x0=0x3f x1=0x0 x2=0x1\b',trace):break
                            time.sleep(1)
                        else:raise RuntimeError('Global scene did not finish and retire its bindings')
                        qmp('screendump',{'filename':str(out/'global-closed.ppm')})
                        scanout_matches=scanout_fraction(out/'before-menu.ppm',out/'global-closed.ppm')
                        if scanout_matches<.90:raise RuntimeError('Global close did not restore native live view')
                for index, key in enumerate(filter(None, args.keys.split(','))):
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': key})
                    time.sleep(.5)
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': ''})
                    time.sleep(1.5)
                    qmp('screendump', {'filename': str(out / ('key-%02d.ppm' % index))})
                if args.ptp_client:
                    spec=importlib.util.spec_from_file_location('external_z63_ptp',args.ptp_client.resolve(strict=True))
                    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
                    qmp('qom-set', {'path':'/machine','property':'usb-cable','value':True})
                    time.sleep(5)
                    (out/'usb-native-state.txt').write_text('\n'.join(qmp('human-monitor-command', {'command-line':line}) for line in [
                        'xp /128bx 0xa0305c590','xp /128bx 0xa0305c610','xp /32gx 0xa0305c680',
                        'xp /16gx 0xa0305c5c0','xp /8gx 0xa0305c9a8','xp /1gx 0xa02dc8228','xp /1wx 0x0d151038']))
                    event=read64(0xa04d264c0+523*8)
                    if 0xa00000000<=event<0xa40000000-32:
                        (out/'usb-native-state.txt').open('a').write('\nNative event 524:\n'+qmp('human-monitor-command',{'command-line':'xp /4gx '+hex(event)}))
                    native=module.Ptp(str(Path(temp)/'usb'))
                    try:
                        code,_,info=native.transaction(0x1001)
                        if code!=0x2001:raise RuntimeError('native DeviceInfo failed')
                        description=module.device_info(info)
                        (out/'device-info.json').write_text(json.dumps(description,indent=2)+'\n')
                        native.open()
                        class Transport:
                            def read(self,op,params,maximum):
                                code,_,data=native.transaction(op,params)
                                if code!=0x2001:raise PtpError(code)
                                if len(data)>maximum:raise RuntimeError('native PTP length')
                                return data
                            def write(self,op,params,data):
                                code,_,_=native.transaction(op,params,data=data)
                                if code!=0x2001:raise PtpError(code)
                        from ptp_client import Client, PtpError
                        client=Client(Transport());hello=client.hello()
                        echo=bytes(range(256))+b'\0\xffFenestra C2.01'
                        if client.query(2,echo)!=echo:raise RuntimeError('PTP binary echo differs')
                        status_payload=client.query(1)
                        if int.from_bytes(status_payload[:4],'little')!=201:raise RuntimeError('PTP firmware status differs')
                        rtos_result=None
                        if args.rtos_test:
                            monitor=client.query(7,struct.pack('<I',0))
                            if len(monitor)!=96 or monitor[:4]!=b'FXT1' or struct.unpack_from('<Q',monitor,16)[0]!=4:
                                raise RuntimeError('RTOS context monitor differs')
                            def native_job(address,arguments):
                                payload=struct.pack('<IIIQ8Q',5,1,1000,address,*arguments)
                                reply=client.query(7,payload)
                                if len(reply)!=4:raise RuntimeError('RTOS native submission differs')
                                job=struct.unpack('<I',reply)[0];end=time.monotonic()+5
                                while True:
                                    state=client.query(7,struct.pack('<I',4))
                                    if len(state)!=64 or state[:4]!=b'FXS1':raise RuntimeError('RTOS worker state differs')
                                    phase=struct.unpack_from('<I',state,12)[0]
                                    if phase==3:break
                                    if phase not in (1,2) or time.monotonic()>=end:raise RuntimeError('RTOS native job failed to complete')
                                    time.sleep(.025)
                                result=struct.unpack_from('<Q',state,24)[0];count=struct.unpack_from('<I',state,32)[0]
                                output=client.query(7,struct.pack('<4I',6,job,0,count)) if count else b''
                                client.query(7,struct.pack('<II',7,job))
                                return result,output,struct.unpack_from('<I',state,20)[0]
                            value,output,task=native_job(symbols['fx_native_context'],[2]+[0]*7)
                            if value!=4 or output or not task:raise RuntimeError('Queued RTOS native context call differs')
                            text=b'Nikon Corporation'
                            offset=application.read_bytes().find(text+b'\0')
                            if offset<0:raise RuntimeError('Native printf string unavailable')
                            value,output,print_task=native_job(0x40c05344,[0x40160000+offset]+[0]*7)
                            if value or output!=text or print_task!=task:raise RuntimeError('Native dedicated-task printf capture differs')
                            def native_read(address,count):
                                data=client.query(7,struct.pack('<IQI',2,address,count))
                                if len(data)!=count:raise RuntimeError('Native monitor read length differs')
                                return data
                            arena=native_read(0xa02fc7b90,24)
                            callback=struct.unpack('<Q',native_read(0xa02fc7530,8))[0]
                            queue=struct.unpack('<Q',native_read(0xa02fc5230,8))[0]
                            block=struct.unpack('<Q',native_read(queue+8,8))[0] if 0xa00000000<=queue<0xa40000000-16 else 0
                            linux_layout=dict(ready=arena[0],pool=hex(struct.unpack_from('<Q',arena,8)[0]),
                                pool_bytes=struct.unpack_from('<I',arena,16)[0],callback=hex(callback),queue=hex(queue),block=hex(block))
                            rtos_result=dict(current_el=4,native_task=task,printf_bytes=len(output),printf_exact=True,linux_layout=linux_layout,passed=True)
                            if args.sync_test or args.irq_test:
                                profile=json.loads((Path(__file__).resolve().parents[1]/'targets/z63-c201.json').read_text())
                                bindings={row['name']:row['address'] for row in profile['bindings']}
                                def invoke(name,*arguments):
                                    value,output,called_task=native_job(bindings[name],list(arguments)+[0]*(8-len(arguments)))
                                    if output or called_task!=task:raise RuntimeError('Native sync worker identity differs')
                                    return value&0xffffffff
                                scratch,_,_=native_job(bindings['rtos.memory.allocate'],[32]+[0]*7)
                                if not 0xa00000000<=scratch<0xa40000000-32:raise RuntimeError('Native sync scratch allocation failed')
                                def configuration(data):
                                    response=client.query(7,struct.pack('<IQI',3,scratch,len(data))+data)
                                    if response!=struct.pack('<I',len(data)):raise RuntimeError('Native sync config copy differs')
                                if args.irq_test:
                                    limit=invoke('rtos.irq.limit');levels=invoke('rtos.irq.priority-levels')
                                    if not 32<=limit<=1023 or not 1<=levels<=256:raise RuntimeError('Native IRQ limits differ')
                                    observed={}
                                    for name,line in [('status',30),('priority.get',30),('trigger.get',30),('affinity.get',32)]:
                                        configuration(bytes(8))
                                        if invoke('rtos.irq.'+name,line,scratch):raise RuntimeError('Native IRQ '+name+' failed')
                                        value=struct.unpack('<I',native_read(scratch,4))[0]
                                        observed[name]=value
                                    if observed['status']&~7 or observed['priority.get']>=levels or observed['trigger.get'] not in (0,2):
                                        raise RuntimeError('Native IRQ status fields differ')
                                    if invoke('rtos.irq.status',limit+1,scratch)!=0xffffffef:
                                        raise RuntimeError('Native IRQ invalid-line rejection differs')
                                    rtos_result['irq']=dict(limit=limit,priority_levels=levels,observed=observed,
                                        invalid_line_rejected=True,line_modified=False,passed=True)
                            if args.sync_test:
                                configuration(struct.pack('<2I',0,0))
                                mutex=invoke('rtos.mutex.create',scratch)
                                if not 0<mutex<0x80000000:raise RuntimeError('Native mutex create failed')
                                for operation,arguments in [('lock',()),('unlock',()),('try-lock',()),('unlock',()),('lock-timeout',(0,)),('unlock',()),('delete',())]:
                                    if invoke('rtos.mutex.'+operation,mutex,*arguments):raise RuntimeError('Native mutex '+operation+' failed')
                                configuration(struct.pack('<3I',0,1,2))
                                semaphore=invoke('rtos.semaphore.create',scratch)
                                if not 0<semaphore<0x80000000:raise RuntimeError('Native semaphore create failed')
                                for operation,expected in [('poll',0),('poll',0xffffffce),('signal',0),('wait',0),('wait-timeout',0xffffffce),('delete',0)]:
                                    if invoke('rtos.semaphore.'+operation,semaphore,0)!=expected:raise RuntimeError('Native semaphore '+operation+' differs')
                                rtos_result['synchronization']=dict(mutex_operations=8,semaphore_operations=7,
                                    empty_poll_and_timeout=True,scratch_released=True,passed=True)
                            if args.sync_test or args.irq_test:
                                value,output,_=native_job(bindings['rtos.memory.release'],[scratch]+[0]*7)
                                if output:raise RuntimeError('Native scratch release emitted output')
                        linux_result=None
                        if args.linux_test:
                            from ptp_linux import LinuxBridge
                            bridge=LinuxBridge(client)
                            bridge.call(1,struct.pack('<Q',bridge.nonce))
                            before=bridge.status()
                            if before['nonce']!=bridge.nonce or not before['hooked'] or not before['address']:
                                raise RuntimeError('Linux bridge allocation or callback differs')
                            if args.linux_agent:
                                linux_result=exercise_linux_agent(bridge,args.linux_agent,out)
                            else:
                                output=bridge.command("printf 'Fenestra-C201-IPC-control'",timeout=15)
                                if b'Fenestra-C201-IPC-control' not in output:
                                    raise RuntimeError('Linux shell output differs')
                                after=bridge.status()
                                if after['running'] or after['completions']<=before['completions'] or after['dropped']:
                                    raise RuntimeError('Linux completion accounting differs')
                                linux_result=dict(original_shell=True,correlated_completion=True,
                                    output_bytes=len(output),agent_tested=False,passed=True)
                        ptp_result=dict(hello=hello,binary_echo_bytes=len(echo),device_info=description,rtos=rtos_result,linux=linux_result,passed=True)
                    finally:
                        native.close();native.u.s.close()
                time.sleep(2)
                status = qmp('query-status')
                if args.panic_test:
                    image=application.read_bytes();base=0x40160000
                    layout=struct.unpack_from('<9I',image,symbols['fx_z63_panic_layout']-base)
                    if layout[:2]!=(0x46585036,1):raise RuntimeError('Panic observation layout differs')
                    offset=struct.unpack_from('<I',image,symbols['fx_z63_menu_runtime_offset']-base)[0]
                    runtime=read64(read64(0xa02912e10+80)+offset);panic=read64(runtime+layout[2])
                    values=[read64(panic+at)&0xffffffff for at in layout[3:8]]
                    if values[:3]!=[1,1,0] or not values[3] or values[4]!=1:
                        raise RuntimeError('Native panic presentation differs: '+repr(values))
                    qmp('screendump',{'filename':str(out/'panic.ppm')})
                    pixel_control=panic_pixel_control(out/'panic.ppm')
                    if not pixel_control['passed']:raise RuntimeError('Panic rendered without visible error-screen pixels')
                    panic_result=dict(captured=True,shown=True,frames=values[3],cpu_faults=values[4],
                        esr=hex(read64(panic+layout[8])),pixel_control=pixel_control,passed=True)
                    if args.panic_peer_test:
                        runtime_meta=struct.unpack_from('<5I',image,symbols['fx_z63_runtime_layout']-base)
                        worker_meta=struct.unpack_from('<3I',image,symbols['fx_z63_worker_layout']-base)
                        if runtime_meta[:2]!=(0x46585236,1) or worker_meta[:2]!=(0x46585736,1):
                            raise RuntimeError('Peer observation layout differs')
                        if read64(runtime+runtime_meta[4])&0xffffffff!=2:raise RuntimeError('Panic cohort count differs')
                        workers=read64(runtime+runtime_meta[3])
                        phases=[read64(read64(workers+8*i)+worker_meta[2])&0xffffffff for i in range(2)]
                        if sorted(phases)!=[4,5]:raise RuntimeError('Panic peer failed to quiesce: '+repr(phases))
                        panic_result['peer_quiesced']=True
                if args.startup_test:
                    startup_result['running']=boot_wait(lambda s:s['phase']==5 and not s['error'])
                    if args.shutdown_test:
                        qmp('qom-set',{'path':'/machine','property':'mcu-keys','value':'power=off'})
                        startup_result['stopped']=boot_wait(lambda s:s['phase']==7 and s['shutdown_done']==1 and not s['error'])
                        startup_result['power_switch_tested']=True
                    startup_result['passed']=True
                elif args.shutdown_test:raise ValueError('--shutdown-test requires --startup-test')
                qmp('screendump', {'filename': str(out / 'final.ppm')})
                qmp('quit')
                process.wait(timeout=10)
            except BaseException as error:
                (out / 'failure.json').write_text(json.dumps(dict(error=repr(error),
                    candidate_sha256=receipt['candidate_sha256'],
                    qualification='Failed or interrupted test; no runtime acceptance.'), indent=2) + '\n')
                raise
            finally:
                if connection is not None:
                    connection.close()
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    records = []
    for line in (out / 'calls.log').read_text().splitlines():
        match = re.search(r'call pc=(0x[0-9a-f]+) x0=(0x[0-9a-f]+) x1=(0x[0-9a-f]+) x2=(0x[0-9a-f]+)', line)
        if match:
            records.append(tuple(int(value, 16) for value in match.groups()))
    marker = symbols['fx_z63_probe_module_result']
    stages = [(stage, result, detail) for pc, stage, result, detail in records if pc == marker]
    if args.expect == 'success':
        required = [(10, 0, 0), (20, 0, 1), (21, 0, 2), (22, 0, 5), (30, 0, 0)]
        observed_stages = [value for value in stages if value[0] in (10, 20, 21, 22, 30)]
        passed = observed_stages == required and not any(stage == 40 for stage, _, _ in stages)
    elif args.expect == 'ui':
        requests=[(result,detail) for stage,result,detail in stages if stage==51]
        if args.demo_test:
            passed=((60,0,1) in stages and (61,0,0) in stages and demo_result is not None and
                len(requests)>=10 and all(r in (0,2,4,12) for r,_ in requests) and
                not any(stage in (40,52) for stage,_,_ in stages))
        else:passed=((60,0,1) in stages and (61,0,0) in stages and args.ui_test and
                0x71000002 in views and len(requests)>4 and requests[-1]==(0,24) and
                all(r in (0,12) for r,_ in requests) and not any(stage==40 for stage,_,_ in stages))
    elif args.expect == 'timeout':
        passed=((60,0,1) in stages and (61,0,0) in stages and (10,0,0) in stages and
                (40,7,0) in stages and (63,1,0) in stages and not any(stage==51 for stage,_,_ in stages))
    elif args.expect == 'files':
        requests=[(result,detail) for stage,result,detail in stages if stage==(51 if receipt.get('runtime') else 50)]
        lifecycle=((60,0,1) in stages and (61,0,0) in stages and (63,0,1) in stages) if receipt.get('runtime') else ((10,0,0) in stages and (30,0,0) in stages)
        passed=(lifecycle and len(requests)==12 and
                [r for r,_ in requests]==[0]*9+[8]+[0]*2 and requests[-1][1]==24 and
                not any(stage==40 for stage,_,_ in stages))
    elif args.expect == 'global':
        frames=[detail for stage,result,detail in stages if stage==75 and result==0]
        requests=[(result,detail) for stage,result,detail in stages if stage==51]
        passed=((60,0,1) in stages and (61,0,0) in stages and (63,0,1) in stages and
                frames==[1,2,3] and requests[-1]==(0,24) and
                all(result in (0,2) for result,_ in requests) and
                not any(stage in (40,52) for stage,_,_ in stages))
    elif args.expect == 'mapping':
        requests=[(result,detail) for stage,result,detail in stages if stage==51]
        passed=((60,0,1) in stages and (61,0,0) in stages and (63,0,1) in stages and
                requests==[(0,24)]+[(0,22)]*4+[(0,24)] and
                not any(stage in (40,52) for stage,_,_ in stages))
    elif args.expect == 'runtime':
        requests = [(result, detail) for stage, result, detail in stages if stage == 51]
        count=args.module_count
        starts=sorted(detail for stage,result,detail in stages if stage==61 and result==0)
        passed = ((60, 0, count) in stages and starts==list(range(count)) and (63, 0, count) in stages and
                  len([1 for stage,result,_ in stages if stage==10 and result==0])==count and
                  len(requests) >= 10*count and all(result == 0 for result, _ in requests) and
                  requests[-1][1] == 24 and not any(stage == 40 for stage, _, _ in stages))
    elif args.expect == 'services':
        requests=[(result,detail) for stage,result,detail in stages if stage==(51 if receipt.get('runtime') else 50)]
        lifecycle=((60,0,1) in stages and (61,0,0) in stages and (63,0,1) in stages) if receipt.get('runtime') else ((10,0,0) in stages and (30,0,0) in stages)
        passed=(lifecycle and len(requests)==21 and
                [r for r,_ in requests]==[0]*6+[12]+[0]*14 and requests[-1][1]==24 and
                not any(stage==40 for stage,_,_ in stages))
    elif args.expect == 'sdk':
        requests = [(result, detail) for stage, result, detail in stages if stage == 50]
        passed = ((10, 0, 0) in stages and (30, 0, 0) in stages and
                  len(requests) >= 10 and all(result == 0 for result, _ in requests) and
                  requests[-1][1] == 24 and not any(stage == 40 for stage, _, _ in stages))
    elif args.expect == 'fault':
        passed = (10, 0, 0) in stages and (40, 10, 1 << 32) in stages
    else:
        loads = [result for stage, result, _ in stages if stage == 10]
        passed = len(loads) == 1 and loads[0] != 0 and not any(20 <= stage <= 41 for stage, _, _ in stages)
    if receipt.get('drawing'):
        frames = [result for stage,result,_ in stages if stage == 81]
        passed = passed and len(frames) == 3 and frames == [0,0,0] and (82,0,3) in stages
    passed = passed and status.get('running') is True
    if args.native_menu_test:
        passed = passed and menu_completed
    if receipt.get('menu'):
        activations = sum(pc == symbols['activate'] for pc, *_ in records)
        passed = passed and activations >= (2 if args.repeat_activation else 1)
        if args.repeat_activation:
            starts = [(result, detail) for stage, result, detail in stages if stage == 4]
            passed = passed and len(starts) == 1 and starts[0][0] == 0 and starts[0][1] > 0
    if args.tasks_test:
        passed=passed and task_result is not None and any(stage==54 and result==0 and detail==int(task_result['original_ui_task'],16) for stage,result,detail in stages)
    result = dict(demo=demo_result,panic=panic_result,startup=startup_result,manager=manager_result,reader=reader_result,tasks=task_result,passed=passed, target='z63-c201', expected=args.expect, status=status,
        stages=stages, seconds=time.monotonic() - start, candidate_sha256=receipt['candidate_sha256'],
        qemu_sha256=hashlib.sha256(args.qemu.read_bytes()).hexdigest(),
        plugin_sha256=hashlib.sha256(args.plugin.read_bytes()).hexdigest(),
        visual_review_required=True, physical_camera_tested=False, native_menu_completed=menu_completed,
        repeat_activation_tested=args.repeat_activation,ptp=ptp_result,
        native_view_identities=views, live_view_match_fraction=scanout_matches,
        menu_match_fraction=menu_matches, menu_exit_presses=menu_exit_presses, command=command)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('passed', 'expected', 'status', 'stages')}, indent=2))
    if not passed:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
