"""Execute compiled page/navigation entries against original initialized data.

The populated page-root pointer is an explicit RAM fixture. No UI, startup,
card hardware or original application transition is simulated as success.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from platform_profiles import ROOT,BASE,load,verify_page_namespace

BINDINGS={
 'zr-c110':dict(root=0xa034dfc20,navigate=0x403bba78,apply=0x403b9c74,prefix='fx_c110',
                image='c110-menu.snapshot',code='c110-menu.bin'),
 'z9-c532':dict(root=0xa032d5de8,navigate=0x403afa44,apply=0x403adbac,prefix='fx_z9',
                image='z9-core.main',code='core.bin')}

def verify(target,firmware,build):
    if not __debug__:raise ValueError('native instruction verification requires assertions; do not use Python -O')
    profile,original=load(target,firmware);namespace=verify_page_namespace(profile,original);b=BINDINGS[target]
    report=json.loads((build/'build.json').read_text());candidate=(build/b['image']).read_bytes()
    digest=report['candidate_sha256'] if target=='z9-c532' else report['sha256']
    if hashlib.sha256(candidate).hexdigest()!=digest:raise ValueError('candidate differs')
    if report.get('navigation_hook_version')!=1:raise ValueError('missing separate page namespace/navigation')
    for name,value in report['source_sha256'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=value:raise ValueError('source changed '+name)
    symbols={k:int(v,16) if isinstance(v,str) else v for k,v in report['symbols'].items()}
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE,UC_PROT_READ,UC_PROT_EXEC
    from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X20,UC_ARM64_REG_X30,UC_ARM64_REG_SP,UC_ARM64_REG_PC
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(BASE,(len(candidate)+4095)&~4095,UC_PROT_READ|UC_PROT_EXEC);u.mem_write(BASE,candidate)
    ram=0xa02800000;u.mem_map(ram,0x4000000)
    data_file=struct.unpack_from('<Q',original,0x8148)[0]
    data_ram=struct.unpack_from('<Q',original,0x80d8)[0];data_bytes=struct.unpack_from('<Q',original,0x80d0)[0]
    u.mem_write(data_ram,original[data_file:data_file+data_bytes])
    page0=namespace['first_file']-96-data_file+data_ram
    for group in range(7):u.mem_write(b['root']+8*group,struct.pack('<Q',page0))
    stack=0x60000000;stop=stack+0x3f000;u.mem_map(stack,0x40000)
    observed=[];writes=[]
    def write(uc,access,address,size,value,ctx):
        if not stack<=address<stack+0x3f000:writes.append((address,size))
    def hook(uc,pc,size,ctx):
        if pc in (b['navigate']+16,b['apply']):
            observed.append((pc,uc.reg_read(UC_ARM64_REG_X0),uc.reg_read(UC_ARM64_REG_X1)))
            uc.emu_stop()
    u.hook_add(UC_HOOK_MEM_WRITE,write);u.hook_add(UC_HOOK_CODE,hook)
    def call(entry,x,y=0):
        observed.clear();u.reg_write(UC_ARM64_REG_X0,x);u.reg_write(UC_ARM64_REG_X1,y)
        u.reg_write(UC_ARM64_REG_X30,stop);u.reg_write(UC_ARM64_REG_SP,stack+0x30000)
        u.emu_start(entry,stop,count=10000)
        return u.reg_read(UC_ARM64_REG_X0)
    getter=symbols[b['prefix']+'_menu_page'];nav=symbols[b['prefix']+'_menu_navigate']
    getter_checks=0
    # Include the zero separators: they must not become extension metadata.
    for group in (2,3,5):
        for page in range(1,profile['native_pages']['stop']):
            assert call(getter,group,page)==page0+page*96,(group,page)
            assert u.reg_read(UC_ARM64_REG_PC)==stop
            getter_checks+=1
    for page in range(namespace['fenestra'],namespace['extension_limit']):
        assert call(getter,2,page)==0,'uninstalled custom page must be absent'
    assert call(getter,7,1)==0
    trampoline=symbols[b['prefix']+'_navigate_original']
    assert bytes(u.mem_read(trampoline,16))==original[b['navigate']-BASE:b['navigate']-BASE+16]
    nav_checks=0
    # All stock IDs AND original special selectors pass through unchanged.
    for page in range(1,profile['native_pages']['stop']+13):
        call(nav,page);assert observed and observed[0][0]==b['navigate']+16
        assert u.reg_read(UC_ARM64_REG_X20)==page
        nav_checks+=1
    for page in range(namespace['fenestra'],namespace['extension_limit']):
        call(nav,page);assert observed==[(b['apply'],2,page)],observed
        nav_checks+=1
    assert not writes,('non-stack writes before menu installation',writes[:5])
    return dict(passed=True,target=target,candidate_sha256=digest,native_namespace=namespace,
        stock_page_getter_checks=getter_checks,navigator_checks=nav_checks,non_stack_writes=0,
        fixtures=['populated native page-root pointers','stop at original navigation continuation/application transition'],
        original_transition_executed=False,qemu_test=False,physical_qualified=False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--target',choices=tuple(BINDINGS),required=True)
    p.add_argument('--firmware',required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if not out.is_relative_to(ROOT) or out.exists():raise ValueError('new owned output required')
    result=verify(a.target,a.firmware,a.build);out.parent.mkdir(parents=True,exist_ok=True)
    with out.open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps(result))
if __name__=='__main__':main()
