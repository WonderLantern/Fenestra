"""Synthetic Zf porting checks; no firmware, camera or private key required."""
import binascii
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import zf_c301
import repack
import zf_c301_repack
from repack import build_core


def fixture():
    names = ['eg2110_030100.bi', 'ex.bi', 'li.bi', 'vr.bi', 'tpj.bi']
    body = bytearray(b' ' * 32 + struct.pack('>4I', 5, 208, 0, 0) + bytes(160))
    components = []
    for i, name in enumerate(names):
        payload = bytes([i + 1]) * (i + 3)
        component = payload + binascii.crc_hqx(payload, 0).to_bytes(2, 'big')
        struct.pack_into('>16s4I', body, 48 + i * 32, name.encode(), len(body), len(component), 0, 0)
        body.extend(component)
        components.append(component)
    body.extend(binascii.crc_hqx(body, 0).to_bytes(2, 'big') + bytes(14))
    return bytes(body), components


class ZfPortTests(unittest.TestCase):
    def test_contiguous_directory_and_corrupt_inputs(self):
        body, components = fixture()
        rows = zf_c301.inspect_body(body)
        self.assertEqual([row['name'] for row in rows], ['eg2110_030100.bi', 'ex.bi', 'li.bi', 'vr.bi', 'tpj.bi'])
        self.assertEqual(rows[1]['plaintext_body_offset'], 208 + len(components[0]))
        self.assertEqual(rows[1]['encrypted_package_offset'], 0x260 + rows[1]['plaintext_body_offset'])
        self.assertNotEqual(rows[1]['plaintext_body_offset'] % 16, 0)
        for offset in (208, len(body) - 1, 48 + 32 + 19):
            bad = bytearray(body)
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                zf_c301.inspect_body(bad)
        with self.assertRaises(ValueError):
            zf_c301.inspect_body(body[:-1])

    def test_identity_and_optional_digest(self):
        body, components = fixture()
        key = b'12345678'  # synthetic suffix
        raw = bytearray(0x260 + len(body))
        raw[0x240:0x254] = hashlib.sha1(body + key).digest()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, body_file, app, key_file = (root / n for n in ('stock', 'body', 'app', 'key'))
            package.write_bytes(raw)
            body_file.write_bytes(body)
            app.write_bytes(components[0])
            key_file.write_bytes(key)
            constants = dict(PACKAGE_SIZE=len(raw), PACKAGE_SHA256=hashlib.sha256(raw).hexdigest(),
                             BODY_SIZE=len(body), BODY_SHA256=hashlib.sha256(body).hexdigest(),
                             EG_SIZE=len(components[0]), EG_SHA256=hashlib.sha256(components[0]).hexdigest(),
                             EG_PAYLOAD_SHA256=hashlib.sha256(components[0][:-2]).hexdigest(),
                             Z9_CODE_START=0, Z9_CODE_END=2)
            with patch.multiple(zf_c301, **constants), patch.object(zf_c301, '_inspect_sysctl_events', return_value={}), \
                    patch.object(zf_c301, '_inspect_dispatch_candidate', return_value={}), \
                    patch.object(zf_c301, '_inspect_native_pages', return_value={}):
                result = zf_c301.inspect_inputs(package, body_file, app)
                self.assertIsNone(result['original_header_sha1_verified'])
                self.assertFalse(result['runtime_build_available'])
                self.assertFalse(result['z9_code_region_in_zf']['all_zero'])
                self.assertFalse(result['z9_code_region_in_zf']['loaded_or_executable_verified'])
                self.assertTrue(zf_c301.inspect_inputs(package, body_file, app, key_file)['original_header_sha1_verified'])
                key_file.write_bytes(b'87654321')
                with self.assertRaises(ValueError):
                    zf_c301.inspect_inputs(package, body_file, app, key_file)
                body_file.write_bytes(body[:-1] + bytes([body[-1] ^ 1]))
                with self.assertRaises(ValueError):
                    zf_c301.inspect_inputs(package, body_file)

    def test_header_digest_is_from_the_hashed_stream(self):
        original = bytes(range(256)) * 4
        class ChangingFile(io.BytesIO):
            changed = False
            def fileno(self):
                return 1
            def read(self, size=-1):
                result = super().read(size)
                if not self.changed:
                    self.changed = True
                    pos = self.tell()
                    self.seek(0x240)
                    self.write(b'X' * 20)
                    self.seek(pos)
                return result
        with patch.object(Path, 'open', return_value=ChangingFile(original)), \
                patch.object(zf_c301.os, 'fstat', return_value=SimpleNamespace(st_size=len(original))):
            checked = zf_c301._checked_file(Path('unused'), len(original), hashlib.sha256(original).hexdigest())
        self.assertEqual(checked, original[0x240:0x254])

    def test_hook_comparison_counts_aligned_and_unaligned_matches(self):
        z9 = bytes(range(64))
        zf = b'Z' * 4 + z9 + b'X' * 5 + z9
        row = zf_c301._compare_hooks(z9, zf, {'candidate': zf_c301.BASE})[0]
        self.assertEqual([w['matches'] for w in row['windows']], [2, 2, 2])
        self.assertEqual([w['aligned_matches'] for w in row['windows']], [1, 1, 1])
        self.assertEqual(row['windows'][2]['aligned_examples'], [hex(zf_c301.BASE + 4)])
        with self.assertRaises(ValueError):
            zf_c301._compare_hooks(z9, zf, {'outside': zf_c301.BASE + 4})

    def test_sysctl_event_ids_and_names_are_both_checked(self):
        image = bytearray(26 * 88 + 512)
        position = 26 * 88
        for number, suffix in zf_c301.SYSCTL_EVENTS.items():
            name = ('eSyscCommandId_' + suffix).encode('ascii') + bytes(1)
            struct.pack_into('<I4xQ', image, number * 88, number, zf_c301.BASE + position)
            image[position:position + len(name)] = name
            position += len(name)
        found = zf_c301._inspect_sysctl_events(image, zf_c301.BASE)
        self.assertEqual([row['id'] for row in found['events']], [7, 8, 19, 20, 24, 25])
        self.assertFalse(found['dispatch_hook_verified'])
        image[20 * 88] = 19
        with self.assertRaises(ValueError):
            zf_c301._inspect_sysctl_events(image, zf_c301.BASE)

    def test_dispatch_window_is_bounded(self):
        image = bytes(range(256)) + bytes(12)
        row = zf_c301._inspect_dispatch_candidate(image, zf_c301.BASE)
        self.assertEqual(row['span_sha256'], hashlib.sha256(image).hexdigest())
        self.assertFalse(row['complete_function_and_hook_verified'])
        with self.assertRaises(ValueError):
            zf_c301._inspect_dispatch_candidate(image, zf_c301.BASE + 4)

    def test_zf_native_page_namespace_requires_all_expected_records(self):
        stop = zf_c301.NATIVE_PAGES['stop']
        kinds = [2] * 1433 + [0] * 3 + [3] * 128 + [5] * 169
        image = bytearray(len(kinds) * 96)
        for page, kind in enumerate(kinds, 1):
            struct.pack_into('<I', image, (page - 1) * 96, (page << 16 | kind) if kind else 0)
        with patch.object(zf_c301, 'NATIVE_PAGES', dict(first=0, stop=stop, kinds=zf_c301.NATIVE_PAGES['kinds'])):
            result = zf_c301._inspect_native_pages(image)
            self.assertEqual(result['native_slots'], 1733)
            self.assertTrue(result['scanned_array_does_not_overlap_extension_ids'])
            struct.pack_into('<I', image, 0, 0x20002)
            with self.assertRaises(ValueError):
                zf_c301._inspect_native_pages(image)

    def test_unknown_core_target_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unsupported platform'):
            build_core('zf-c301', 'unused', 'unused')
        with self.assertRaisesRegex(ValueError, 'unsupported platform'):
            repack.decode_original('zf-c301', b'', b'')
        with self.assertRaisesRegex(ValueError, 'unsupported platform'):
            repack.rebuild('zf-c301', b'', b'', b'', b'', b'', None, {})
        with self.assertRaisesRegex(ValueError, 'unsupported platform'):
            repack.produce('zf-c301', 'unused', 'unused', None, 'unused', 'unused')

    def test_existing_core_targets_keep_their_builders(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(repack, 'ROOT', Path(tmp).resolve()), \
                patch.object(repack.subprocess, 'run') as run:
            for target, script in (('zr-c110', 'build_c110_menu.py'), ('z9-c532', 'build_z9_core.py')):
                run.reset_mock()
                build_core(target, 'original', Path(tmp) / 'build' / target)
                self.assertEqual(Path(run.call_args_list[0].args[0][2]).name, script)
                self.assertEqual(Path(run.call_args_list[1].args[0][2]).name, 'verify_platform_build.py')

    def test_zf_repack_synthetic_material_and_receipt(self):
        body, components = fixture()
        key = b'12345678'
        stream = bytes((i * 13 + 7) & 255 for i in range(len(body)))
        raw = bytearray(0x260 + len(body))
        raw[0x240:0x254] = hashlib.sha1(body + key).digest()
        raw[0x254:0x260] = bytes(12)
        raw[0x260:] = bytes(a ^ b for a, b in zip(body, stream))
        changed_eg = bytes([0x55]) * (len(components[0]) - 2)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package = root / 'package.bin'
            body_file = root / 'body.bin'
            eg_file = root / 'eg.bin'
            key_file = root / 'k8.bin'
            stream_file = root / 'stream.bin'
            output = root / 'out'
            package.write_bytes(raw)
            body_file.write_bytes(body)
            eg_file.write_bytes(changed_eg)
            key_file.write_bytes(key)
            stream_file.write_bytes(stream)
            constants = dict(PACKAGE_SIZE=len(raw), PACKAGE_SHA256=hashlib.sha256(raw).hexdigest(),
                             BODY_SIZE=len(body), BODY_SHA256=hashlib.sha256(body).hexdigest(),
                             EG_SIZE=len(components[0]), EG_SHA256=hashlib.sha256(components[0]).hexdigest(),
                             EG_PAYLOAD_SHA256=hashlib.sha256(components[0][:-2]).hexdigest())
            with patch.multiple(zf_c301, **constants):
                result = zf_c301_repack.repack(package, body_file, eg_file, key_file, stream_file, output)
            self.assertEqual(result['status'], 'PACKAGED_INTERNAL_INTEGRITY_ONLY')
            self.assertFalse(result['flashable'])
            candidate = (output / 'Z_f_0301.fenestra.candidate.bin').read_bytes()
            decoded = bytes(a ^ b for a, b in zip(candidate[0x260:], stream))
            self.assertEqual(decoded[:208], body[:208])
            self.assertEqual(decoded[208:208 + len(changed_eg)], changed_eg)
            self.assertEqual(binascii.crc_hqx(decoded[:-14], 0), 0)
            receipt = json.loads((output / 'repack.receipt.json').read_text())
            self.assertNotIn('path', json.dumps(receipt))
            self.assertNotIn(key.hex(), json.dumps(receipt))


if __name__ == '__main__':
    unittest.main()
