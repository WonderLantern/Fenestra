import ctypes as c
from pathlib import Path
import struct
import sys
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from ptp_client import Client,BackendError,PtpError,request_frame,parse_response


class Transport:
    def __init__(self):self.writes=[];self.lost=False;self.code=0;self.busy=0;self.bad=False
    def read(self,op,params,maximum):
        if op==0x95e0:return struct.pack('<4sIQ4I',b'FXP2',2,0x123456789,1024,3,110,1)+bytes(32)
        assert op==0x95e2 and maximum==1056
        if self.busy:self.busy-=1;raise PtpError(0x2019)
        request=self.writes[-1]
        response=request[:24]+struct.pack('<I',0x80000000|self.code)+bytes(4)+request[32:]
        response=response[:28]+struct.pack('<I',zlib.crc32(response))+response[32:]
        return response[:-1]+bytes([response[-1]^1]) if self.bad else response
    def write(self,op,params,payload):
        assert op==0x95e1 and params==()
        self.writes.append(payload)
        if self.lost:self.lost=False;raise TimeoutError('acknowledgement lost')


class ClientTests(unittest.TestCase):
    def setUp(self):self.t=Transport();self.client=Client(self.t);self.client.hello()
    def test_binary_and_busy(self):
        data=bytes(range(256))*4;self.client.submit(2,data);self.t.busy=2
        self.assertEqual(self.client.fetch(),data);self.assertIsNone(self.client.pending)
    def test_lost_ack_reuses_exact_request(self):
        self.t.lost=True
        with self.assertRaises(TimeoutError):self.client.submit(2,b'x')
        with self.assertRaises(RuntimeError):self.client.submit(2,b'x')
        self.client.retry_submit();self.assertEqual(self.t.writes[0],self.t.writes[1])
        self.assertEqual(self.client.fetch(),b'x')
    def test_backend_error_is_terminal_and_recoverable(self):
        self.client.submit(99);self.t.code=5
        with self.assertRaises(BackendError):self.client.fetch()
        self.assertIsNone(self.client.pending)
        self.t.code=0;self.client.submit(2,b'next');self.assertEqual(self.client.fetch(),b'next')
    def test_corrupt_reply_does_not_lose_pending_job(self):
        self.client.submit(2,b'keep');self.t.bad=True
        with self.assertRaises(ValueError):self.client.fetch()
        self.assertIsNotNone(self.client.pending)
        self.t.bad=False;self.assertEqual(self.client.fetch(),b'keep')
    def test_argument_and_identity_bounds(self):
        for session,id,data in [(0,1,b''),(1,0,b''),(1,1<<32,b''),(1,1,b'a'*1025)]:
            with self.assertRaises(ValueError):request_frame(session,id,2,data)
        self.client.submit(2,b'x');response=self.t.read(0x95e2,(),1056)
        with self.assertRaises(ValueError):parse_response(response,99,1,2)


@unittest.skipUnless(sys.platform=='win32','Windows COM contract')
class WpdLocalTests(unittest.TestCase):
    def setUp(self):
        from ptp_wpd import WpdPtp
        self.w=WpdPtp()
    def test_typed_parameters_without_opening_camera(self):
        values=self.w.operation(13,0x95e2,(1,2,3,4,1056))
        self.assertEqual(values.GetUnsignedIntegerValue(c.byref(self.w.key(1001))),0x95e2)
        collection=values.GetIPortableDevicePropVariantCollectionValue(c.byref(self.w.key(1002)))
        count=c.c_ulong();collection.GetCount(c.byref(count));self.assertEqual(count.value,5)
        self.assertIsNone(self.w.device)
    def test_buffer_roundtrip_and_free(self):
        data=bytes(range(256));v=self.w.values();raw=(c.c_ubyte*len(data)).from_buffer_copy(data)
        v.SetBufferValue(c.byref(self.w.key(1012)),raw,len(data))
        pointer,count=v.GetBufferValue(c.byref(self.w.key(1012)))
        try:self.assertEqual(c.string_at(pointer,count),data)
        finally:self.w.free(pointer)
    def test_non_fenestra_operation_rejected_before_transport(self):
        with self.assertRaises(ValueError):self.w.operation(13,0x9440,())
        with self.assertRaises(ValueError):self.w.operation(13,0x95e2,(1,)*6)


if __name__=='__main__':unittest.main()
