"""Client decoding and compatibility for the native worker debug protocol."""
from pathlib import Path
import struct
import sys
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from ptp_client import Client,BackendError

class Transport:
    def __init__(self):self.caps=31;self.data=b'';self.result=0;self.writes=[]
    def write(self,op,params,payload):self.writes.append(payload)
    def read(self,op,params,maximum):
        if op==0x95e0:return struct.pack('<4sIQ4I',b'FXP2',2,7,1024,self.caps,110,1)+bytes(32)
        request=self.writes[-1]
        raw=request[:20]+struct.pack('<III',len(self.data),0x80000000|self.result,0)+self.data
        return raw[:28]+struct.pack('<I',zlib.crc32(raw))+raw[32:]

class DiagnosticsTests(unittest.TestCase):
    def setUp(self):self.t=Transport();self.c=Client(self.t);self.c.hello()
    def test_old_firmware_rejects_locally(self):
        self.t.caps=3;self.c.hello()
        for invoke in (self.c.diagnostics,self.c.regions,lambda:self.c.read_region(1,0,16)):
            with self.assertRaises(RuntimeError):invoke()
        self.assertEqual(self.t.writes,[])
    def test_diagnostics_decode(self):
        self.t.data=struct.pack('<4sIIIQ10I3Q7I12x',b'FXD1',1,128,4,123456,*range(10),
            0x96000004,0x123456789,0xa10000000,*range(7))
        d=self.c.diagnostics()
        self.assertEqual((d['phase'],d['uptime_ms'],d['el0_exits'],d['far']),(4,123456,7,'0xa10000000'))
    def test_region_decode_and_unknown_schema(self):
        self.t.data=struct.pack('<4sIII',b'FXR1',1,2,32)+struct.pack('<4I2Q',1,1,4908,0,0xa01000000,0)+struct.pack('<4I2Q',2,1,16384,1,0xa02000000,0)
        self.assertEqual(self.c.regions()[1]['name'],'module-image')
        self.t.data=self.t.data[:20]+struct.pack('<I',3)+self.t.data[24:]
        with self.assertRaises(ValueError):self.c.regions()
    def test_read_encoding_and_bounds(self):
        self.t.data=bytes(range(256))*4
        self.assertEqual(self.c.read_region(2,4096,1024),self.t.data)
        self.assertEqual(self.t.writes[-1][32:],struct.pack('<III',2,4096,1024))
        for args in [(0,0,1),(3,0,1),(1,-1,1),(1,1<<32,1),(1,0,0),(1,0,1025)]:
            with self.assertRaises(ValueError):self.c.read_region(*args)
        self.t.data=b'x'
        with self.assertRaises(ValueError):self.c.read_region(1,0,2)
    def test_backend_reject_does_not_leave_pending(self):
        self.t.result=1
        with self.assertRaises(BackendError):self.c.read_region(1,4908,1)
        self.assertIsNone(self.c.pending)
    def test_bad_diagnostic_length(self):
        self.t.data=bytes(128)
        with self.assertRaises(ValueError):self.c.diagnostics()

if __name__=='__main__':unittest.main()
