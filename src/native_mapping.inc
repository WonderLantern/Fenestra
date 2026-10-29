/* Stable owner storage backs the interval list. A partial failure keeps its
 * claim until RECLAIM. The registry lock also serializes shared pool bitmaps
 * used by selected-root views; original writers still need caller exclusion. */
#include "fenestra_mmu_context.h"
static uintptr_t mapping_state(FxPlatformOwner *o) {
    const FxNamedBinding *b=fx_platform_find(o,"rtos.mmu.context");
    return b && b->kind==FX_BIND_TABLE && b->bytes>=0x39u?(uintptr_t)b->address:0;
}
static uint32_t mapping_private(uintptr_t state) {return *(volatile uint8_t *)(state+0x38u);}
static FxResult mapping_error(FxServiceReply *a,int32_t code) {
    a->value[3]=(uint64_t)(int64_t)code;return code?FX_IO:FX_OK;
}
typedef struct {FxPlatformOwner *owner;uintptr_t state,table;uint32_t root;} MappingBackend;
static int mapping_root_capable(FxPlatformOwner *o) {
    const FxNamedBinding *b=fx_platform_find(o,"rtos.mmu.context");
    return b && b->bytes>=sizeof(FxNativeMmuContext) &&
        native(o,"rtos.mmu.context.map") && native(o,"rtos.mmu.context.unmap") &&
        native(o,"rtos.mmu.context.protect") && native(o,"rtos.mmu.context.translate") &&
        native(o,"rtos.mmu.context.attributes") && native(o,"rtos.mmu.invalidate-all");
}
static FxResult mapping_backend(FxPlatformOwner *o,uint32_t root,MappingBackend *b) {
    uintptr_t state=mapping_state(o);if(!state)return FX_CAPABILITY;
    if(root>3u)return FX_ARGUMENT;
    *b=(MappingBackend){.owner=o,.state=state,.root=root};
    if(!root){memcpy(&b->table,(const void *)state,sizeof(b->table));return FX_OK;}
    if(!mapping_root_capable(o))return FX_CAPABILITY;
    const FxNativeMmuContext *source=(const void *)state;
    b->table=source->roots[root-1u];
    if(!b->table || (b->table&4095u))return FX_STATE;
    return FX_OK;
}
static void mapping_view(const MappingBackend *b,FxNativeMmuContext *view) {
    *view=*(const FxNativeMmuContext *)b->state;
    view->root=b->table;view->private_roots=0;view->pages=fx_native_mmu_view_pages(view);
    /* Original operations use root/pool pointers and physical_pool. The view
     * is not installed in TTBR; physical_root is never used for these calls. */
}
static void mapping_commit(const MappingBackend *b,const FxNativeMmuContext *view) {
    /* Allocations can survive an error. Copy every allocator bitmap byte,
     * never the selected root, mode flag or another task's context fields. */
    FxNativeMmuContext *source=(void *)b->state;
    memcpy(source->allocated,view->allocated,sizeof(source->allocated));
}
static int32_t mapping_translate(const MappingBackend *b,uint64_t va,uint64_t *pa) {
    if(!b->root)return ((int32_t (*)(uint64_t,uint64_t *))native(b->owner,"rtos.mmu.translate"))(va,pa);
    FxNativeMmuContext view;mapping_view(b,&view);
    return ((int32_t (*)(void *,uint64_t,uint64_t *,int32_t))native(b->owner,"rtos.mmu.context.translate"))(&view,va,pa,-1);
}
static int32_t mapping_attributes(const MappingBackend *b,uint64_t va,uint32_t *attributes) {
    if(!b->root)return ((int32_t (*)(uint64_t,uint32_t *))native(b->owner,"rtos.mmu.attributes"))(va,attributes);
    FxNativeMmuContext view;mapping_view(b,&view);
    return ((int32_t (*)(void *,uint64_t,uint32_t *,int32_t))native(b->owner,"rtos.mmu.context.attributes"))(&view,va,attributes,-1);
}
static uint32_t mapping_protection_attributes(uint32_t attributes) {
    attributes&=0x9fffffffu;
    /* Same normalization as the independently checked original public
     * PROTECT wrapper; context.protect is below that wrapper. */
    if(attributes&0x20u) {
        if(attributes&4u)attributes&=~0x20u;
        else if(attributes&8u)attributes&=~8u;
    }
    return attributes;
}
static int32_t mapping_mutate(const MappingBackend *b,uint32_t op,uint64_t va,uint64_t bytes,uint64_t pa,uint32_t attributes) {
    FxNativeMmuContext view;int32_t code;
    if(b->root || mapping_root_capable(b->owner)) {
        mapping_view(b,&view);
        if(op==FX_MAPPING_MAP)code=((int32_t (*)(void *,uint64_t,uint64_t,uint64_t,uint32_t,uint32_t,int32_t))native(b->owner,"rtos.mmu.context.map"))(&view,va,bytes,pa,attributes&0x9fffffffu,0u,-1);
        else if(op==FX_MAPPING_PROTECT)code=((int32_t (*)(void *,uint64_t,uint64_t,uint32_t,uint32_t,int32_t))native(b->owner,"rtos.mmu.context.protect"))(&view,va,bytes,mapping_protection_attributes(attributes),0u,-1);
        else code=((int32_t (*)(void *,uint64_t,uint64_t,uint32_t,int32_t))native(b->owner,"rtos.mmu.context.unmap"))(&view,va,bytes,0u,-1);
        mapping_commit(b,&view);return code;
    }
    if(op==FX_MAPPING_MAP)return ((int32_t (*)(uint64_t,uint64_t,uint64_t,uint32_t))native(b->owner,"rtos.mmu.map"))(va,bytes,pa,attributes&0x9fffffffu);
    if(op==FX_MAPPING_PROTECT)return ((int32_t (*)(uint64_t,uint64_t,uint32_t))native(b->owner,"rtos.mmu.protect"))(va,bytes,(attributes&0x9fffffffu)|0x20000000u);
    return ((int32_t (*)(uint64_t,uint64_t))native(b->owner,"rtos.mmu.unmap"))(va,bytes);
}
static void mapping_invalidate(const MappingBackend *b,uint64_t address,uint64_t bytes) {
    /* The stock range helper uses VAE1IS with ASID zero. Selected roots can
     * be used with other ASIDs: use the stock VMALLE1IS barrier sequence.
     * Neither operation supplies writer exclusion or device DMA coherence. */
    if(b->root)((void (*)(void))native(b->owner,"rtos.mmu.invalidate-all"))();
    else ((void (*)(uint64_t,uint64_t))native(b->owner,"rtos.mmu.invalidate"))(address,bytes);
}
static FxResult mapping_query_address(FxPlatformOwner *o,uint32_t root,const FxMappingRequest *r,FxServiceReply *a) {
    if(r->handle || r->physical || r->bytes || r->attributes)return FX_ARGUMENT;
    if(r->address>=UINT64_C(0x8000000000))return FX_BOUNDS;
    MappingBackend b;FxResult result=mapping_backend(o,root,&b);if(result!=FX_OK)return result;
    if(!root && (!native(o,"rtos.mmu.translate") || !native(o,"rtos.mmu.attributes")))return FX_CAPABILITY;
    uint64_t pa=0;int32_t code=mapping_translate(&b,r->address,&pa);if(code)return mapping_error(a,code);
    uint32_t attributes=0;code=mapping_attributes(&b,r->address,&attributes);
    if(!code){a->value[0]=pa;a->value[1]=attributes;}return mapping_error(a,code);
}
static FxResult mapping_hold(Resources *s,Mapping *m) {
    m->released=1u;fx_capacity_retain(&m->claim);
    return retain_object(&s->mapping,m->token,s->mappings,m->bytes);
}
static FxResult mapping_locked(FxPlatformOwner *o,Resources *s,uint32_t op,uint32_t root,uint64_t writer_lock,const FxMappingRequest *r,FxServiceReply *a,FxNativeMappingRegistry *registry,Mapping *reserved,uint64_t handle,Mapping **completed) {
    uintptr_t invalidate=native(o,"rtos.mmu.invalidate");if(!invalidate || !native(o,"rtos.mmu.translate"))return FX_CAPABILITY;
    MappingBackend b;Mapping *m=0;FxResult result;
    if(op==FX_MAPPING_MAP) {
        if(r->handle || !r->bytes || r->bytes>1024u*1024u || ((r->address|r->physical|r->bytes)&4095u) ||
           r->address>UINT64_C(0x8000000000)-r->bytes || r->physical>UINT64_C(0x1000000000)-r->bytes)return FX_BOUNDS;
        result=mapping_backend(o,root,&b);if(result!=FX_OK)return result;
        if(!root && mapping_private(b.state))return FX_STATE;
        if(!root && !native(o,"rtos.mmu.map"))return FX_CAPABILITY;
        if(!reserved || !handle)return FX_STATE;
        for(m=registry->claims;m;m=m->next)
            if(m->table==b.table && r->address<m->address+m->bytes && m->address<r->address+r->bytes)return FX_BUSY;
        for(uint64_t offset=0;offset<r->bytes;offset+=4096u) {
            uint64_t pa=0;int32_t code=mapping_translate(&b,r->address+offset,&pa);
            if(!code)return FX_BUSY;
            if(code!=-26)return mapping_error(a,code);
        }
        m=reserved;
        *m=(Mapping){.next=registry->claims,.token=handle,.address=r->address,.physical=r->physical,
                     .bytes=r->bytes,.table=b.table,.root=root,.writer_lock=writer_lock};
        registry->claims=m;
        m->claim=fx_capacity_acquire(fx_capacity_find(o->capacity,"mmu.claimed-bytes"),m->bytes);
        acquired(s->mappings,m->bytes);
        int32_t code=mapping_mutate(&b,op,r->address,r->bytes,r->physical,r->attributes);
        mapping_invalidate(&b,r->address,r->bytes);
        if(code){fx_capacity_failed(m->claim.domain,(uint32_t)code);(void)mapping_hold(s,m);}
        a->value[0]=m->token;a->value[1]=m->address;a->value[2]=m->bytes;
        return mapping_error(a,code);
    }
    m=fx_slot_pool_get(&s->mapping,r->handle);
    if(!m)return FX_STALE;
    if(r->address || r->physical || r->bytes || (op!=FX_MAPPING_PROTECT && r->attributes))return FX_ARGUMENT;
    if(op==FX_MAPPING_RELEASE)return mapping_hold(s,m);
    if(op!=FX_MAPPING_PROTECT && op!=FX_MAPPING_RECLAIM)return FX_ARGUMENT;
    if(m->released && op==FX_MAPPING_PROTECT)return FX_STALE;
    result=mapping_backend(o,m->root,&b);if(result!=FX_OK)return result;
    if(b.table!=m->table)return FX_INTEGRITY;
    if(!b.root && mapping_private(b.state))return FX_STATE;
    if(!b.root && !native(o,op==FX_MAPPING_PROTECT?"rtos.mmu.protect":"rtos.mmu.unmap"))return FX_CAPABILITY;
    for(uint64_t offset=0;offset<m->bytes;offset+=4096u) {
        uint64_t pa=0;int32_t code=mapping_translate(&b,m->address+offset,&pa);
        if(!code && pa!=m->physical+offset)return FX_INTEGRITY;
        if(code && (code!=-26 || op==FX_MAPPING_PROTECT))return mapping_error(a,code);
    }
    int32_t code=0;
    if(op==FX_MAPPING_PROTECT)code=mapping_mutate(&b,op,m->address,m->bytes,0,r->attributes);
    else for(uint64_t offset=0;offset<m->bytes;offset+=4096u) {
        uint64_t pa=0;int32_t query=mapping_translate(&b,m->address+offset,&pa);
        if(query==-26)continue;
        if(query){code=query;break;}
        code=mapping_mutate(&b,op,m->address+offset,4096u,0,0);if(code)break;
    }
    mapping_invalidate(&b,m->address,m->bytes);
    if(!code && op==FX_MAPPING_RECLAIM)*completed=m;
    if(code){fx_capacity_release(&m->claim,(uint32_t)code);(void)mapping_hold(s,m);}
    return mapping_error(a,code);
}
static FxResult mapping_call(FxPlatformOwner *o,Resources *s,const FxServiceRequest *q,FxServiceReply *a) {
    uintptr_t state=mapping_state(o);if(!state)return FX_CAPABILITY;
    uint64_t op=q->arg[2],writer_lock=0;uint32_t root=0;
    if(op==FX_MAPPING_QUERY) {
        if(q->bytes || q->arg[3])return FX_ARGUMENT;
        FxMappingInfo info={.abi=1u,.bytes=sizeof(info),.operations=0x7eu,.capacity=fx_slot_pool_limit(&s->mapping),
            .max_bytes=1024u*1024u,.page_bytes=4096u,.private_roots=mapping_private(state)};
        if(mapping_root_capable(o))info.operations|=(1u<<FX_MAPPING_MAP_ROOT)|(1u<<FX_MAPPING_TRANSLATE_ROOT);
        if(native(o,"rtos.spinlock.try-lock") && native(o,"rtos.spinlock.unlock"))
            info.operations|=(1u<<FX_MAPPING_MAP_LOCKED)|(1u<<FX_MAPPING_TRANSLATE_LOCKED);
        info.retained=s->mapping.retained;info.active=s->mapping.used-s->mapping.retained;
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    FxMappingRequest r;
    if(op==FX_MAPPING_MAP_LOCKED || op==FX_MAPPING_TRANSLATE_LOCKED) {
        if(q->bytes!=sizeof(FxMappingLockedRequest) || q->arg[3])return FX_ARGUMENT;
        FxMappingLockedRequest selected;memcpy(&selected,q->data,sizeof(selected));
        if((selected.root>2u && selected.root!=UINT32_MAX) || selected.reserved ||
           !selected.writer_lock || (selected.writer_lock&7u))return FX_ARGUMENT;
        root=selected.root==UINT32_MAX?0u:selected.root+1u;r=selected.mapping;writer_lock=selected.writer_lock;
        op=op==FX_MAPPING_MAP_LOCKED?FX_MAPPING_MAP:FX_MAPPING_TRANSLATE;
    } else if(op==FX_MAPPING_MAP_ROOT || op==FX_MAPPING_TRANSLATE_ROOT) {
        if(q->bytes!=sizeof(FxMappingRootRequest) || q->arg[3])return FX_ARGUMENT;
        FxMappingRootRequest selected;memcpy(&selected,q->data,sizeof(selected));
        if(selected.root>2u || selected.reserved)return FX_ARGUMENT;
        root=selected.root+1u;r=selected.mapping;
        op=op==FX_MAPPING_MAP_ROOT?FX_MAPPING_MAP:FX_MAPPING_TRANSLATE;
    } else {
        if(q->bytes!=sizeof(r) || q->arg[3])return FX_ARGUMENT;
        memcpy(&r,q->data,sizeof(r));
    }
    if(r.reserved || op<FX_MAPPING_MAP || op>FX_MAPPING_RECLAIM)return FX_ARGUMENT;
    if(op==FX_MAPPING_TRANSLATE && !root && !writer_lock)return mapping_query_address(o,0,&r,a);
    uint32_t zero=0;FxNativeMappingRegistry *registry=fx_native_mappings();if(!registry)return FX_STATE;
    /* Heap growth must precede both native writer exclusion and the global
     * mapping claim lock. Failed prepublication attempts return this slot. */
    if(atomic_load_explicit(&registry->lock,memory_order_acquire))return FX_BUSY;
    Mapping *reserved=0;uint64_t handle=0u;
    if(op==FX_MAPPING_MAP) {
        void *item=0;FxResult prepared=resource_slot(o,&s->mapping,&item,&handle);if(prepared!=FX_OK)return prepared;reserved=item;
    }
    if(!atomic_compare_exchange_strong_explicit(&registry->lock,&zero,1u,memory_order_acquire,memory_order_relaxed)) {
        if(reserved)(void)fx_slot_pool_release(&s->mapping,handle);
        return FX_BUSY;
    }
    if(op==FX_MAPPING_PROTECT || op==FX_MAPPING_RECLAIM) {
        Mapping *m=fx_slot_pool_get(&s->mapping,r.handle);if(m)writer_lock=m->writer_lock;
    }
    FxMappingLockStatus status={.result=FX_STATE};FxResult result=FX_OK;
    Mapping *completed=0;FxCapacityClaim released_claim={0};
    uintptr_t acquire=0,release=0;
    if(writer_lock) {
        acquire=native(o,"rtos.spinlock.try-lock");release=native(o,"rtos.spinlock.unlock");
        if(!acquire || !release) {
            atomic_store_explicit(&registry->lock,0u,memory_order_release);
            if(reserved)(void)fx_slot_pool_release(&s->mapping,handle);
            return FX_CAPABILITY;
        }
        status.acquire=((int32_t (*)(uintptr_t))acquire)((uintptr_t)writer_lock);
    }
    if(status.acquire) {
        a->value[3]=(uint64_t)(int64_t)status.acquire;result=status.acquire==-1009?FX_BUSY:FX_IO;
    } else {
        result=op==FX_MAPPING_TRANSLATE?mapping_query_address(o,root,&r,a):
            mapping_locked(o,s,(uint32_t)op,root,writer_lock,&r,a,registry,reserved,handle,&completed);
        if(writer_lock) {
            status.result=(uint32_t)result;
            status.release=((int32_t (*)(uintptr_t))release)((uintptr_t)writer_lock);
            if(status.release) {
                if(!a->value[3])a->value[3]=(uint64_t)(int64_t)status.release;
                result=FX_IO;
                Mapping *uncertain=completed?completed:reserved && reserved->token?reserved:fx_slot_pool_get(&s->mapping,r.handle);
                if(uncertain){fx_capacity_release(&uncertain->claim,(uint32_t)status.release);(void)mapping_hold(s,uncertain);}
            }
        }
    }
    if(writer_lock){memcpy(a->data,&status,sizeof(status));a->bytes=sizeof(status);}
    /* Keep the identity and interval claim until the native writer unlock
     * has acknowledged completion. Never notify an RTOS waiter under either
     * native CPU exclusion or this registry's lock. */
    if(completed && !status.release) {
        Mapping **link=&registry->claims;while(*link && *link!=completed)link=&(*link)->next;
        if(*link)*link=completed->next;
        released_claim=completed->claim;
        (void)release_object(&s->mapping,completed->token,s->mappings,completed->bytes);
    }
    atomic_store_explicit(&registry->lock,0u,memory_order_release);
    if(reserved && !reserved->token)(void)fx_slot_pool_release(&s->mapping,handle);
    fx_capacity_release(&released_claim,0u);
    return result;
}
