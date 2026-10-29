"""Synthetic release-identity normalization; no reference firmware data."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'host'))
from cipher_patch import sha
import release_exact

class ExactReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.file=self.root/'candidate.bin'
        self.old=b'A'*32;self.fixed=b'B'*32
        self.data=b'X'*64+self.old+b'Y'*64+self.old
        self.expected=b'X'*64+self.fixed+b'Y'*64+self.fixed
        self.file.write_bytes(self.data)
        self.proof=dict(passed=True,candidate_sha256=sha(self.data),debug_identity=dict(
            identity=self.old.hex(),linked_copies=[hex(0x40160000+64),hex(0x40160000+160)]))
        self.ref=dict(runtime_identity=self.fixed.hex(),application_sha256=sha(self.expected),reference='synthetic')
        self.save()

    def save(self):
        (self.root/'verification.json').write_text(json.dumps(self.proof))

    def test_only_identity_bytes_normalized(self):
        with patch.object(release_exact,'reference',return_value=self.ref):
            actual,report=release_exact.normalize('synthetic',self.file)
        self.assertEqual(actual,self.expected)
        self.assertTrue(report['exact_application_verified'])
        self.assertEqual(self.file.read_bytes(),self.data)

    def test_changed_machine_code_cannot_claim_release(self):
        changed=b'!'+self.data[1:];self.file.write_bytes(changed)
        self.proof['candidate_sha256']=sha(changed);self.save()
        with patch.object(release_exact,'reference',return_value=self.ref), self.assertRaises(ValueError):
            release_exact.normalize('synthetic',self.file)

    def test_unverified_candidate_rejected(self):
        self.proof['passed']=False;self.save()
        with self.assertRaises(ValueError):release_exact.normalize('synthetic',self.file)

    def test_identity_address_outside_candidate(self):
        self.proof['debug_identity']['linked_copies']=['0x4015ffff'];self.save()
        with patch.object(release_exact,'reference',return_value=self.ref), self.assertRaises(ValueError):
            release_exact.normalize('synthetic',self.file)

    def test_final_update_must_match_release(self):
        with patch.object(release_exact,'reference',return_value={'package_sha256':sha(b'expected')}),self.assertRaises(ValueError):
            release_exact.finalize('z9-c532',b'different',b'')

if __name__=='__main__':unittest.main()
