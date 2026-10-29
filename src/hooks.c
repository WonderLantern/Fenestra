#include "fenestra_hooks.h"
#include "fenestra_platform_native.h"
#include "fenestra_memory.h"
typedef uint64_t (*Native)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef struct {
    uint32_t before_count,after_count;uint64_t expected;
    /* instructions followed by equally many resolved u64 CALL targets */
    FxHookInstruction instructions[];
} Policy;
typedef struct {
    _Atomic uint32_t accept,fault;
    _Atomic uint32_t *storage,*state;_Atomic uint64_t *gate;
    FxHookRegistry *registry;uintptr_t entry;
    uint32_t uploaded,installed,pinned;
    FxPlatformOwner *owner;uint64_t token;
    _Atomic uintptr_t *slot;
    FxHookProgram *upload;Policy *policy;
    void *(*allocate)(uint32_t,uint32_t);void (*release)(void *);
    uint64_t failure_result,original;uint32_t policy_bytes;
    FxAllocationObserver observer;uint32_t retained_mask;
} Hook;
enum {RETAIN_HOOK=1u,RETAIN_UPLOAD=2u,RETAIN_POLICY=4u};
static void retain_allocation(Hook *s,uint32_t bit,uint32_t bytes) {
    if(!(s->retained_mask&bit)){fx_allocation_observe(s->observer,bytes,FX_ALLOC_PUBLISHED);s->retained_mask|=bit;}
}

/* Admission closes monotonically once a published gate is detached/faulted.
 * One atomic word orders policy entry against drain; a separate active counter
 * and accept flag would leave a check/increment race before policy reclamation. */
#define CLOSED (UINT64_C(1)<<63)
#define CALLS (CLOSED-1u)
static void close_gate(Hook *s){atomic_fetch_or_explicit(s->gate,CLOSED,memory_order_acq_rel);atomic_store(&s->accept,0u);}
static FxResult instructions(const FxHookInstruction *p,uint32_t count) {
    if(count>FX_HOOK_STEPS)return FX_BOUNDS;
    for(uint32_t i=0;i<count;++i) {
        const FxHookInstruction *q=&p[i];
        if(q->op<FX_VM_CONSTANT || q->op>FX_VM_RETURN || q->dst>15u || q->a>15u || q->b>15u)return FX_ARGUMENT;
        if((q->op==FX_VM_LOAD || q->op==FX_VM_STORE) &&
           q->b!=1u && q->b!=2u && q->b!=4u && q->b!=8u)return FX_ARGUMENT;
        if(q->op==FX_VM_SKIP_ZERO && q->value>count-i-1u)return FX_BOUNDS;
    }
    return FX_OK;
}
FxResult fx_hook_program_check(const FxHookProgram *p) {
    if(!p || p->abi!=1u || p->bytes!=sizeof(*p) || !p->table || !p->expected || (p->expected&3u) || (p->offset&7u))return FX_ARGUMENT;
    FxResult r=instructions(p->before,p->before_count);return r==FX_OK?instructions(p->after,p->after_count):r;
}
static FxResult execute(Hook *s,const FxHookInstruction *p,const uint64_t *targets,uint32_t count,uint64_t r[16]) {
    for(uint32_t i=0;i<count;++i) {
        const FxHookInstruction *q=&p[i];uint64_t a=r[q->a],b=r[q->b];
        switch(q->op) {
        case FX_VM_CONSTANT:r[q->dst]=q->value;break;
        case FX_VM_MOVE:r[q->dst]=a;break;
        case FX_VM_ADD:r[q->dst]=a+b;break;
        case FX_VM_SUBTRACT:r[q->dst]=a-b;break;
        case FX_VM_AND:r[q->dst]=a&b;break;
        case FX_VM_OR:r[q->dst]=a|b;break;
        case FX_VM_XOR:r[q->dst]=a^b;break;
        case FX_VM_SHIFT_LEFT:r[q->dst]=a<<(b&63u);break;
        case FX_VM_SHIFT_RIGHT:r[q->dst]=a>>(b&63u);break;
        case FX_VM_EQUAL:r[q->dst]=(uint64_t)(a==b);break;
        case FX_VM_SKIP_ZERO:if(!a)i+=(uint32_t)q->value;break;
        case FX_VM_CALL: {
            Native fn=(Native)(uintptr_t)targets[i];
            r[q->dst]=fn(r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7]);break;
        }
        case FX_VM_RETURN:return FX_EMPTY;
        case FX_VM_LOAD:case FX_VM_STORE: {
            if(a>UINT64_MAX-q->value || a+q->value>UINT64_MAX-q->b)return FX_BOUNDS;
            if(s->owner->platform->io.scalar) {
                uint64_t value=r[q->dst];
                FxResult result=s->owner->platform->io.scalar(s->owner->platform->io.context,a+q->value,q->b,&value,(uint32_t)(q->op==FX_VM_STORE));
                if(result!=FX_OK)return result;
                if(q->op==FX_VM_LOAD)r[q->dst]=value;
                break;
            }
            uint8_t data[8]={0};FxResult result;
            if(q->op==FX_VM_LOAD) {
                if(!s->owner->platform->io.read)return FX_CAPABILITY;
                result=s->owner->platform->io.read(s->owner->platform->io.context,a+q->value,data,q->b);
                if(result!=FX_OK)return result;
                r[q->dst]=0;for(uint32_t n=0;n<q->b;++n)r[q->dst]|=(uint64_t)data[n]<<(8u*n);
            } else {
                if(!s->owner->platform->io.write)return FX_CAPABILITY;
                for(uint32_t n=0;n<q->b;++n)data[n]=(uint8_t)(r[q->dst]>>(8u*n));
                result=s->owner->platform->io.write(s->owner->platform->io.context,a+q->value,data,q->b);
                if(result!=FX_OK)return result;
            }
            break;
        }
        default:return FX_INTEGRITY;
        }
    }
    return FX_OK;
}
uint64_t fx_hook_invoke(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h,FxHookCell *cell) {
    Hook *s=(void *)atomic_load_explicit(&cell->slot,memory_order_acquire);uint64_t r[16]={a,b,c,d,e,f,g,h,0};
    uint64_t entered=atomic_fetch_add_explicit(s->gate,1u,memory_order_acq_rel);
    uint32_t enabled=!(entered&CLOSED) && s->owner->phase==FX_OWNER_RUNNING;
    if(!enabled) {
        /* Late callers never dereference a drained policy allocation. */
        Native original=(Native)(uintptr_t)s->original;
        uint64_t result=original(a,b,c,d,e,f,g,h);
        atomic_fetch_sub_explicit(s->gate,1u,memory_order_release);return result;
    }
    Policy *p=s->policy;
    const FxHookInstruction *before=p->instructions,*after=before+p->before_count;
    const uint64_t *targets=(const uint64_t *)(const void *)(after+p->after_count);
    FxResult error=enabled?execute(s,before,targets,p->before_count,r):FX_OK;
    if(error==FX_EMPTY) {atomic_fetch_sub_explicit(s->gate,1u,memory_order_release);return r[8];}
    if(error!=FX_OK) {
        /* Notify before entering any potentially blocking native function.
         * A failed before-program never continues the original operation. */
        uint32_t zero=0;(void)atomic_compare_exchange_strong(&s->fault,&zero,(uint32_t)error);
        close_gate(s);zero=0;
        (void)atomic_compare_exchange_strong(&s->owner->first_error,&zero,(uint32_t)error);
        s->owner->phase=FX_OWNER_FAULT;fx_native_module_fault(s->owner,error);fx_platform_wake(s->owner);
        atomic_fetch_sub_explicit(s->gate,1u,memory_order_release);return s->failure_result;
    }
    Native original=(Native)(uintptr_t)s->original;
    r[8]=original(r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7]);
    uint64_t original_result=r[8];
    /* The original call can block while Finish/stop/Panic publishes a new
     * owner phase. Provider notification may still be waiting for binding_lock,
     * so accept alone does not authorize starting another policy program.
     * Already-entered native calls retain their storage and finish normally. */
    if(error==FX_OK && enabled && atomic_load(&s->accept) &&
       atomic_load_explicit(&s->owner->phase,memory_order_acquire)==FX_OWNER_RUNNING)
        error=execute(s,after,targets+p->before_count,p->after_count,r);
    if(error==FX_EMPTY)error=FX_OK;
    if(error!=FX_OK) {
        uint32_t zero=0;(void)atomic_compare_exchange_strong(&s->fault,&zero,(uint32_t)error);
        close_gate(s);zero=0;(void)atomic_compare_exchange_strong(&s->owner->first_error,&zero,(uint32_t)error);s->owner->phase=FX_OWNER_FAULT;
        r[8]=original_result;fx_native_module_fault(s->owner,error);fx_platform_wake(s->owner);
    }
    atomic_fetch_sub_explicit(s->gate,1u,memory_order_release);return r[8];
}
/* The extra C argument lives at [sp]; x0..x7 remain the native arguments.
 * x16/x17 are AAPCS64 scratch. The shared entry preserves LR and alignment. */
#if defined(__aarch64__)
extern void fx_hook_gate_enter(void);
__asm__(".text\n.align 4\n.global fx_hook_gate_enter\nfx_hook_gate_enter:\n"
        "stp x16,x30,[sp,#-16]!\nbl fx_hook_invoke\n"
        "ldr x30,[sp,#8]\nadd sp,sp,#16\nret\n");
void fx_hook_page_encode(uint8_t *page,FxHookCell *cells) {
    for(uint32_t i=0;i<FX_HOOK_GATES_PER_PAGE;++i) {
        /* BTI c; ldr x16,cell; ldr x17,entry; br x17; two literals. */
        const uint32_t code[4]={0xd503245fu,0x58000070u,0x58000091u,0xd61f0220u};
        uint64_t literals[2]={(uintptr_t)&cells[i],(uintptr_t)fx_hook_gate_enter};
        memcpy(page+i*32u,code,sizeof(code));memcpy(page+i*32u+16u,literals,sizeof(literals));
    }
}
#endif
static int reserve(FxHookRegistry *r,uint32_t bytes) {
    uint32_t old=atomic_load(&r->used);
    do {if(bytes>FX_HOOK_BUDGET-old)return 0;}
    while(!atomic_compare_exchange_weak(&r->used,&old,old+bytes));
    uint32_t peak=atomic_load(&r->peak);
    while(peak<old+bytes && !atomic_compare_exchange_weak(&r->peak,&peak,old+bytes)){}
    return 1;
}
static void *observed_allocate(Hook *s,uint32_t bytes,FxAllocationObserver observer) {
    if(!reserve(s->registry,bytes))return 0;
    void *p=s->allocate(bytes,0u);if(!p)atomic_fetch_sub(&s->registry->used,bytes);
    else fx_allocation_observe(observer,bytes,FX_ALLOC_REQUESTED);
    return p;
}
static void *hook_allocate(Hook *s,uint32_t bytes){return observed_allocate(s,bytes,s->observer);}
static void observed_release(Hook *s,void *p,uint32_t bytes,FxAllocationObserver observer,uint32_t published) {
    FxHookRegistry *registry=s->registry;void (*release)(void *)=s->release;
    release(p);atomic_fetch_sub(&registry->used,bytes);
    fx_allocation_observe(observer,bytes,published?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
}
static void hook_release(Hook *s,void *p,uint32_t bytes,uint32_t bit) {
    uint32_t retained=s->retained_mask&bit;s->retained_mask&=~bit;
    observed_release(s,p,bytes,s->observer,(uint32_t)(retained!=0u));
}
static FxHookCell *take_cell(FxHookRegistry *r,uintptr_t *entry) {
    for(FxHookPage *p=atomic_load_explicit(&r->pages,memory_order_acquire);p;p=p->next)if(p->ready)
        for(uint32_t i=0;i<FX_HOOK_GATES_PER_PAGE;++i) {
            uintptr_t zero=0u;
            if(atomic_compare_exchange_strong(&p->cells[i].slot,&zero,1u)) {
                atomic_store(&p->cells[i].state,1u);*entry=(uintptr_t)(p->code+i*32u);return &p->cells[i];
            }
        }
    return 0;
}
static FxResult acquire_cell(FxHookRegistry *r,FxPlatformOwner *owner,Hook *allocator,FxHookCell **out,uintptr_t *entry) {
    *out=take_cell(r,entry);if(*out)return FX_OK;
    uint32_t zero=0u;
    if(!atomic_compare_exchange_strong(&r->growing,&zero,1u))return FX_BUSY;
    *out=take_cell(r,entry);FxResult result=FX_OK;
    if(!*out) {
        uint32_t bytes=(uint32_t)sizeof(FxHookPage)+FX_HOOK_PAGE_BYTES+4095u;
        FxHookPage *p=observed_allocate(allocator,bytes,r->page_observer);
        if(!p)result=FX_BUDGET;
        else {
            memset(p,0,sizeof(*p));
            p->code=(uint8_t *)(((uintptr_t)(p+1)+4095u)&~(uintptr_t)4095u);
            uint32_t retained=0u;result=fx_native_hook_page(owner,p->code,p->cells,&retained);
            if(result==FX_OK || retained) {
                fx_allocation_observe(r->page_observer,bytes,FX_ALLOC_PUBLISHED);
                p->ready=(uint32_t)(result==FX_OK);
                p->next=atomic_load_explicit(&r->pages,memory_order_relaxed);
                if(result==FX_OK) {
                    atomic_store(&p->cells[0].slot,1u);atomic_store(&p->cells[0].state,1u);
                    *out=&p->cells[0];*entry=(uintptr_t)p->code;
                }
                atomic_store_explicit(&r->pages,p,memory_order_release);
            } else observed_release(allocator,p,bytes,r->page_observer,0u);
        }
    }
    atomic_store_explicit(&r->growing,0u,memory_order_release);return result;
}
static void drop_policy(Hook *s){if(s->policy){hook_release(s,s->policy,s->policy_bytes,RETAIN_POLICY);
    atomic_fetch_sub(s->storage,s->policy_bytes);}s->policy=0;s->policy_bytes=0u;}
static FxResult install(Hook *s) {
    const FxHookProgram *u=s->upload;
    FxResult r=fx_hook_program_check(u);if(r!=FX_OK)return r;
    uint32_t count=u->before_count+u->after_count;
    uint32_t bytes=(uint32_t)sizeof(Policy)+count*((uint32_t)sizeof(FxHookInstruction)+8u);
    Policy *p=hook_allocate(s,bytes);if(!p)return FX_BUDGET;
    memset(p,0,bytes);s->policy=p;s->policy_bytes=bytes;atomic_fetch_add(s->storage,bytes);
    p->before_count=u->before_count;p->after_count=u->after_count;p->expected=u->expected;
    memcpy(p->instructions,u->before,u->before_count*sizeof(FxHookInstruction));
    memcpy(p->instructions+u->before_count,u->after,u->after_count*sizeof(FxHookInstruction));
    uint64_t *targets=(uint64_t *)(void *)(p->instructions+count);
    for(uint32_t i=0;i<count;++i)if(p->instructions[i].op==FX_VM_CALL) {
        const FxNamedBinding *fn=fx_platform_open_binding(s->owner,p->instructions[i].value);
        if(!fn){drop_policy(s);return FX_STALE;}
        if(fn->kind!=FX_BIND_FUNCTION || fn->abi!=FX_CALL_U64_8){drop_policy(s);return FX_CAPABILITY;}
        targets[i]=fn->address;
    }
    const FxNamedBinding *b=fx_platform_open_binding(s->owner,u->table);
    if(!b){drop_policy(s);return FX_STALE;}
    if(b->kind!=FX_BIND_TABLE || !(b->flags&FX_BIND_WRITE) || u->offset>b->bytes ||
       8u>b->bytes-u->offset || ((b->address+u->offset)&7u)){drop_policy(s);return FX_BOUNDS;}
    s->slot=(_Atomic uintptr_t *)(uintptr_t)(b->address+u->offset);
    s->original=u->expected;
    uintptr_t expected=(uintptr_t)s->original;
    atomic_store_explicit(&s->accept,1u,memory_order_release);
    if(!atomic_compare_exchange_strong_explicit(s->slot,&expected,s->entry,memory_order_acq_rel,memory_order_acquire)) {
        atomic_store(&s->accept,0u);drop_policy(s);return FX_STALE;
    }
    s->pinned=1u;s->installed=1u;atomic_store(s->state,3u);
    retain_allocation(s,RETAIN_HOOK,(uint32_t)sizeof(*s));retain_allocation(s,RETAIN_POLICY,s->policy_bytes);
    hook_release(s,s->upload,(uint32_t)sizeof(FxHookProgram),RETAIN_UPLOAD);s->upload=0;atomic_fetch_sub(s->storage,(uint32_t)sizeof(FxHookProgram));return FX_OK;
}
FxResult fx_hook_resource(void *context,uint32_t op,const FxNamedBinding *binding,FxPlatformOwner *owner,uint64_t token,
    uint64_t *cookie,const FxServiceRequest *q,FxServiceReply *a) {
    (void)context;(void)binding;Hook *s=0;
    FxHookRegistry *registry=fx_native_hooks();if(!registry)return FX_CAPABILITY;
    if(op==FX_RESOURCE_ATTACH) {
        const FxNamedBinding *allocation=fx_platform_find(owner,"rtos.memory.allocate");
        const FxNamedBinding *deallocation=fx_platform_find(owner,"rtos.memory.release");
        if(!allocation || allocation->kind!=FX_BIND_FUNCTION || allocation->abi!=FX_CALL_U64_8 ||
           !deallocation || deallocation->kind!=FX_BIND_FUNCTION || deallocation->abi!=FX_CALL_U64_8)return FX_CAPABILITY;
        Hook allocator={.registry=registry,.allocate=(void *)(uintptr_t)allocation->address,.release=(void *)(uintptr_t)deallocation->address};
        if(registry->owner_account)allocator.observer=registry->owner_account(owner);
        uintptr_t entry=0u;FxHookCell *cell=0;
        FxResult result=acquire_cell(registry,owner,&allocator,&cell,&entry);if(result!=FX_OK)return result;
        s=hook_allocate(&allocator,(uint32_t)sizeof(*s));
        if(!s){atomic_store(&cell->state,0u);atomic_store(&cell->slot,0u);return FX_BUDGET;}
        memset(s,0,sizeof(*s));s->owner=owner;s->token=token;s->allocate=allocator.allocate;s->release=allocator.release;
        s->registry=registry;s->entry=entry;s->observer=allocator.observer;
        s->gate=&cell->gate;s->storage=&cell->storage;s->state=&cell->state;
        atomic_store(s->gate,0u);atomic_store(s->storage,(uint32_t)sizeof(*s));atomic_store(s->state,2u);
        atomic_store_explicit(&cell->slot,(uintptr_t)s,memory_order_release);
        *cookie=(uintptr_t)cell;return FX_OK;
    }
    if(!*cookie)return FX_STALE;
    FxHookCell *cell=(void *)(uintptr_t)*cookie;
    s=(void *)atomic_load_explicit(&cell->slot,memory_order_acquire);
    if((uintptr_t)s<=1u)return FX_STALE;
    if(s->owner!=owner || s->token!=token)return FX_STALE;
    if(op==FX_RESOURCE_PANIC){close_gate(s);retain_allocation(s,RETAIN_HOOK,(uint32_t)sizeof(*s));
        if(s->upload)retain_allocation(s,RETAIN_UPLOAD,(uint32_t)sizeof(*s->upload));
        if(s->policy)retain_allocation(s,RETAIN_POLICY,s->policy_bytes);
        return FX_OK;}
    if(op==FX_RESOURCE_DETACH) {
        if(!s->pinned) {
            /* Never published: no native caller can hold a trampoline or
             * pointer into this allocation. Unlike installed programs it
             * can be reclaimed after a failed/abandoned upload. */
            if(s->upload)hook_release(s,s->upload,(uint32_t)sizeof(FxHookProgram),RETAIN_UPLOAD);
            drop_policy(s);
            atomic_store(s->storage,0u);atomic_store(s->state,0u);
            atomic_store_explicit(&cell->slot,0u,memory_order_release);
            *cookie=0;hook_release(s,s,(uint32_t)sizeof(*s),RETAIN_HOOK);return FX_OK;
        }
        close_gate(s);
        if(s->installed) {
            uintptr_t expected=s->entry;
            if(!atomic_compare_exchange_strong(s->slot,&expected,(uintptr_t)s->original))return FX_BUSY;
            s->installed=0;atomic_store(s->state,4u);
        }
        if(atomic_load_explicit(s->gate,memory_order_acquire)&CALLS)return FX_BUSY;
        drop_policy(s);
        /* A native caller may have fetched the old trampoline immediately
         * before pointer restoration. Keep its stable gate/Hook/original pinned. Closed admission and a
         * drained entrant count allow reclaiming only the policy, not the gate. */
        *cookie=0;return FX_OK;
    }
    if(op!=FX_RESOURCE_CALL || !q || !a)return FX_ARGUMENT;
    if(q->arg[2]==FX_INTERCEPT_QUERY) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxHookInfo info={1u,sizeof(info),0xffu,0u};memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);
        a->value[0]=1u;a->value[1]=sizeof(FxHookProgram);a->value[2]=0u; /* Dynamic: use RESOURCES/CAPACITY. */a->value[3]=FX_HOOK_STEPS;return FX_OK;
    }
    if(q->arg[2]==FX_INTERCEPT_RESOURCES) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxHookResources info={.abi=1u,.bytes=sizeof(info)};
        for(FxHookPage *p=atomic_load_explicit(&registry->pages,memory_order_acquire);p;p=p->next)if(p->ready)
        for(uint32_t i=0;i<FX_HOOK_GATES_PER_PAGE;++i) {
            ++info.total;
            /* Read only permanent metadata. An unpublished Hook allocation
             * can disappear on another owner while this snapshot is copied. */
            uint32_t state=atomic_load_explicit(&p->cells[i].state,memory_order_acquire);
            if(!state)++info.free;
            else if(state==1u)++info.reserved;
            else if(state==2u)++info.unpublished;
            else if(state==3u)++info.installed;
            else ++info.retained;
            info.storage_bytes+=atomic_load(&p->cells[i].storage);
            info.active_calls+=atomic_load(&p->cells[i].gate)&CALLS;
        }
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(q->arg[2]==FX_INTERCEPT_CAPACITY) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxHookCapacity info={.abi=1u,.bytes=sizeof(info),.page_bytes=FX_HOOK_PAGE_BYTES,
            .gates_per_page=FX_HOOK_GATES_PER_PAGE,.budget=FX_HOOK_BUDGET,
            .used=atomic_load(&registry->used),.peak=atomic_load(&registry->peak)};
        for(FxHookPage *p=atomic_load_explicit(&registry->pages,memory_order_acquire);p;p=p->next)
            if(p->ready)++info.pages;else ++info.failed_pages;
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(q->arg[2]==FX_INTERCEPT_STATUS) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        a->value[0]=s->installed;a->value[1]=atomic_load(s->gate)&CALLS;a->value[2]=atomic_load(&s->fault);a->value[3]=atomic_load(&s->accept);return FX_OK;
    }
    if(s->pinned || atomic_load(&s->fault))return FX_STATE;
    if(q->arg[2]==FX_INTERCEPT_FAILURE_RETURN) {
        if(q->bytes!=sizeof(s->failure_result) || q->arg[3])return FX_ARGUMENT;
        memcpy(&s->failure_result,q->data,sizeof(s->failure_result));return FX_OK;
    }
    if(q->arg[2]==FX_INTERCEPT_BEGIN && !q->bytes && q->arg[3]==sizeof(FxHookProgram)) {
        if(!s->upload){s->upload=hook_allocate(s,(uint32_t)sizeof(*s->upload));
            if(s->upload)atomic_fetch_add(s->storage,(uint32_t)sizeof(*s->upload));}
        if(!s->upload)return FX_BUDGET;
        s->uploaded=0;memset(s->upload,0,sizeof(*s->upload));return FX_OK;
    }
    if(q->arg[2]==FX_INTERCEPT_APPEND && q->bytes && q->arg[3]==s->uploaded && s->upload && q->bytes<=sizeof(*s->upload)-s->uploaded) {
        memcpy((uint8_t *)s->upload+s->uploaded,q->data,q->bytes);s->uploaded+=q->bytes;a->value[0]=s->uploaded;return FX_OK;
    }
    if(q->arg[2]==FX_INTERCEPT_COMMIT && !q->bytes && !q->arg[3] && s->uploaded==sizeof(FxHookProgram))return install(s);
    return FX_ARGUMENT;
}
