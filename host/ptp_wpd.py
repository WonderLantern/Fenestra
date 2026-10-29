"""Windows native WPD MTP extensions; constructing this object does not open USB.

Constants/transfer lifecycle: Microsoft WpdMtpExtensions.h and PortableDevice.h,
https://learn.microsoft.com/windows/win32/wpd_sdk/supporting-mtp-extensions
The COM contract is locally tested; actual camera transfers require qualification.
"""
import ctypes as c
import os
from pathlib import Path
from ptp_contract import check_profile,identify,nikon_interface,validate_transfer

ROOT=Path(__file__).resolve().parents[1]
COMMON='{F0422A9C-5DC8-4440-B5BD-5DF28835658A}'
MTP='{4D545058-1A2E-4106-A357-771E0819FC56}'


class WpdPtp:
    def __init__(self,profile='auto'):
        self.expected_profile=check_profile(profile)
        import comtypes
        import comtypes.client
        import comtypes.gen
        self.com=comtypes;self.client=comtypes.client;self.device=None
        generated=ROOT/'build/comtypes_gen';generated.mkdir(parents=True,exist_ok=True)
        self.client.gen_dir=str(generated);comtypes.gen.__path__=[str(generated)]
        self.client.GetModule(str(Path(os.environ['SystemRoot'])/'System32/PortableDeviceApi.dll'))
        self.client.GetModule(str(Path(os.environ['SystemRoot'])/'System32/PortableDeviceTypes.dll'))
        from comtypes.gen import PortableDeviceApiLib as api, PortableDeviceTypesLib as types
        self.api=api;self.types=types
        self.free=c.OleDLL('ole32').CoTaskMemFree
        self.free.argtypes=[c.c_void_p];self.free.restype=None

    def key(self,pid,category=MTP):
        k=self.api._tagpropertykey();k.fmtid=self.com.GUID(category);k.pid=pid;return k

    def values(self):
        return self.client.CreateObject(self.types.PortableDeviceValues,interface=self.api.IPortableDeviceValues)

    def command(self,id):
        v=self.values();category=self.com.GUID(MTP)
        v.SetGuidValue(c.byref(self.key(1001,COMMON)),c.byref(category))
        v.SetUnsignedIntegerValue(c.byref(self.key(1002,COMMON)),id)
        return v

    def operation(self,id,opcode,params):
        if opcode not in (0x95e0,0x95e1,0x95e2,0x95e3) or len(params)>5 or any(not 0<=n<1<<32 for n in params):
            raise ValueError('only bounded Fenestra PTP operations permitted')
        v=self.command(id)
        v.SetUnsignedIntegerValue(c.byref(self.key(1001)),opcode)
        collection=self.client.CreateObject(self.types.PortableDevicePropVariantCollection,
            interface=self.api.IPortableDevicePropVariantCollection)
        temporary=self.values()
        for n in params:
            temporary.SetUnsignedIntegerValue(c.byref(self.key(1001)),n)
            variant=temporary.GetValue(c.byref(self.key(1001)))
            collection.Add(c.byref(variant))
        v.SetIPortableDevicePropVariantCollectionValue(c.byref(self.key(1002)),collection)
        return v

    def __enter__(self):
        cfg=c.WinDLL('CfgMgr32.dll');guid=self.com.GUID('{6AC27878-A6FA-4155-BA85-F98F491D4F33}')
        cfg.CM_Get_Device_Interface_List_SizeW.argtypes=[c.POINTER(c.c_ulong),c.POINTER(self.com.GUID),c.c_wchar_p,c.c_ulong]
        cfg.CM_Get_Device_Interface_ListW.argtypes=[c.POINTER(self.com.GUID),c.c_wchar_p,c.c_wchar_p,c.c_ulong,c.c_ulong]
        size=c.c_ulong()
        if cfg.CM_Get_Device_Interface_List_SizeW(c.byref(size),c.byref(guid),None,0):raise RuntimeError('WPD enumeration failed')
        text=c.create_unicode_buffer(size.value)
        if cfg.CM_Get_Device_Interface_ListW(c.byref(guid),None,text,size.value,0):raise RuntimeError('WPD enumeration failed')
        matches=[x for x in text[:].split('\0') if nikon_interface(x)]
        if len(matches)!=1:raise RuntimeError('expected one Nikon WPD device')
        d=self.client.CreateObject(self.api.PortableDeviceFTM,interface=self.api.IPortableDevice)
        d.Open(matches[0],self.values());self.device=d
        try:
            keys=self.client.CreateObject(self.types.PortableDeviceKeyCollection,interface=self.api.IPortableDeviceKeyCollection)
            model=self.key(8,'{26D4979A-E643-4626-9E2B-736DC0C92FDC}')
            version=self.key(3,'{26D4979A-E643-4626-9E2B-736DC0C92FDC}')
            keys.Add(c.byref(model));keys.Add(c.byref(version))
            values=d.Content().Properties().GetValues('DEVICE',keys)
            self.identity=dict(model=values.GetStringValue(c.byref(model)),version=values.GetStringValue(c.byref(version)))
            self.profile=identify(self.identity['model'],self.identity['version'],self.expected_profile)
        except BaseException:
            self.close();raise
        return self

    def close(self):
        if self.device is not None:
            d=self.device;self.device=None;d.Close()

    def __exit__(self,*args):self.close()

    def send(self,values):
        if self.device is None:raise RuntimeError('WPD not open')
        result=self.device.SendCommand(0,values)
        hr=result.GetErrorValue(c.byref(self.key(1003,COMMON)))
        if hr<0:raise OSError(f'WPD HRESULT 0x{hr&0xffffffff:08x}')
        return result

    def check_response(self,result,required=False):
        from ptp_client import PtpError
        try:code=result.GetUnsignedIntegerValue(c.byref(self.key(1003)))
        except self.com.COMError:
            if required:raise
            return
        if code!=0x2001:raise PtpError(code)

    def context_command(self,id,context):
        v=self.command(id);v.SetStringValue(c.byref(self.key(1006)),context);return v

    def transfer(self,opcode,params,payload,maximum):
        params=validate_transfer(opcode,params,payload,maximum)
        writing=payload is not None
        if writing and (opcode!=0x95e1 or not 32<=len(payload)<=1056):raise ValueError('invalid Fenestra submit')
        if not writing and (opcode not in (0x95e0,0x95e2,0x95e3) or not 1<=maximum<=1056):raise ValueError('invalid Fenestra read')
        start=self.operation(14 if writing else 13,opcode,params)
        if writing:start.SetUnsignedLargeIntegerValue(c.byref(self.key(1007)),len(payload))
        result=self.send(start)
        try:context=result.GetStringValue(c.byref(self.key(1006)))
        except self.com.COMError:
            self.check_response(result)
            raise
        output=bytearray();failed=False
        try:
            self.check_response(result)
            total=len(payload) if writing else result.GetUnsignedLargeIntegerValue(c.byref(self.key(1007)))
            if total>(1056 if writing else maximum):raise ValueError('unexpected transfer size')
            offset=0
            while offset<total:
                n=min(1056,total-offset)
                v=self.context_command(16 if writing else 15,context)
                v.SetUnsignedIntegerValue(c.byref(self.key(1010 if writing else 1008)),n)
                raw=(c.c_ubyte*n).from_buffer_copy(payload[offset:offset+n] if writing else bytes(n))
                v.SetBufferValue(c.byref(self.key(1012)),raw,n)
                part=self.send(v)
                transferred=part.GetUnsignedIntegerValue(c.byref(self.key(1011 if writing else 1009)))
                if not 0<transferred<=n:raise ValueError('invalid WPD transfer progress')
                if not writing:
                    pointer,count=part.GetBufferValue(c.byref(self.key(1012)))
                    try:
                        if count!=transferred:raise ValueError('WPD buffer count differs')
                        output.extend(c.string_at(pointer,count))
                    finally:self.free(pointer)
                offset+=transferred
        except BaseException:
            failed=True;raise
        finally:
            try:self.check_response(self.send(self.context_command(17,context)),required=True)
            except Exception:
                if not failed:raise
        return bytes(output)

    def read(self,opcode,params,maximum):return self.transfer(opcode,params,None,maximum)
    def write(self,opcode,params,payload):self.transfer(opcode,params,payload,0)
