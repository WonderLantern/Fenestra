/* Original context operations on newly allocated, unpublished tables only.
 * Every pool-page PA is checked before the native allocator sees it. No TTBR,
 * stock table, DMA user or cache alias is published by this control. */
#include "fenestra_mmu_sdk.h"
#include "fenestra_translation.h"
static FxSdkClient client;
static uint32_t stage;
static uint64_t memory,translation,buffer,address,physical_root,physical_pool;
static uint64_t functions[5];
static FxMmuArenaLayout arena;
static const char *const names[]={"rtos.mmu.context.map","rtos.mmu.context.unmap",
    "rtos.mmu.context.protect","rtos.mmu.context.translate","rtos.mmu.context.attributes"};
static uint64_t u64(const uint8_t *b){uint64_t v=0;for(uint32_t i=0;i<8u;++i)v|=(uint64_t)b[i]<<(8u*i);return v;}
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(client.pending) {
        FxServiceReply a;FxResult r=fx_sdk_take(&client,api,&a);if(r==FX_BUSY)return 0;
        if(r!=FX_OK || a.result!=FX_OK)return 100+(int)stage;
        if(stage==1u)memory=a.value[0];
        if(stage==2u)translation=a.value[0];
        if(stage==3u) {
            buffer=a.value[0];address=a.value[1];
            if(fx_mmu_arena_layout(address,65536u,8u,&arena)!=FX_OK)return 2;
        }
        if(stage>=4u && stage<=12u) {
            uint64_t par=a.value[0];if(par&1u)return 3;
            uint64_t pa=par&UINT64_C(0x0000fffffffff000);
            if(stage==4u)physical_root=pa;
            else if(stage==5u)physical_pool=pa;
            else if(pa!=physical_pool+(uint64_t)(stage-5u)*4096u)return 4;
        }
        if(stage>=14u && stage<=18u)functions[stage-14u]=a.value[0];
        if((stage==19u || stage==20u || stage==22u || stage==24u || stage==25u || stage==27u) && a.value[0])return 5;
        if(stage==21u && (a.bytes!=8u || u64(a.data)!=0x40160123u))return 6;
        if(stage==23u && (a.bytes!=8u || (u64(a.data)&0x3fu)!=6u))return 7;
        if(stage==26u && (a.bytes!=8u || (u64(a.data)&0x3fu)!=5u))return 8;
        if(stage==28u && (uint32_t)a.value[0]!=UINT32_C(0xffffffe6))return 9;
        ++stage;
    }
    FxResult r;uint64_t args[8]={0};
    if(stage==0u)r=fx_sdk_lifecycle(&client,api,FX_LIFE_CONFIGURE,FX_MODULE_APP);
    else if(stage==1u)r=fx_sdk_open(&client,api,"core.memory",1u);
    else if(stage==2u)r=fx_sdk_open(&client,api,"core.translation",1u);
    else if(stage==3u)r=fx_sdk_memory_allocate(&client,api,memory,65536u,1u);
    else if(stage>=4u && stage<=12u) {
        args[0]=stage==4u?arena.root:arena.pool+(uint64_t)(stage-5u)*4096u;
        args[1]=FX_TRANSLATE_EL1_WRITE;
        r=fx_sdk_platform(&client,api,FX_BIND_CALL,translation,0u,0u,args,sizeof(args));
    } else if(stage==13u) {
        FxNativeMmuContext context;
        if(fx_mmu_context_prepare(&context,&arena,physical_root,physical_pool)!=FX_OK)return 10;
        r=fx_sdk_mmu_context_write(&client,api,memory,buffer,&context);
    } else if(stage>=14u && stage<=18u)r=fx_sdk_open(&client,api,names[stage-14u],1u);
    else if(stage==19u)r=fx_sdk_mmu_map(&client,api,functions[0],address,0x12345000u,4096u,0x40160000u,6u);
    else if(stage==20u || stage==22u || stage==25u || stage==28u)
        r=fx_sdk_mmu_query(&client,api,functions[stage==20u || stage==28u?3u:4u],address,0x12345123u,address+96u);
    else if(stage==21u || stage==23u || stage==26u) {
        FxMemoryRequest q={buffer,8u,96u};r=fx_sdk_platform(&client,api,FX_BIND_CALL,memory,FX_MEMORY_READ,0u,&q,sizeof(q));
    } else if(stage==24u)r=fx_sdk_mmu_protect(&client,api,functions[2],address,0x12345000u,4096u,5u);
    else if(stage==27u)r=fx_sdk_mmu_unmap(&client,api,functions[1],address,0x12345000u,4096u);
    else if(stage>=29u && stage<=33u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,functions[stage-29u],0u,0u,0,0);
    else if(stage==34u)r=fx_sdk_memory_release(&client,api,memory,buffer,1u);
    else if(stage==35u || stage==36u)r=fx_sdk_platform(&client,api,FX_BIND_CLOSE,stage==35u?translation:memory,0u,0u,0,0);
    else r=fx_sdk_lifecycle(&client,api,FX_LIFE_FINISH,0u);
    return r==FX_OK?0:11;
}
