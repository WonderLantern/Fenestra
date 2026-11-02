"""Reject a relocated or changed Zf RTOS helper even after input validation."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
import zf_c301_rtos_profile as profile
import zf_c301_linux_profile as linux
class RtosProfileTests(unittest.TestCase):
    def test_checked_headers_and_rejection_before_output(self):
        data=bytes(range(64));helpers={'command':(0x40160000,16,hashlib.sha256(data[:16]).hexdigest())}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with patch.multiple(profile,ROOT=root,PREIMAGES=helpers),patch.object(profile,'_payload',return_value=data):
                result=profile.prepare('synthetic',root/'build/valid')
                self.assertFalse(result['runtime_qualified'])
                self.assertFalse(result['ptp_policy_qualified'])
                self.assertIn('0x40160000',(root/'build/valid/fx_zf_rtos_profile.h').read_text())
                with self.assertRaises(ValueError):profile.prepare('synthetic',root/'build/valid')
            for mutated in [bytes([data[0]^1])+data[1:],data[16:]+data[:16]]:
                with patch.multiple(profile,ROOT=root,PREIMAGES=helpers),patch.object(profile,'_payload',return_value=mutated):
                    with self.assertRaisesRegex(ValueError,'helper differs'):profile.prepare('synthetic',root/'build/bad')
                    self.assertFalse((root/'build/bad').exists())
    def test_linux_transport_headers_and_changed_window_are_checked(self):
        data=bytes(range(64))
        helpers={'receive':(0x40160000,32,hashlib.sha256(data[:32]).hexdigest())}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with patch.multiple(linux,ROOT=root,PREIMAGES=helpers),patch.object(linux,'_payload',return_value=data):
                result=linux.prepare('synthetic',root/'build/valid')
                self.assertFalse(result['runtime_qualified'])
                self.assertFalse(result['transport_activated'])
                self.assertIn('FX_ZF_LINUX_RECEIVE',(root/'build/valid/fx_zf_linux_profile.h').read_text())
            changed=bytes([data[0]^1])+data[1:]
            with patch.multiple(linux,ROOT=root,PREIMAGES=helpers),patch.object(linux,'_payload',return_value=changed):
                with self.assertRaisesRegex(ValueError,'helper differs'):linux.prepare('synthetic',root/'build/bad')
                self.assertFalse((root/'build/bad').exists())

    def test_command_entry_is_dispatch_not_registration(self):
        self.assertEqual(profile.PREIMAGES['command'][:2],(0x40c018a8,452))
        self.assertNotEqual(profile.PREIMAGES['command'][0],0x40c01890)
if __name__=='__main__':unittest.main()
