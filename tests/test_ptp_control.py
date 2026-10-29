"""Out-of-band cancellation cannot accidentally replay an uncertain request."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from ptp_client import Client,PtpError,CONTROL


class Transport:
    def __init__(self):
        self.state=1;self.cancelled=0;self.lost=False;self.bad=False;self.operations=[]
        self.session=0x123456789;self.request_id=0

    def write(self,op,params,data):
        self.operations.append(op);self.request_id=struct.unpack_from('<I',data,16)[0]
        self.state=1;self.cancelled=0

    def read(self,op,params,maximum):
        self.operations.append(op)
        if op==0x95e0:
            raw=bytearray(struct.pack('<4sIQ4I',b'FXP2',2,self.session,1024,3,110,1)+bytes(32))
            struct.pack_into('<I',raw,36,4);return bytes(raw)
        assert op==CONTROL and maximum==32
        low,high,id,action=params
        assert low|high<<32==self.session and id==self.request_id
        if action==1:
            self.cancelled=1
            if self.state!=2:self.state=4
        if action==2:
            if self.state in (1,2):raise PtpError(0x2019)
            self.state=5
        if self.lost:self.lost=False;raise TimeoutError('control ACK lost')
        return struct.pack('<4s5IQ',b'FXC1',1,id+int(self.bad),self.state,self.cancelled,0,self.session)


class ControlTests(unittest.TestCase):
    def setUp(self):
        self.transport=Transport();self.client=Client(self.transport);self.client.hello()
        self.client.submit(2,b'retained')

    def test_queued_cancel_is_immediate_and_repeatable(self):
        self.assertEqual(self.client.cancel()['state'],4);self.assertIsNone(self.client.pending)
        self.assertEqual(self.client.cancel()['state'],4)
        self.assertEqual(self.client.release()['state'],5)
        self.assertEqual(self.transport.operations.count(0x95e1),1)
        self.assertNotIn(0x95e2,self.transport.operations)

    def test_running_cancel_retains_until_worker_finishes(self):
        self.transport.state=2
        result=self.client.cancel()
        self.assertTrue(result['cancel_requested']);self.assertFalse(result['terminal'])
        self.assertIsNotNone(self.client.pending)
        with self.assertRaises(PtpError):self.client.release()
        with self.assertRaises(RuntimeError):self.client.submit(2,b'new')
        self.transport.state=4
        self.assertEqual(self.client.job_control()['state'],4);self.assertIsNone(self.client.pending)

    def test_lost_control_ack_keeps_original_request(self):
        original=self.client.pending;self.transport.lost=True
        with self.assertRaises(TimeoutError):self.client.cancel()
        self.assertEqual(self.client.pending,original)
        self.assertEqual(self.client.cancel()['state'],4);self.assertIsNone(self.client.pending)
        self.assertEqual(self.transport.operations.count(0x95e1),1)

    def test_malformed_control_identity_keeps_request(self):
        self.transport.bad=True;original=self.client.pending
        with self.assertRaises(ValueError):self.client.cancel()
        self.assertEqual(self.client.pending,original)

    def test_old_endpoint_does_not_probe_new_command(self):
        self.client.control_available=False;count=len(self.transport.operations)
        with self.assertRaises(RuntimeError):self.client.cancel()
        self.assertEqual(len(self.transport.operations),count)


if __name__=='__main__':unittest.main()
