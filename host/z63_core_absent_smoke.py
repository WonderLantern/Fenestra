"""Boot a C2.01 candidate whose resident core is absent, beside the original.

Every hook and guard stays in the image; the code region and its marker are
cleared, as if the bytes had not reached the camera's memory. The guards must
then leave original startup unchanged: no call may enter the code region and
the scanout must match an original boot. Requires the same external nikon-z63
QEMU, plugin and inputs as z63_emulator_smoke.py. Emulator evidence only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

from build_z63_adapter import load_application
from z63_emulator_smoke import clone, scanout_fraction
from z63_placement import marker_address, MARKER


def boot(args, image, out, watch):
    out.mkdir()
    clone(args.nand.resolve(strict=True), out / 'nand.img')
    clone(args.card.resolve(strict=True), out / 'card.img')
    with tempfile.TemporaryDirectory(prefix='fx63-') as temp:
        sock = Path(temp) / 'qmp'
        machine = ','.join(['nikon-z63', 'loader=' + str(args.loader.resolve(strict=True)),
            'snor-user=' + str(args.snor.resolve(strict=True)), 'nand=' + str(out / 'nand.img'),
            'mcu-image=' + str(args.mcu.resolve(strict=True)), 'z9-capture=on', 'cfexpress=on',
            'z9-capture-raw=' + str(args.raw.resolve(strict=True)), 'sio-sink=9:8:7',
            'imu-mg=1000:0:0', 'scanout-dump=' + str(out / 'frame.ppm')])
        command = [str(args.qemu.resolve(strict=True)), '-machine', machine, '-cpu', 'cortex-a53',
            '-smp', '4,maxcpus=5', '-bios', str(image), '-serial', 'file:' + str(out / 'serial.log'),
            '-monitor', 'none', '-display', 'none', '-nic', 'none', '-action', 'shutdown=pause',
            '-drive', 'file=' + str(out / 'card.img') + ',if=none,id=card,format=raw',
            '-device', 'nvme,drive=card,serial=LOCALTEST,bus=dw-pcie,addr=0',
            '-qmp', 'unix:' + str(sock) + ',server=on,wait=off',
            '-plugin', str(args.plugin.resolve(strict=True)) + ',out=' + str(out / 'calls.log') +
            ',call=' + ':'.join(hex(a) for a in watch)]
        with (out / 'qemu.log').open('wb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=log)
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
                        response = json.loads(stream.readline())
                        if 'return' in response:
                            return response['return']
                        if 'error' in response:
                            raise RuntimeError(response['error'])

                qmp('qmp_capabilities')
                time.sleep(args.seconds)
                qmp('screendump', {'filename': str(out / 'screen.ppm')})
                status = qmp('query-status')
            finally:
                process.kill()
                process.wait()
    calls = [line for line in (out / 'calls.log').read_text().splitlines() if 'call pc=' in line]
    return status, calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('qemu', 'original', 'application', 'receipt', 'loader', 'snor', 'nand', 'mcu', 'card', 'raw', 'plugin', 'output-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=35)
    args = parser.parse_args()
    profile, original = load_application(args.original)
    receipt = json.loads(args.receipt.read_text())
    candidate = bytearray(args.application.read_bytes())
    if hashlib.sha256(candidate).hexdigest() != receipt['candidate_sha256']:
        raise ValueError('candidate differs from the build receipt')
    base = profile['image_base']
    begin, end = (profile['probe_code_region'][name] for name in ('begin', 'end'))
    marker = marker_address(profile)
    if candidate[marker - base:end - base] != MARKER or not receipt.get('guards'):
        raise ValueError('candidate has no guarded hooks')
    candidate[begin - base:end - base] = bytes(end - begin)
    out = args.output_dir.resolve()
    if out.exists():
        raise ValueError('new output directory required')
    out.mkdir(parents=True)
    absent = out / 'core-absent.eg.bin'
    absent.write_bytes(candidate)
    # Watch every hook destination and the dispatcher guard itself.
    entries = sorted({receipt['symbols'][name] for name in receipt['hooks'].values()})
    guard = int(receipt['guards'][hex(profile['sysctl']['dispatcher'])], 16)
    stock_status, _ = boot(args, args.original.resolve(strict=True), out / 'original', [guard])
    status, calls = boot(args, absent, out / 'absent', [guard, *entries])
    guard_calls = [line for line in calls if 'pc=' + hex(guard) in line]
    core_calls = [line for line in calls if 'pc=' + hex(guard) not in line]
    match = scanout_fraction(out / 'original' / 'screen.ppm', out / 'absent' / 'screen.ppm')
    result = dict(schema='fenestra.z63-core-absent/v1', candidate_sha256=receipt['candidate_sha256'],
        absent_sha256=hashlib.sha256(candidate).hexdigest(), seconds=args.seconds,
        original_status=stock_status, absent_status=status, guard_entries=len(guard_calls),
        core_entries=len(core_calls), scanout_match=match,
        passed=bool(stock_status.get('running') and status.get('running') and guard_calls and
                    not core_calls and match >= .90),
        emulator_only=True, physical_camera_tested=False)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
