"""USB packet/endpoint/ownership fixtures, not physical Linux USB evidence."""
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from ptp_usb import UsbPtp,device_info,HEADER
from ptp_client import PtpError


def container(kind,code,tid,data=b''):return HEADER.pack(12+len(data),kind,code,tid)+data
def string(text):return bytes([len(text)+1])+text.encode('utf-16-le')+b'\0\0'
def info(version='1.10',model='ZR'):
    return struct.pack('<HIH',100,10,100)+string('Nikon')+b'\0\0'+bytes(20)+b''.join(map(string,('Nikon Corporation',model,version,'serial')))


class Interface(list):
    bInterfaceClass=6;bInterfaceSubClass=1;bInterfaceProtocol=1;bAlternateSetting=0;bInterfaceNumber=2
    def __init__(self):
        super().__init__([types.SimpleNamespace(bmAttributes=2,bEndpointAddress=e,wMaxPacketSize=512) for e in (0x81,2)])
class Device:
    def __init__(self):
        self.incoming=[];self.writes=[];self.version='1.10';self.model='ZR';self.kernel=False;self.open_code=0x2001
        self.fragment=4096;self.vendor_data=b'hello';self.response_code=0x2001;self.bad_tid=False;self.fail_write=False
    def get_active_configuration(self):return [Interface()]
    def is_kernel_driver_active(self,i):return self.kernel
    def write(self,endpoint,raw,timeout):
        assert endpoint==2 and timeout>0
        raw=bytes(raw);self.writes.append(raw)
        if self.fail_write:return len(raw)-1
        if not raw:return 0
        length,kind,opcode,tid=HEADER.unpack_from(raw);assert length==len(raw)
        if opcode==0x1001:self.incoming.append(container(2,opcode,tid,info(self.version,self.model))+container(3,0x2001,tid))
        elif opcode==0x1002:self.incoming.append(container(3,self.open_code,tid))
        elif opcode==0x1003:self.incoming.append(container(3,0x2001,tid))
        elif opcode==0x95e1:
            if kind==2:self.incoming.append(container(3,self.response_code,tid))
        else:
            if self.bad_tid:tid+=1
            raw=container(3,self.response_code,tid)
            if self.response_code==0x2001:raw=container(2,opcode,tid,self.vendor_data)+raw
            self.incoming.append(raw)
        return length
    def read(self,endpoint,n,timeout):
        assert endpoint==0x81 and n==4096 and timeout>0
        if not self.incoming:raise TimeoutError('USB fixture empty')
        raw=self.incoming[0];cut=min(n,self.fragment,len(raw));result=raw[:cut]
        if cut==len(raw):self.incoming.pop(0)
        else:self.incoming[0]=raw[cut:]
        return result


class UsbTests(unittest.TestCase):
    def setUp(self):
        self.device=Device();self.actions=[]
        util=types.ModuleType('usb.util')
        for name in ('claim_interface','release_interface','dispose_resources'):
            setattr(util,name,lambda *args,n=name:self.actions.append(n))
        core=types.ModuleType('usb.core');core.find=lambda **kw:[self.device]
        usb=types.ModuleType('usb');usb.core=core;usb.util=util
        self.patch=patch.dict(sys.modules,{'usb':usb,'usb.core':core,'usb.util':util});self.patch.start()
        self.addCleanup(self.patch.stop)
    def opcodes(self):return [HEADER.unpack_from(raw)[2] for raw in self.device.writes if raw]
    def test_dataset_bounds_and_identity(self):
        self.assertEqual(device_info(info())['model'],'ZR')
        for raw in (info()[:-1],info()+b'0',b'\0'*8+b'\xff'):
            with self.assertRaises(ValueError):device_info(raw)
    def test_fragmentation_coalescing_and_complete_close(self):
        self.device.fragment=7
        with UsbPtp() as t:
            self.assertEqual(t.read(0x95e0,(),64),b'hello')
            t.write(0x95e1,(),bytes(1056))
            self.assertEqual(t.tid,2)
        self.assertEqual(self.opcodes(),[0x1001,0x1002,0x95e0,0x95e1,0x95e1,0x1003])
        self.assertEqual(self.actions,['claim_interface','release_interface','dispose_resources'])
    def test_zlp_terminates_packet_multiple(self):
        with UsbPtp() as t:t.write(0x95e1,(),bytes(500))
        self.assertIn(b'',self.device.writes)
    def test_reject_new_firmware_before_session(self):
        self.device.version='1.11'
        with self.assertRaises(ValueError):UsbPtp().__enter__()
        self.assertEqual(self.opcodes(),[0x1001])
        self.assertEqual(self.actions[-2:],['release_interface','dispose_resources'])
    def test_z9_uses_its_own_model_and_version_before_session(self):
        self.device.model='Z 9';self.device.version='5.32'
        with UsbPtp(profile='z9-c532') as t:
            self.assertEqual(t.profile,'z9-c532')
            self.assertEqual(t.identity['model'],'Z 9')
            self.assertEqual(t.read(0x95e0,(),64),b'hello')
        self.assertEqual(self.opcodes(),[0x1001,0x1002,0x95e0,0x1003])
    def test_explicit_wrong_profile_does_not_open_session(self):
        with self.assertRaises(ValueError):UsbPtp(profile='z9-c532').__enter__()
        self.assertEqual(self.opcodes(),[0x1001])
    def test_no_kernel_detach_or_close_foreign_session(self):
        self.device.kernel=True
        with self.assertRaises(RuntimeError):UsbPtp().__enter__()
        self.assertEqual(self.device.writes,[])
        self.device.kernel=False;self.device.open_code=0x201e
        with self.assertRaises(PtpError):UsbPtp().__enter__()
        self.assertNotIn(0x1003,self.opcodes())
    def test_mismatched_tid_taints_without_retry_or_close_command(self):
        with UsbPtp() as t:
            self.device.bad_tid=True
            with self.assertRaises(ValueError):t.read(0x95e0,(),64)
            n=len(self.device.writes)
            with self.assertRaises(RuntimeError):t.read(0x95e0,(),64)
            self.assertEqual(len(self.device.writes),n)
        self.assertNotIn(0x1003,self.opcodes())
    def test_oversized_data_and_short_write_taint(self):
        with UsbPtp() as t:
            self.device.vendor_data=b'x'*65
            with self.assertRaises(ValueError):t.read(0x95e0,(),64)
            self.assertTrue(t.tainted)
        self.device=Device()
        with UsbPtp() as t:
            self.device.fail_write=True
            with self.assertRaises(OSError):t.read(0x95e0,(),64)
            self.assertTrue(t.tainted)
    def test_busy_response_remains_synchronized(self):
        with UsbPtp() as t:
            self.device.response_code=0x2019
            with self.assertRaises(PtpError):t.read(0x95e2,(1,2,3,4,1024),1056)
            self.assertFalse(t.tainted)
            self.device.response_code=0x2001
            self.assertEqual(t.read(0x95e0,(),64),b'hello')
    def test_no_arbitrary_vendor_opcode(self):
        with UsbPtp() as t:
            n=len(self.device.writes)
            with self.assertRaises(ValueError):t.read(0xf001,(),64)
            with self.assertRaises(ValueError):t.write(0x95e1,(),b'x')
            self.assertEqual(len(self.device.writes),n)


if __name__=='__main__':unittest.main()
