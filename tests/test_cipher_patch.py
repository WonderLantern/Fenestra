"""Synthetic fixtures only: no camera, firmware, device key or network required."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'host'))
from cipher_patch import apply_patch, make_patch, atomic_new
from repack import zr_transform, zr_tail
import zr_plain_container

class CipherPatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.original = bytes(range(256))*100
        result = bytearray(self.original)
        result[10:42] = b'X'*32
        result[12000:12010] = b'Y'*10
        self.result = bytes(result)
        self.stock, self.patch, self.output = [self.root/n for n in ('stock.bin', 'local.fxpatch', 'output.bin')]
        self.stock.write_bytes(self.original)
        make_patch(self.original, self.result, 'synthetic', self.patch)

    def rewrite(self, change):
        with zipfile.ZipFile(self.patch) as archive:
            content = {n: archive.read(n) for n in archive.namelist()}
        manifest = json.loads(content['manifest.json'])
        change(manifest, content)
        content['manifest.json'] = json.dumps(manifest).encode()
        other = self.root/'modified.fxpatch'
        with zipfile.ZipFile(other, 'w') as archive:
            for name, data in content.items():
                archive.writestr(name, data)
        return other

    def rejected(self, patch=None):
        with self.assertRaises((ValueError, KeyError, zipfile.BadZipFile)):
            apply_patch(self.stock, patch or self.patch, self.output)
        self.assertFalse(self.output.exists())

    def test_exact_roundtrip_and_readonly_original(self):
        apply_patch(self.stock, self.patch, self.output)
        self.assertEqual(self.output.read_bytes(), self.result)
        self.assertEqual(self.stock.read_bytes(), self.original)

    def test_wrong_base_same_length(self):
        self.stock.write_bytes(b'!' + self.original[1:])
        self.rejected()

    def test_truncated_base(self):
        self.stock.write_bytes(self.original[:-1])
        self.rejected()

    def test_modified_payload(self):
        def alter(m, c):
            n = m['ranges'][0]['member']; c[n] = b'!' + c[n][1:]
        self.rejected(self.rewrite(alter))

    def test_overlap(self):
        self.rejected(self.rewrite(lambda m,c: m['ranges'][1].update(offset=11)))

    def test_out_of_bounds(self):
        self.rejected(self.rewrite(lambda m,c: m['ranges'][0].update(offset=len(self.original))))

    def test_negative_offset(self):
        self.rejected(self.rewrite(lambda m,c: m['ranges'][0].update(offset=-1)))

    def test_bool_is_not_integer_offset(self):
        self.rejected(self.rewrite(lambda m,c: m['ranges'][0].update(offset=True)))

    def test_path_traversal(self):
        self.rejected(self.rewrite(lambda m,c: m['ranges'][0].update(member='../escape.bin')))
        self.assertFalse((self.root.parent/'escape.bin').exists())

    def test_unexpected_archive_member(self):
        self.rejected(self.rewrite(lambda m,c: c.update({'secret.bin': b'not allowed'})))

    def test_wrong_final_digest(self):
        self.rejected(self.rewrite(lambda m,c: m.update(result_sha256='0'*64)))

    def test_original_range_digest(self):
        self.rejected(self.rewrite(lambda m,c: m['ranges'][0].update(before_sha256='0'*64)))

    def test_preserve_existing_output(self):
        self.output.write_bytes(b'existing')
        with self.assertRaises(FileExistsError):
            apply_patch(self.stock, self.patch, self.output)
        self.assertEqual(self.output.read_bytes(), b'existing')

    def test_input_cannot_be_output(self):
        with self.assertRaises(FileExistsError):
            apply_patch(self.stock, self.patch, self.stock)
        self.assertEqual(self.stock.read_bytes(), self.original)

    def test_deterministic_patch(self):
        other = self.root/'second.fxpatch'
        make_patch(self.original, self.result, 'synthetic', other)
        self.assertEqual(self.patch.read_bytes(), other.read_bytes())

    def test_empty_patch_rejected(self):
        with self.assertRaises(ValueError):
            make_patch(self.original, self.original, 'synthetic', self.root/'empty.fxpatch')

    def test_truncated_archive(self):
        self.patch.write_bytes(self.patch.read_bytes()[:100])
        self.rejected()

    def test_zr_aes_counter_position(self):
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        material = bytes(range(48)) # public synthetic test key; not device material
        body = bytes(range(256))
        stream = Cipher(algorithms.AES(material[:32]), modes.CTR(material[32:])).encryptor()
        expected = body[:32] + (stream.update(body) + stream.finalize())[32:]
        self.assertEqual(zr_transform(body, material), expected)
        self.assertEqual(zr_transform(expected, material), body)

    def test_zr_container_crc_repair(self):
        original = zr_plain_container.serialize([('sample.bin', b'abcd'), ('other.bin', b'unchanged')])
        row = zr_plain_container.inspect(original)['components'][0]
        modified = bytearray(original); modified[row['offset']] ^= 1
        repaired, _ = zr_plain_container.repair_crcs(modified)
        rows = zr_plain_container.inspect(repaired)['components']
        self.assertEqual(rows[1], zr_plain_container.inspect(original)['components'][1])

    def test_tail_length_and_sha1_seed(self):
        self.assertEqual(len(zr_tail(bytes(20))), 12)
        self.assertEqual(zr_tail(bytes(20)), zr_tail(bytes(1)+b'X'*19))
        self.assertNotEqual(zr_tail(bytes(20)), zr_tail(b'\1'+bytes(19)))

if __name__ == '__main__':
    unittest.main()
