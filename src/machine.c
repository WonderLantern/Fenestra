#include "fenestra_machine.h"
#include "fenestra_memory.h"
FxResult fx_machine_resource(void *context,uint32_t op,const FxNamedBinding *b,FxPlatformOwner *o,uint64_t token,
    uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)b;(void)token;
    if(op==FX_RESOURCE_ATTACH){*cookie=1u;return FX_OK;}
    if(*cookie!=1u)return FX_STALE;
    if(op==FX_RESOURCE_DETACH){*cookie=0;return FX_OK;}
    if(op==FX_RESOURCE_PANIC)return FX_OK;
    if(op!=FX_RESOURCE_CALL || !q || !a)return FX_ARGUMENT;
    if(q->arg[2]==FX_MACHINE_QUERY) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxMachineInfo info={.abi=1u,.bytes=sizeof(info),.operations=o->platform->io.scalar?6u:0u,.max_range=1024u*1024u};
        if(o->platform->io.cache) {
            FxResult r=o->platform->io.cache(o->platform->io.context,(uintptr_t)&info,sizeof(info),FX_MACHINE_QUERY);
            if(r!=FX_OK)return r;
            info.operations|=0xf8u;
        }
        a->value[0]=1u;a->value[1]=info.operations&6u;a->value[2]=info.operations&0xf8u;a->value[3]=info.max_range;
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(q->bytes!=sizeof(FxMachineRequest) || q->arg[3])return FX_ARGUMENT;
    FxMachineRequest r;memcpy(&r,q->data,sizeof(r));
    if(r.reserved || r.address>UINT64_MAX-r.bytes)return FX_BOUNDS;
    if(q->arg[2]==FX_MACHINE_READ || q->arg[2]==FX_MACHINE_WRITE) {
        if(!o->platform->io.scalar)return FX_CAPABILITY;
        if((r.bytes!=1u && r.bytes!=2u && r.bytes!=4u && r.bytes!=8u) || (r.address&(r.bytes-1u)))return FX_BOUNDS;
        if(q->arg[2]==FX_MACHINE_WRITE && r.bytes<8u)r.value&=(UINT64_C(1)<<(8u*r.bytes))-1u;
        FxResult result=o->platform->io.scalar(o->platform->io.context,r.address,r.bytes,&r.value,(uint32_t)(q->arg[2]==FX_MACHINE_WRITE));
        if(result==FX_OK)a->value[0]=r.value;
        return result;
    }
    if(q->arg[2]<FX_MACHINE_BARRIER || q->arg[2]>FX_MACHINE_INSTRUCTIONS)return FX_ARGUMENT;
    if(!o->platform->io.cache)return FX_CAPABILITY;
    if(q->arg[2]==FX_MACHINE_BARRIER) {
        if(r.address || r.bytes || r.value)return FX_ARGUMENT;
    } else if(!r.bytes || r.bytes>1024u*1024u || r.value)return FX_BOUNDS;
    return o->platform->io.cache(o->platform->io.context,r.address,r.bytes,(uint32_t)q->arg[2]);
}
