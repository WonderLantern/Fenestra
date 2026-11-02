"""Run a disposable C2.01 full-system module smoke test with external inputs.

Requires a matching nikon-z63 QEMU and its z63log call-trace plugin. No firmware,
model, physical camera connection or update-package installation is supplied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('qemu', 'application', 'receipt', 'loader', 'snor', 'nand', 'mcu', 'card', 'raw', 'plugin', 'output-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=35)
    parser.add_argument('--keys', default='', help='comma-separated native MCU key presses after boot')
    parser.add_argument('--native-menu-test', action='store_true', help='navigate Setup/Fenestra, activate, Back and return to live view using MCU keys')
    parser.add_argument('--repeat-activation', action='store_true', help='press Run twice in the native menu and require a single worker attempt')
    parser.add_argument('--expect', choices=('success', 'fault', 'rejected'), default='success')
    args = parser.parse_args()
    if not 10 <= args.seconds <= 300:
        parser.error('seconds must be 10..300')
    if args.repeat_activation and not args.native_menu_test:
        parser.error('--repeat-activation requires --native-menu-test')
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
        command = [str(args.qemu.resolve(strict=True)), '-machine', machine, '-cpu', 'cortex-a53',
            '-smp', '4,maxcpus=5', '-bios', str(application), '-serial', 'file:' + str(out / 'serial.log'),
            '-monitor', 'none', '-display', 'none', '-nic', 'none', '-action', 'shutdown=pause',
            '-drive', 'file=' + str(out / 'card.img') + ',if=none,id=card,format=raw',
            '-device', 'nvme,drive=card,serial=LOCALTEST,bus=dw-pcie,addr=0',
            '-qmp', 'unix:' + str(sock) + ',server=on,wait=off',
            '-plugin', str(args.plugin.resolve(strict=True)) + ',out=' + str(out / 'calls.log') +
            ',call=' + ':'.join(hex(symbols[name]) for name in observed)]
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
                    save_view()
                    views.append(identity)
                    return identity

                def keypress(key):
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': key})
                    time.sleep(.5)
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': ''})
                    time.sleep(1.5)

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
                    keypress('ok')
                    time.sleep(2)
                    if args.repeat_activation:
                        keypress('ok')
                        time.sleep(2)
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
                        if scanout_matches >= .90:
                            break
                    menu_matches = scanout_fraction(out / 'before-menu.ppm', out / 'fenestra.ppm')
                    if scanout_matches < .90 or menu_matches >= .50:
                        raise RuntimeError('live-view scanout control did not distinguish menu/return')
                    menu_completed = True
                for index, key in enumerate(filter(None, args.keys.split(','))):
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': key})
                    time.sleep(.5)
                    qmp('qom-set', {'path': '/machine', 'property': 'mcu-keys', 'value': ''})
                    time.sleep(1.5)
                    qmp('screendump', {'filename': str(out / ('key-%02d.ppm' % index))})
                time.sleep(2)
                status = qmp('query-status')
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
    elif args.expect == 'fault':
        passed = (10, 0, 0) in stages and (40, 10, 1 << 32) in stages
    else:
        loads = [result for stage, result, _ in stages if stage == 10]
        passed = len(loads) == 1 and loads[0] != 0 and not any(20 <= stage <= 41 for stage, _, _ in stages)
    passed = passed and status.get('running') is True
    if args.native_menu_test:
        passed = passed and menu_completed
    if receipt.get('menu'):
        activations = sum(pc == symbols['activate'] for pc, *_ in records)
        passed = passed and activations >= (2 if args.repeat_activation else 1)
        if args.repeat_activation:
            starts = [(result, detail) for stage, result, detail in stages if stage == 4]
            passed = passed and len(starts) == 1 and starts[0][0] == 0 and starts[0][1] > 0
    result = dict(passed=passed, target='z63-c201', expected=args.expect, status=status,
        stages=stages, seconds=time.monotonic() - start, candidate_sha256=receipt['candidate_sha256'],
        qemu_sha256=hashlib.sha256(args.qemu.read_bytes()).hexdigest(),
        plugin_sha256=hashlib.sha256(args.plugin.read_bytes()).hexdigest(),
        visual_review_required=True, physical_camera_tested=False, native_menu_completed=menu_completed,
        repeat_activation_tested=args.repeat_activation,
        native_view_identities=views, live_view_match_fraction=scanout_matches,
        menu_match_fraction=menu_matches, menu_exit_presses=menu_exit_presses, command=command)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('passed', 'expected', 'status', 'stages')}, indent=2))
    if not passed:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
