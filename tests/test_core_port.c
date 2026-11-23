#include "fenestra_core_port.h"
#include <assert.h>
#include <string.h>

enum { PORT_BYTES=4096 };
static const uintptr_t base=UINT32_C(0x401cc000);

static FxCorePort make_port(void) {
    FxCorePort port={
        .magic=FX_CORE_PORT_MAGIC,.abi=FX_CORE_PORT_ABI,.header_bytes=sizeof(port),
        .install=base+256u,.attach=base+264u,.dispatch=base+272u
    };
    memset(port.application,0xa5,sizeof(port.application));
    memset(port.core,0x5a,sizeof(port.core));
    for(uint32_t i=0;i<FX_CORE_PORT_IMPORT_COUNT;++i)port.imports[i]=base+512u+8u*i;
    return port;
}

int main(void) {
    const FxCorePort original=make_port();
    FxCorePort port=original;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_OK);
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.core,original.core)==FX_INTEGRITY);
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.application)==FX_INTEGRITY);
    assert(fx_core_port_validate(&port,sizeof(port),base,original.application,original.core)==FX_ARGUMENT);
    assert(fx_core_port_validate(&port,PORT_BYTES,UINTPTR_MAX-7u,original.application,original.core)==FX_ARGUMENT);
    ++port.abi;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_INTEGRITY);
    port=original;port.reserved=1u;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_INTEGRITY);
    port=original;port.install=base;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_BOUNDS);
    port=original;port.dispatch=base+PORT_BYTES;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_BOUNDS);
    port=original;port.imports[0]=0u;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_BOUNDS);
    port=original;port.imports[3]+=4u;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_BOUNDS);
    port=original;port.imports[3]=base+PORT_BYTES-8u;
    assert(fx_core_port_validate(&port,PORT_BYTES,base,original.application,original.core)==FX_BOUNDS);
    return 0;
}
