"""Linux USB/PTP transport using PyUSB + libusb, without driver detachment/reset.

PyUSB API: https://github.com/pyusb/pyusb/blob/master/docs/tutorial.rst
Wire framing follows the original C1.10 PTP container parser (12-byte header).
Import is inert. Only GetDeviceInfo/OpenSession/CloseSession and FXP2 are sent.
"""
import secrets
import struct
import time
from ptp_client import PtpError
from ptp_contract import check_profile,identify,validate_transfer

HEADER=struct.Struct('<IHHI')


def device_info(raw):
    """Bounded PTP DeviceInfo dataset parser, including UTF-16 code units."""
    offset=0
    def take(n):
        nonlocal offset
        if n<0 or n>len(raw)-offset:raise ValueError('truncated DeviceInfo')
        value=raw[offset:offset+n];offset+=n;return value
    def string():
        n=take(1)[0]
        if not n:return ''
        value=take(n*2)
        if value[-2:]!=b'\0\0':raise ValueError('DeviceInfo string terminator')
        return value[:-2].decode('utf-16-le',errors='strict')
    take(8) # StandardVersion, VendorExtensionID, VendorExtensionVersion
    string();take(2) # VendorExtensionDesc, FunctionalMode
    arrays=[]
    for _ in range(5):
        n=struct.unpack('<I',take(4))[0]
        if n>4096:raise ValueError('oversized DeviceInfo array')
        arrays.append(struct.unpack('<'+'H'*n,take(n*2)))
    manufacturer,model,version,serial=(string() for _ in range(4))
    if offset!=len(raw):raise ValueError('DeviceInfo trailing bytes')
    return dict(manufacturer=manufacturer,model=model,version=version,serial=serial,operations=arrays[0])


class UsbPtp:
    def __init__(self,timeout_ms=5000,profile='auto'):
        if not 1<=timeout_ms<=30000:raise ValueError('USB timeout1..30000 ms')
        self.timeout_ms=timeout_ms;self.device=None;self.interface=None;self.opened=False
        self.tainted=False;self.tid=0;self.buffer=bytearray();self.util=None
        self.expected_profile=check_profile(profile)

    def __enter__(self):
        import usb.core
        import usb.util
        self.util=usb.util
        matches=list(usb.core.find(find_all=True,idVendor=0x04b0))
        if len(matches)!=1:raise RuntimeError('expected exactly one Nikon USB device')
        self.device=matches[0]
        try:
            config=self.device.get_active_configuration()
            interfaces=[i for i in config if (i.bInterfaceClass,i.bInterfaceSubClass,i.bInterfaceProtocol)==(6,1,1)
                        and i.bAlternateSetting==0]
            if len(interfaces)!=1:raise RuntimeError('expected one active Still Image/PTP interface')
            interface=interfaces[0]
            if self.device.is_kernel_driver_active(interface.bInterfaceNumber):
                raise RuntimeError('PTP interface is owned by a kernel driver; no automatic detach')
            endpoints=[e for e in interface if e.bmAttributes&3==2]
            incoming=[e for e in endpoints if e.bEndpointAddress&0x80]
            outgoing=[e for e in endpoints if not e.bEndpointAddress&0x80]
            if len(incoming)!=1 or len(outgoing)!=1:raise RuntimeError('PTP bulk endpoint topology differs')
            self.ep_in=incoming[0].bEndpointAddress;self.ep_out=outgoing[0].bEndpointAddress
            self.packet=incoming[0].wMaxPacketSize
            self.out_packet=outgoing[0].wMaxPacketSize
            if self.packet not in (64,512,1024) or self.out_packet not in (64,512,1024):raise RuntimeError('unsupported USB bulk packet size')
            self.util.claim_interface(self.device,interface.bInterfaceNumber)
            self.interface=interface.bInterfaceNumber
            info=device_info(self._exchange(0x1001,(),None,65536,0))
            if info['manufacturer'].strip().lower() not in ('nikon','nikon corporation'):
                raise ValueError('expected Nikon manufacturer')
            self.profile=identify(info['model'],info['version'],self.expected_profile)
            self.identity=info
            # Never close/reuse a pre-existing session: OpenSession's 0x201e
            # remains an error and close() sends nothing unless OUR open passed.
            self._exchange(0x1002,(secrets.randbelow(0xffffffff)+1,),None,0,0)
            self.opened=True
            return self
        except BaseException:
            try:self.close()
            except Exception:pass
            raise

    def _milliseconds(self,deadline):
        remaining=deadline-time.monotonic()
        if remaining<=0:raise TimeoutError('PTP transaction deadline; no replay')
        return max(1,int(remaining*1000))

    def _send(self,kind,code,tid,payload,deadline):
        raw=HEADER.pack(12+len(payload),kind,code,tid)+payload
        if self.device.write(self.ep_out,raw,timeout=self._milliseconds(deadline))!=len(raw):
            raise OSError('short USB write; uncertain transaction retained, no replay')
        if kind==2 and len(raw)%self.out_packet==0:
            if self.device.write(self.ep_out,b'',timeout=self._milliseconds(deadline))!=0:
                raise OSError('USB data-phase ZLP failed')

    def _container(self,tid,maximum,deadline):
        def fill(n):
            while len(self.buffer)<n:
                # A bulk read buffer must be a packet multiple; read ahead is
                # retained so coalesced data/response containers work as well.
                part=bytes(self.device.read(self.ep_in,4096,timeout=self._milliseconds(deadline)))
                if not part:continue # USB ZLP is not a PTP container
                if len(part)>4096:raise ValueError('USB read exceeded requested buffer')
                self.buffer.extend(part)
        fill(12)
        length,kind,code,transaction=HEADER.unpack_from(self.buffer)
        limit=maximum+12 if kind==2 else 32
        if length<12 or length>limit or kind not in (2,3) or transaction!=tid:
            raise ValueError('PTP container identity/type/length differs')
        fill(length);payload=bytes(self.buffer[12:length]);del self.buffer[:length]
        if kind==3 and len(payload)%4:raise ValueError('PTP response parameter alignment')
        return kind,code,payload

    def _exchange(self,opcode,params,payload,maximum,tid):
        if self.device is None or self.tainted:raise RuntimeError('USB transport closed or uncertain')
        deadline=time.monotonic()+self.timeout_ms/1000
        try:
            self._send(1,opcode,tid,struct.pack('<'+'I'*len(params),*params),deadline)
            if payload is not None:self._send(2,opcode,tid,bytes(payload),deadline)
            kind,code,data=self._container(tid,maximum,deadline)
            if kind==2:
                if payload is not None or not maximum or code!=opcode:raise ValueError('unexpected PTP data phase')
                kind,code,response=self._container(tid,0,deadline)
                if kind!=3:raise ValueError('PTP response missing')
            else:data=None
            if self.buffer:raise ValueError('unsolicited bulk container after response')
            if code!=0x2001:raise PtpError(code)
            if maximum and data is None:raise ValueError('PTP successful read missing data')
            return data if data is not None else b''
        except PtpError:
            raise # Complete, matching error response leaves framing synchronized.
        except BaseException:
            self.tainted=True;raise

    def transfer(self,opcode,params,payload,maximum):
        if not self.opened:raise RuntimeError('PTP session not open')
        params=validate_transfer(opcode,params,payload,maximum)
        if self.tid==0xfffffffe:raise RuntimeError('PTP transaction IDs exhausted; reopen')
        self.tid+=1
        return self._exchange(opcode,params,payload,maximum,self.tid)

    def read(self,opcode,params,maximum):return self.transfer(opcode,params,None,maximum)
    def write(self,opcode,params,payload):self.transfer(opcode,params,payload,0)
    def close(self):
        try:
            if self.opened and not self.tainted:
                self.tid+=1
                self._exchange(0x1003,(),None,0,self.tid)
        finally:
            self.opened=False
            try:
                if self.interface is not None:self.util.release_interface(self.device,self.interface)
            finally:
                self.interface=None
                if self.device is not None:
                    self.util.dispose_resources(self.device);self.device=None

    def __exit__(self,kind,*args):
        try:self.close()
        except Exception:
            if kind is None:raise
