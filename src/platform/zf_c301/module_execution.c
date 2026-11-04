#if !defined(FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR) || FX_ZF_EXPERIMENTAL_MODULE_EXECUTOR != 1
#error "Zf module execution requires the explicit experimental executor"
#endif

#include "module_execution.h"
#include "fenestra_memory.h"

#define FX_ZF_CODE_BEGIN UINT64_C(0x401a0000)
#define FX_ZF_CODE_END   UINT64_C(0x40200000)
#define FX_ZF_PAGE 4096u

static void *allocate(uint32_t bytes) {
    return ((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d31898u)(bytes,0u);
}

static void *allocate_aligned(uint32_t bytes,uint32_t alignment,void **raw_out) {
    if(!bytes || !alignment || (alignment&(alignment-1u)) || bytes>UINT32_MAX-(alignment-1u))return 0;
    void *raw=allocate(bytes+alignment-1u);
    if(!raw)return 0;
    uintptr_t aligned=((uintptr_t)raw+alignment-1u)&~(uintptr_t)(alignment-1u);
    memset((void *)aligned,0,bytes);
    *raw_out=raw;
    return (void *)aligned;
}

static uintptr_t page_down(uintptr_t value) { return value & ~(uintptr_t)(FX_ZF_PAGE-1u); }
static uintptr_t page_up(uintptr_t value) { return (value+FX_ZF_PAGE-1u)&~(uintptr_t)(FX_ZF_PAGE-1u); }

static int probe(void *context,uintptr_t va,uint32_t flags) {
    FxZfOwnerStorage *owner=context;
    uintptr_t begin=page_down((uintptr_t)owner),end=page_up((uintptr_t)owner+sizeof(*owner));
    uintptr_t private_begin=(uintptr_t)owner->module_private_tables;
    uintptr_t user_begin=(uintptr_t)owner->module_user_tables;
    uintptr_t plan_begin=(uintptr_t)owner->module_expected;
    uintptr_t plan_end=page_up(plan_begin+
        (FX_MODULE_MEMORY_LIMIT/FX_EXEC_PAGE)*2u*sizeof(uint64_t));
    uintptr_t image_begin=page_down((uintptr_t)owner->module_image_raw);
    uintptr_t image_end=page_up((uintptr_t)owner->module_image_raw+
        owner->module_runtime.stage.loader.module.bytes);
    uintptr_t stack_begin=page_down((uintptr_t)owner->module_user_stack_storage);
    uintptr_t stack_end=page_up((uintptr_t)owner->module_user_stack_storage+owner->module_user_stack_bytes);
    int owned=(va>=begin && va<end) ||
        (private_begin && va>=private_begin && va<private_begin+FX_ZF_TABLES*FX_ZF_PAGE) ||
        (user_begin && va>=user_begin && va<user_begin+FX_ZF_TABLES*FX_ZF_PAGE) ||
        (owner->module_plan_raw && va>=plan_begin && va<plan_end) ||
        (owner->module_image_raw && va>=image_begin && va<image_end) ||
        (owner->module_user_stack_storage && va>=stack_begin && va<stack_end) ||
        (va>=FX_ZF_CODE_BEGIN && va<FX_ZF_CODE_END);
    if(!owned)return 0;
#if defined(__aarch64__)
    uint64_t par;
    if(flags&FX_ZF_MAP_WRITE)
        __asm__ volatile("at s1e1w,%1; isb; mrs %0,par_el1":"=r"(par):"r"(va):"memory");
    else
        __asm__ volatile("at s1e1r,%1; isb; mrs %0,par_el1":"=r"(par):"r"(va):"memory");
    return !(par&1u) && (par&UINT64_C(0x0000fffffffff000))==va && (par>>56)==255u;
#else
    (void)flags;return 1;
#endif
}

static volatile uint64_t *image_leaf(FxZfOwnerStorage *owner) {
    uint64_t *table=owner->module_private_tables;
    uintptr_t va=(uintptr_t)owner->module_image;
    for(uint32_t shift=30u;shift>12u;shift-=9u) {
        uint64_t pte=table[(va>>shift)&511u];
        uintptr_t next=(uintptr_t)(pte&UINT64_C(0x0000fffffffff000));
        if((pte&3u)!=3u || next<(uintptr_t)owner->module_private_tables ||
           next>=(uintptr_t)owner->module_private_tables+FX_ZF_TABLES*FX_ZF_PAGE)return 0;
        table=(uint64_t *)next;
    }
    return &table[(va>>12)&511u];
}

static FxResult map_private(FxZfOwnerStorage *owner) {
    uintptr_t begin=page_down((uintptr_t)owner);
    uint32_t bytes=(uint32_t)(page_up((uintptr_t)owner+sizeof(*owner))-begin);
    FxResult result=fx_zf_space_init(&owner->module_private_space,owner->module_private_tables,
        FX_ZF_TABLES,owner,probe);
    if(result==FX_OK)result=fx_zf_space_map(&owner->module_private_space,begin,bytes,
        FX_ZF_MAP_READ|FX_ZF_MAP_WRITE);
    uintptr_t image=page_down((uintptr_t)owner->module_image);
    uint32_t image_bytes=(uint32_t)(page_up((uintptr_t)owner->module_image+
        owner->module_runtime.stage.loader.module.bytes)-image);
    if(result==FX_OK)result=fx_zf_space_map(&owner->module_private_space,image,image_bytes,
        FX_ZF_MAP_READ|FX_ZF_MAP_WRITE);
    /* The gate keeps using its owner stack and the arena/table metadata after
     * TTBR0 changes. These allocations must exist in the private EL1 view. */
    if(result==FX_OK)result=fx_zf_space_map(&owner->module_private_space,
        (uintptr_t)owner->module_private_tables,2u*FX_ZF_TABLES*FX_ZF_PAGE,
        FX_ZF_MAP_READ|FX_ZF_MAP_WRITE);
    uintptr_t plan=(uintptr_t)owner->module_expected;
    uint32_t plan_bytes=(uint32_t)(page_up(plan+
        (FX_MODULE_MEMORY_LIMIT/FX_EXEC_PAGE)*2u*sizeof(uint64_t))-plan);
    if(result==FX_OK)result=fx_zf_space_map(&owner->module_private_space,plan,plan_bytes,
        FX_ZF_MAP_READ|FX_ZF_MAP_WRITE);
    if(result==FX_OK)result=fx_zf_space_map(&owner->module_private_space,
        (uintptr_t)owner->module_user_stack,owner->module_user_stack_bytes,
        FX_ZF_MAP_READ|FX_ZF_MAP_WRITE);
    if(result==FX_OK)result=fx_zf_space_map(&owner->module_private_space,FX_ZF_CODE_BEGIN,
        (uint32_t)(FX_ZF_CODE_END-FX_ZF_CODE_BEGIN),FX_ZF_MAP_READ|FX_ZF_MAP_EXECUTE);
    if(result==FX_OK)result=fx_zf_space_seal(&owner->module_private_space,&owner->module_private_root);
    return result;
}

static FxResult map_user(FxZfOwnerStorage *owner) {
    FxResult result=fx_zf_space_init(&owner->module_user_space,owner->module_user_tables,
        FX_ZF_TABLES,owner,probe);
    uintptr_t api=page_down((uintptr_t)owner->module_user_api);
    uintptr_t exchange=page_down((uintptr_t)owner->module_user_exchange);
    uintptr_t stack=page_down((uintptr_t)owner->module_user_stack);
    uint32_t image_bytes=owner->module_runtime.stage.loader.module.bytes;
    uintptr_t trampoline=(uintptr_t)fx_zf_user_return;
    /* Exception entry and SVC return use the owner context and kernel stack
     * before restoring TTBR0. Keep them mapped but inaccessible to EL0. */
    for(uintptr_t va=page_down((uintptr_t)owner);
        result==FX_OK && va<page_up((uintptr_t)owner+sizeof(*owner));va+=FX_ZF_PAGE) {
        uint32_t flags=FX_ZF_MAP_READ|FX_ZF_MAP_WRITE;
        if(va==api)flags=FX_ZF_MAP_READ|FX_ZF_MAP_USER;
        else if(va==exchange)flags|=FX_ZF_MAP_USER;
        result=fx_zf_space_map(&owner->module_user_space,va,FX_ZF_PAGE,flags);
    }
    if(result==FX_OK)result=fx_zf_space_map(&owner->module_user_space,stack,
        owner->module_user_stack_bytes,FX_ZF_MAP_READ|FX_ZF_MAP_WRITE|FX_ZF_MAP_USER);
    if(result==FX_OK) {
        for(uintptr_t va=(uintptr_t)owner->module_image;va<(uintptr_t)owner->module_image+image_bytes;va+=FX_ZF_PAGE) {
            uint64_t pte=owner->module_arena.expected[(va-(uintptr_t)owner->module_image)/FX_ZF_PAGE];
            if(!pte)continue;
            uint32_t flags=FX_ZF_MAP_READ|FX_ZF_MAP_USER;
            if(!(pte&UINT64_C(1)<<7))flags|=FX_ZF_MAP_WRITE;
            if(!(pte&FX_EXEC_PXN))flags|=FX_ZF_MAP_EXECUTE;
            result=fx_zf_space_map(&owner->module_user_space,va,FX_ZF_PAGE,flags);
            if(result!=FX_OK)break;
        }
    }
    if(result==FX_OK && (trampoline<FX_ZF_CODE_BEGIN || trampoline>=FX_ZF_CODE_END || (trampoline&4095u)))result=FX_CAPABILITY;
    if(result==FX_OK) {
        for(uintptr_t va=(uintptr_t)owner->module_private_tables;
            result==FX_OK && va<(uintptr_t)owner->module_private_tables+2u*FX_ZF_TABLES*FX_ZF_PAGE;
            va+=FX_ZF_PAGE)
            result=fx_zf_space_map(&owner->module_user_space,va,FX_ZF_PAGE,
                FX_ZF_MAP_READ|FX_ZF_MAP_WRITE);
    }
    if(result==FX_OK) {
        for(uintptr_t va=FX_ZF_CODE_BEGIN;va<FX_ZF_CODE_END;va+=FX_ZF_PAGE) {
            uint32_t flags=FX_ZF_MAP_READ|FX_ZF_MAP_EXECUTE;
            if(va==page_down(trampoline))flags|=FX_ZF_MAP_USER;
            result=fx_zf_space_map(&owner->module_user_space,va,FX_ZF_PAGE,flags);
            if(result!=FX_OK)break;
        }
    }
    if(result==FX_OK)result=fx_zf_space_seal(&owner->module_user_space,&owner->module_user_root);
    return result;
}

FxResult fx_zf_owner_module_reserve_image(FxZfOwnerStorage *owner,uint32_t bytes) {
    if(!owner || !bytes || bytes>FX_MODULE_MEMORY_LIMIT)return FX_ARGUMENT;
    if(owner->module_image_raw)return bytes<=owner->module_image_capacity?FX_OK:FX_BOUNDS;
    uint32_t capacity=(bytes+FX_ZF_PAGE-1u)&~(FX_ZF_PAGE-1u);
    owner->module_image_raw=allocate_aligned(capacity,FX_ZF_PAGE,
        &owner->module_image_storage_raw);
    if(owner->module_image_raw)owner->module_image_capacity=capacity;
    return owner->module_image_raw?FX_OK:FX_BUDGET;
}

static int publish_operation(FxZfExecContext *context) {
    FxZfOwnerStorage *owner=(FxZfOwnerStorage *)context->operation_context;
    FxZfModuleRuntime *runtime=&owner->module_runtime;
    uint32_t bytes=(owner->module_runtime.stage.loader.module.bytes+4095u)&~4095u;
    owner->module_publish_stage=1u;
    FxResult result=fx_exec_bind(&owner->module_arena,(uintptr_t)owner->module_image,
        (uintptr_t)owner->module_image,bytes,image_leaf(owner),
        (FxExecStorage){owner->module_expected,owner->module_staging,FX_MODULE_MEMORY_LIMIT/FX_EXEC_PAGE});
    owner->module_publish_result=(uint32_t)result;
    owner->module_publish_stage=2u;
    if(result==FX_OK)result=fx_zf_module_runtime_bind_execution(runtime,&owner->module_arena);
    owner->module_publish_result=(uint32_t)result;
    owner->module_publish_stage=3u;
    uint64_t count,freq;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=r"(count),"=r"(freq));
    if(!freq || freq>UINT32_MAX)return FX_CAPABILITY;
    uint64_t now=count/freq*1000u+count%freq*1000u/freq;
    if(result==FX_OK)result=fx_loader_activate_mapped(&runtime->stage.loader,&owner->module_arena,now);
    owner->module_publish_result=(uint32_t)result;
    owner->module_publish_stage=4u;
    if(result==FX_OK)result=map_user(owner);
    owner->module_publish_result=(uint32_t)result;
    owner->module_publish_stage=5u;
    if(result==FX_OK)result=fx_zf_module_runtime_bind_user_view(runtime,&owner->module_exec_context,
        (FxServiceModule *)owner->module_user_api,(FxServiceExchange *)owner->module_user_exchange,
        owner->module_private_root,owner->module_user_root,
        (uintptr_t)owner->module_user_stack+owner->module_user_stack_bytes);
    owner->module_publish_result=(uint32_t)result;
    owner->module_publish_stage=6u;
    return (int)result;
}

FxResult fx_zf_owner_module_prepare(FxZfOwnerStorage *owner) {
    if(!owner || owner->module_runtime.phase!=FX_ZF_MODULE_BOUND)return FX_STATE;
    if(owner->module_runtime.exec_context && owner->module_runtime.user_api &&
       owner->module_runtime.user_exchange && owner->module_runtime.private_root &&
       owner->module_runtime.user_root && owner->module_runtime.user_stack)return FX_OK;
    FxModuleStacks stacks=owner->module_runtime.stage.loader.module.stacks;
    if(!fx_module_stacks_valid(stacks) || stacks.native_bytes>sizeof(owner->worker.stack))
        return FX_CAPABILITY;
    if(owner->module_user_stack_storage && owner->module_user_stack_bytes!=stacks.user_bytes)
        return FX_STATE;
    owner->module_user_stack_bytes=stacks.user_bytes;
    owner->module_user_api=&owner->module_api[0];
    owner->module_user_exchange=(FxServiceExchange *)&owner->module_user_exchange_storage[0];
    owner->module_exec_context.diagnostics.magic=UINT32_C(0x465a4645);
    owner->module_exec_context.diagnostics.version=1u;
    owner->module_prepare_stage=1u;
    const uint32_t table_bytes=2u*FX_ZF_TABLES*FX_ZF_PAGE;
    if(!owner->module_private_tables) {
        owner->module_private_tables=allocate_aligned(table_bytes,FX_ZF_PAGE,&owner->module_tables_raw);
        if(owner->module_private_tables)
            owner->module_user_tables=owner->module_private_tables+FX_ZF_TABLES*512u;
    }
    if(!owner->module_plan_raw) {
        owner->module_expected=allocate_aligned(
            (FX_MODULE_MEMORY_LIMIT/FX_EXEC_PAGE)*2u*(uint32_t)sizeof(uint64_t),FX_ZF_PAGE,
            &owner->module_plan_raw);
        if(owner->module_expected) {
            owner->module_staging=owner->module_expected+FX_MODULE_MEMORY_LIMIT/FX_EXEC_PAGE;
        }
    }
    if(!owner->module_user_stack_storage)
        owner->module_user_stack_storage=allocate_aligned(owner->module_user_stack_bytes,FX_ZF_PAGE,
            &owner->module_user_stack_raw);
    if(!owner->module_image_raw)
        (void)fx_zf_owner_module_reserve_image(owner,owner->module_runtime.stage.loader.module.bytes);
    if(!owner->module_private_tables || !owner->module_user_tables) {
        owner->module_prepare_result=FX_BUDGET;
        owner->module_prepare_stage=1u;
        return FX_BUDGET;
    }
    if(!owner->module_plan_raw || !owner->module_user_stack_storage || !owner->module_image_raw) {
        owner->module_prepare_result=FX_BUDGET;
        owner->module_prepare_stage=1u;
        return FX_BUDGET;
    }
    owner->module_image=owner->module_image_raw;
    owner->module_user_stack=owner->module_user_stack_storage;
    FxResult result=map_private(owner);
    owner->module_prepare_result=(uint32_t)result;
    owner->module_prepare_stage=2u;
    if(result==FX_OK) {
        owner->module_exec_context.operation_context=(uintptr_t)owner;
        result=fx_zf_exec_call(&owner->module_exec_context,owner->module_private_root,publish_operation);
        owner->module_exec_context.operation_context=0u;
    }
    owner->module_prepare_result=(uint32_t)result;
    owner->module_prepare_stage=3u;
    if(result!=FX_OK) {
        owner->module_private_root=0u;
        owner->module_user_root=0u;
        owner->module_runtime.execution=0;
        owner->module_runtime.exec_context=0;
        owner->module_runtime.user_api=0;
        owner->module_runtime.user_exchange=0;
        owner->module_runtime.private_root=0u;
        owner->module_runtime.user_root=0u;
        owner->module_runtime.user_stack=0u;
        owner->module_arena=(FxExecArena){0};
        owner->module_private_space=(FxZfAddressSpace){0};
        owner->module_user_space=(FxZfAddressSpace){0};
        memset(owner->module_private_tables,0,FX_ZF_TABLES*FX_ZF_PAGE);
        memset(owner->module_user_tables,0,FX_ZF_TABLES*FX_ZF_PAGE);
        owner->module_exec_context.operation_context=0u;
    }
    return result;
}

FxResult fx_zf_owner_module_publish(FxZfOwnerStorage *owner) {
    return owner&&owner->module_runtime.phase==FX_ZF_MODULE_BOUND&&owner->module_arena.published?FX_OK:FX_STATE;
}
