"""Tests for the hardware-tested Zf profile."""
import json
from pathlib import Path
import tempfile
import unittest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import zf_c301_profile as profile


class ZfProfileTests(unittest.TestCase):
    def test_profile_records_link81_hardware_validation(self):
        value = profile.load_profile()
        self.assertEqual(value['support_state'], 'link81-hardware-tested')
        self.assertEqual(value['link81']['card_read_max_bytes'], 511)
        self.assertEqual(value['native_menu']['setup_page'], 672)
        self.assertEqual(value['native_menu']['setup_stock_rows'] + value['native_menu']['setup_extra_rows_required'], 49)
        self.assertEqual(value['sysctl_events']['app_first_start_complete'], 20)
        self.assertEqual(value['ptp']['operations_capacity'], '0x200')
        self.assertTrue(value['native_menu']['ui_dispatch']['profile_available'])
        bindings = value['core_bindings']['rtos_functions']
        self.assertEqual(bindings['memory_allocate'], '0x40d31898')
        self.assertEqual(bindings['memory_release'], '0x40d318d8')
        self.assertEqual(bindings['same_address_from_z9'], 58)

    def test_identity_checks_accept_synthetic_pinned_lengths_only(self):
        value = profile.load_profile()
        module = value['original_eg_module']
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'eg'
            path.write_bytes(b'\0' * module['payload_bytes'])
            with self.assertRaises(ValueError):
                profile.verify_application(path, value)
            path.write_bytes(b'\0' * (module['payload_bytes'] - 1))
            with self.assertRaises(ValueError):
                profile.verify_application(path, value)

    def test_corrupt_profile_is_rejected(self):
        value = profile.load_profile()
        value['firmware'] = 'C 3.00'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile.json'
            path.write_text(json.dumps(value))
            with self.assertRaises(ValueError):
                profile.load_profile(path)


if __name__ == '__main__': unittest.main()
