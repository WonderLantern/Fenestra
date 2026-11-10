"""Synthetic SDK target and package checks; no camera or firmware required."""
import struct
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import build_sdk_module
import zf_c301_profile
from module_header import validate_package
from pack_module import pack
from platform_profiles import PROFILES
from zf_c301 import EG_PAYLOAD_SHA256


def minimal_elf():
    """A synthetic AArch64 ELF for the packer's structural checks, not execution."""
    data = bytearray(120)
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQQIHHHHHH', data, 16, 3, 183, 1, 0, 64, 120, 0, 64, 56, 1, 64, 0, 0)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0, 0, 120, 120, 1)
    return bytes(data)


class ZfSdkTests(unittest.TestCase):
    def test_zf_identity_is_an_sdk_target_only(self):
        self.assertEqual(build_sdk_module.SDK_TARGETS['zf-c301'], EG_PAYLOAD_SHA256)
        self.assertEqual(build_sdk_module.SDK_TARGETS['zf-c301'],
                         zf_c301_profile.load_profile()['original_eg_module']['payload_sha256'])
        self.assertEqual(EG_PAYLOAD_SHA256, '6891edd22d9a40042e4718480d29082682bbd12aea172c51f4f164b97f74fe08')
        self.assertNotIn('zf-c301', PROFILES)
        self.assertEqual(build_sdk_module.SDK_TARGETS['z9-c532'], PROFILES['z9-c532']['module_identity'])
        self.assertEqual(build_sdk_module.SDK_TARGETS['zr-c110'], PROFILES['zr-c110']['module_identity'])

    def test_module_identity_and_body_are_checked(self):
        identity = bytes.fromhex(EG_PAYLOAD_SHA256)
        module = pack(minimal_elf(), identity, 0x3f0000, entry_abi=1)
        self.assertEqual(module[24:56], identity)
        self.assertEqual(validate_package(module, identity)['entry_abi'], 1)
        with self.assertRaises(ValueError):
            validate_package(module, bytes.fromhex(PROFILES['z9-c532']['module_identity']))
        corrupted = bytearray(module)
        corrupted[-1] ^= 1
        with self.assertRaises(ValueError):
            validate_package(corrupted, identity)

    def test_toolchain_selection_prefers_llvm_and_falls_back_to_gnu(self):
        available = {'clang': '/llvm/clang', 'ld.lld': '/llvm/ld.lld',
                     'aarch64-linux-gnu-gcc': '/gnu/gcc', 'aarch64-linux-gnu-ld': '/gnu/ld'}
        with patch.object(build_sdk_module.shutil, 'which', side_effect=available.get):
            self.assertEqual(build_sdk_module.compiler_toolchain(), ('/llvm/clang', '/llvm/ld.lld', 'llvm'))
        available.pop('clang')
        with patch.object(build_sdk_module.shutil, 'which', side_effect=available.get):
            self.assertEqual(build_sdk_module.compiler_toolchain(), ('/gnu/gcc', '/gnu/ld', 'gnu-aarch64'))
        available.pop('aarch64-linux-gnu-ld')
        with patch.object(build_sdk_module.shutil, 'which', side_effect=available.get):
            with self.assertRaises(RuntimeError):
                build_sdk_module.compiler_toolchain()


if __name__ == '__main__':
    unittest.main()
