"""Independent, user-firmware-backed native binding verification."""
import hashlib
from pathlib import Path
import native_binding_inventory
ROOT=Path(__file__).resolve().parents[1]
BASE=0x40160000
def sha256(data):return hashlib.sha256(data).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)
def local(name):
    p=(ROOT/name).resolve(strict=True)
    require(p.is_relative_to(ROOT),'source outside tree')
    return p
def verify_binding_windows(build, original):
    """Reconstruct the complete native-call inventory and check immutable bytes."""
    from platform_profiles import PROFILES
    require(hashlib.sha256(original).hexdigest()==PROFILES['zr-c110']['sha256'], 'original EG fingerprint differs')
    expected=[]
    for kind in ('display_abi','file_abi'):
        for name,window in build[kind]['functions'].items():
            address=int(window['address'],16);data=bytes.fromhex(window['hex']);at=address-0x40160000
            require(at>=0 and data and original[at:at+len(data)]==data,'native ABI preimage differs: '+name)
            expected.append(dict(name=kind+'/'+name,address=window['address'],bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    from native_binding_inventory import addresses as native_addresses
    addresses=native_addresses(build,resolve_source=local)
    for address in sorted(addresses):
        at=address-0x40160000
        require(0<=at and at+32<=len(original),'native call outside original EG')
        expected.append(dict(name='native-call',address=hex(address),bytes=32,sha256=hashlib.sha256(original[at:at+32]).hexdigest()))
    require(build.get('native_binding_windows')==expected,'native binding inventory or original bytes differ')
    return len(expected)

def binding_evidence(build, receipt, original):
    count=verify_binding_windows(build,original)
    require(receipt.get('native_binding_windows_matched')==count,'incomplete C1.10 binding audit')
