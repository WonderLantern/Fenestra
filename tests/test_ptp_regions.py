import struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from ptp_client import Client,BackendError

class RegionTests(unittest.TestCase):
    def client(self,raw):
        c=Client.__new__(Client);c.query=lambda operation,data=b'':raw;return c
    def frame(self,rows):
        return struct.pack('<4sIII',b'FXR2',2,len(rows),32)+b''.join(struct.pack('<6IQ',*r) for r in rows)
    def row(self,owner,kind,size):
        return ((owner-1)*2+kind+1,1,size,kind,0x1000,0,owner|(kind<<32))
    def test_multiple_owners_and_sparse_identifiers(self):
        rows=self.client(self.frame([self.row(1,0,8475),self.row(1,1,131072),self.row(3,0,3702)])).regions()
        self.assertEqual([r['id'] for r in rows],[1,2,5]);self.assertEqual([r['owner'] for r in rows],[1,1,3])
        self.assertEqual([r['volatile'] for r in rows],[False,True,False])
    def test_empty_runtime_has_no_invented_demo_region(self):
        self.assertEqual(self.client(self.frame([])).regions(),[])
    def test_original_schema_still_decodes(self):
        raw=struct.pack('<4sIII',b'FXR1',1,2,32)+struct.pack('<4I2Q',1,1,4096,0,0x1000,0)+struct.pack('<4I2Q',2,1,65536,1,0x2000,0)
        self.assertEqual([r['name'] for r in self.client(raw).regions()],['package','module-image'])
    def test_malformed_descriptors_are_rejected(self):
        good=list(self.row(2,1,131072))
        for at,value in ((0,2),(1,0),(2,4194305),(3,0),(6,2|(2<<32))):
            row=good.copy();row[at]=value
            with self.subTest(at=at),self.assertRaises(ValueError):self.client(self.frame([row])).regions()
        raw=self.frame([good,good])
        with self.assertRaises(ValueError):self.client(raw).regions()
        with self.assertRaises(ValueError):self.client(raw[:-1]).regions()

    def paged(self,rows,total,next):
        return struct.pack('<4s7I',b'FXR3',3,len(rows),32,total,next,0,0)+b''.join(struct.pack('<6IQ',*r) for r in rows)
    def test_paged_regions_above_four_owners_and_empty_slots(self):
        frames={0:self.paged([self.row(i,kind,512) for i in range(1,16) for kind in (0,1)]+[self.row(16,0,512)],40,31),
                31:self.paged([self.row(16,1,512),self.row(20,0,512),self.row(20,1,512)],40,0xffffffff)}
        seen=[];client=Client.__new__(Client)
        def query(op,data=b''):
            self.assertEqual(op,4);cursor=struct.unpack('<I',data)[0];seen.append(cursor);return frames[cursor]
        client.query=query;rows=client.regions()
        self.assertEqual(seen,[0,31]);self.assertEqual(len(rows),34);self.assertEqual(rows[-1]['owner'],20)
    def test_only_completed_legacy_argument_falls_back(self):
        client=Client.__new__(Client);seen=[]
        def query(op,data=b''):
            seen.append(data)
            if data:raise BackendError(1)
            return self.frame([])
        client.query=query;self.assertEqual(client.regions(),[]);self.assertEqual(seen,[bytes(4),b''])
        client.query=lambda op,data=b'':(_ for _ in ()).throw(TimeoutError('retained uncertain request'))
        with self.assertRaises(TimeoutError):client.regions()
    def test_malformed_paging_cannot_loop_or_duplicate_regions(self):
        for raw in (self.paged([],40,1),self.paged([self.row(1,0,512)],40,0),
                    self.paged([self.row(1,0,512)],0,0xffffffff),self.paged([self.row(2,0,512)],40,2)):
            with self.subTest(raw=raw[:32]),self.assertRaises(ValueError):self.client(raw).regions()

if __name__=='__main__':unittest.main()
