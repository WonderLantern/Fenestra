"""Tests for local-only Zf menu preimage preparation."""
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import zf_c301_menu_displaced as displaced


class ZfMenuDisplacedTests(unittest.TestCase):
    def test_sites_match_native_menu_inventory(self):
        self.assertEqual(set(displaced.MENU_HOOKS), {'page', 'text', 'navigate'})
        self.assertEqual([displaced.MENU_HOOKS[k]['bytes'] for k in ('page', 'text', 'navigate')], [52, 108, 340])

    def test_missing_input_is_rejected(self):
        with self.assertRaises((FileNotFoundError, OSError)):
            displaced.prepare(ROOT / 'missing-zf-eg', ROOT / 'build' / 'missing-menu-output')


if __name__ == '__main__':
    unittest.main()
