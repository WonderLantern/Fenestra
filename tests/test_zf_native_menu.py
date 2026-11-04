"""Synthetic checks for Zf native-menu input validation; no camera required."""
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1] / 'host'))
import zf_native_menu as menu


class ZfNativeMenuTests(unittest.TestCase):
    def test_zf_menu_hook_windows_are_explicit(self):
        self.assertEqual(menu.MENU_HOOKS['page']['address'], 0x403e42b4)
        self.assertEqual(menu.MENU_HOOKS['text']['address'], 0x40338ee8)
        self.assertEqual(menu.MENU_HOOKS['navigate']['address'], 0x403b8024)
        self.assertEqual([menu.MENU_HOOKS[n]['bytes'] for n in ('page','text','navigate')], [52,108,340])
        self.assertTrue(menu.ENTRY_CONTRACTS['page']['complete_function'])
        self.assertTrue(menu.ENTRY_CONTRACTS['text']['complete_function'])
        self.assertFalse(menu.ENTRY_CONTRACTS['navigate']['complete_function'])

    def test_capacity_distinguishes_old_and_required_array_sizes(self):
        self.assertEqual(menu.capacity(47,2,None)['sufficient'],None)
        self.assertFalse(menu.capacity(47,2,47)['sufficient'])
        self.assertTrue(menu.capacity(47,2,49)['sufficient'])
        self.assertEqual(menu.capacity(47,2,49)['required_rows'],49)
        for bad in (-1,True,1.5):
            with self.assertRaises(ValueError):
                menu.capacity(47,2,bad)

    def test_namespace_checks_every_slot_and_preserves_gaps(self):
        image = bytearray(3*96)
        struct.pack_into('<I',image,0,(1 << 16) | 2)
        struct.pack_into('<I',image,2*96,(3 << 16) | 5)
        counts,gaps = menu.namespace(image,0,4)
        self.assertEqual(dict(counts),{2:1,0:1,5:1})
        self.assertEqual(gaps,[2])
        struct.pack_into('<I',image,2*96,(4 << 16) | 5)
        with self.assertRaises(ValueError):
            menu.namespace(image,0,4)
        with self.assertRaises(ValueError):
            menu.namespace(image,0,5)

    def test_address_translation_checks_complete_range(self):
        image=bytes(range(100))
        self.assertEqual(bytes(menu._read(image,0xa00000000,4,20,0xa00000000,40)),image[20:24])
        self.assertEqual(bytes(menu._read(image,menu.BASE+6,4,20,0xa00000000,40)),image[6:10])
        with self.assertRaises(ValueError):
            menu._read(image,0xa00000000+39,2,20,0xa00000000,40)

    def test_text_table_strides_and_corrupt_boundaries(self):
        # Construct independent small pools with 2-byte entries and correct
        # 2899-offset / 2898-attribute per-language spacing.
        data=bytearray(4_000_000)
        offset_ptrs,pool_ptrs,attr_ptrs,classes=1000,2000,3000,4000
        offset_base,pool_base,attr_base=100_000,1_000_000,2_000_000
        for language in range(29):
            table=offset_base+language*2899*4
            pool=pool_base+language*2
            attrs=attr_base+language*2898
            struct.pack_into('<Q',data,offset_ptrs+8*language,table)
            struct.pack_into('<Q',data,pool_ptrs+8*language,pool)
            struct.pack_into('<Q',data,attr_ptrs+8*language,attrs)
            struct.pack_into('<2899I',data,table,0,*([2]*2898))
        def read(address,size):
            if address<0 or address+size>len(data):
                raise ValueError('bad range')
            return memoryview(data)[address:address+size]
        with patch.multiple(menu,OFFSETS=offset_ptrs,POOLS=pool_ptrs,ATTRIBUTES=attr_ptrs,CLASSES=classes):
            offsets,pools,attributes,ends=menu._text_tables(read)
            self.assertEqual((len(offsets),len(pools),len(attributes),ends), (29,29,29,[2]*29))
            struct.pack_into('<Q',data,attr_ptrs+8,attr_base+2899)
            with self.assertRaises(ValueError):
                menu._text_tables(read)
            struct.pack_into('<Q',data,attr_ptrs+8,attr_base+2898)
            struct.pack_into('<I',data,offset_base+4,3)
            with self.assertRaises(ValueError):
                menu._text_tables(read)

    def test_unrecognized_application_is_rejected(self):
        with self.assertRaises(ValueError):
            menu.inspect(b'not a firmware image')


if __name__=='__main__':
    unittest.main()
