"""Z8 host PTP contract recognition tests (identification only; device-side
PTP on the Z8 is still stubbed and reports FX_CAPABILITY)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import ptp_contract


class Z8PtpContractTests(unittest.TestCase):
    def test_z8_profile_recognized(self):
        self.assertEqual(ptp_contract.identify('Nikon Z8', 'C 3.11'), 'z8-c311')
        self.assertEqual(ptp_contract.identify('Nikon Z8', '3.11'), 'z8-c311')
        self.assertEqual(ptp_contract.identify('Z8', 'V3.11'), 'z8-c311')
        self.assertEqual(ptp_contract.identify('Nikon Z8', 'C 3.11', 'z8-c311'), 'z8-c311')

    def test_z8_explicit_profile_rejects_other_models(self):
        with self.assertRaises(ValueError):
            ptp_contract.identify('Nikon Z8', 'C 3.11', 'z9-c532')

    def test_existing_profiles_unaffected(self):
        self.assertEqual(ptp_contract.identify('Z 9', '5.32'), 'z9-c532')
        self.assertEqual(ptp_contract.identify('Z9', 'V5.32'), 'z9-c532')
        self.assertEqual(ptp_contract.identify('ZR', '1.10'), 'zr-c110')

    def test_check_profile_includes_z8(self):
        self.assertEqual(ptp_contract.check_profile('z8-c311'), 'z8-c311')
        self.assertEqual(ptp_contract.check_profile('auto'), 'auto')
        with self.assertRaisesRegex(ValueError, 'z8-c311'):
            ptp_contract.check_profile('z8-c301')


if __name__ == '__main__':
    unittest.main()
