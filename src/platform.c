#include "fenestra_memory.h"
#include "fenestra_platform.h"
_Static_assert(sizeof(FxUiEvent)==24u,"UI event wire ABI remains unchanged");
_Static_assert(sizeof(FxUiTextEvent)==152u && sizeof(FxUiTextEvent)<=FX_SERVICE_BYTES,"UI text wire ABI");
_Static_assert(sizeof(FxUiStatus)==40u,"UI submission status wire ABI");

FxResult fx_platform_bind_module(FxPlatformOwner *o,const FxModule *m,void *image,uint32_t capacity) {
    if(!o || !o->platform || !m || !image || !m->prepared || !m->bytes || m->bytes>capacity ||
       m->bytes>FX_MODULE_MEMORY_LIMIT || !m->segment_count || m->segment_count>FX_MODULE_SEGMENTS ||
       (uintptr_t)image>UINTPTR_MAX-m->bytes)return FX_ARGUMENT;
    if(o->module_image || o->phase!=FX_OWNER_RUNNING)return FX_STATE;
    for(uint32_t i=0;i<m->segment_count;++i) {
        const FxSegment *s=&m->segments[i];
        if(!s->memory_bytes || s->image_offset>=m->bytes || s->memory_bytes>m->bytes-s->image_offset ||
           !(s->flags&FX_READ) || (s->flags&~7u) || (s->flags&(FX_WRITE|FX_EXECUTE))==(FX_WRITE|FX_EXECUTE))return FX_BOUNDS;
        for(uint32_t j=0;j<i;++j) {
            const FxSegment *p=&m->segments[j];
            if(s->image_offset<p->image_offset+p->memory_bytes && p->image_offset<s->image_offset+s->memory_bytes)return FX_BOUNDS;
        }
    }
    memcpy(o->module_segments,m->segments,m->segment_count*sizeof(FxSegment));
    o->module_bytes=m->bytes;o->module_segment_count=m->segment_count;o->module_image=image;
    return FX_OK;
}
FxResult fx_platform_module_transfer(FxPlatformOwner *o,uint64_t address,void *data,uint32_t bytes,uint32_t write_to_module) {
    if(!o || !data || !bytes || write_to_module>1u)return FX_ARGUMENT;
    if(!o->module_image || o->phase!=FX_OWNER_RUNNING)return FX_STATE;
    uint64_t base=(uintptr_t)o->module_image;
    if(address<base || address-base>=o->module_bytes || bytes>o->module_bytes-(address-base))return FX_BOUNDS;
    uint32_t at=(uint32_t)(address-base),remaining=bytes;
    /* A validated range may span adjacent permitted segments, but never
     * image holes, text writes or another owner's allocation. No partial IO. */
    while(remaining) {
        const FxSegment *found=0;
        for(uint32_t i=0;i<o->module_segment_count;++i) {
            const FxSegment *s=&o->module_segments[i];
            if(at>=s->image_offset && at-s->image_offset<s->memory_bytes){found=s;break;}
        }
        if(!found || (write_to_module && (!(found->flags&FX_WRITE) || (found->flags&FX_EXECUTE))))return FX_BOUNDS;
        uint32_t step=found->memory_bytes-(at-found->image_offset);if(step>remaining)step=remaining;
        at+=step;remaining-=step;
    }
    uint8_t *p=o->module_image+(uint32_t)(address-base);
    if(write_to_module)memmove(p,data,bytes);else memmove(data,p,bytes);
    return FX_OK;
}

static int text(const char *s, uint32_t bytes, int empty) {
    uint32_t n = 0;
    while (n < bytes && s[n])
        ++n;
    if (n == bytes || (!empty && !n))
        return 0;
    for (uint32_t i = 0; i < n;) {
        uint32_t c = (uint8_t)s[i++], extra = 0, minimum = 0;
        if (c < 128u)
            continue;
        if (c >= 0xc2u && c < 0xe0u) {
            c &= 31u;
            extra = 1;
            minimum = 128u;
        } else if (c >= 0xe0u && c < 0xf0u) {
            c &= 15u;
            extra = 2;
            minimum = 2048u;
        } else if (c >= 0xf0u && c < 0xf5u) {
            c &= 7u;
            extra = 3;
            minimum = 65536u;
        } else
            return 0;
        if (extra > n - i)
            return 0;
        for (uint32_t j = 0; j < extra; ++j) {
            uint32_t b = (uint8_t)s[i++];
            if ((b & 192u) != 128u)
                return 0;
            c = (c << 6) | (b & 63u);
        }
        if (c < minimum || c > 0x10ffffu || (c >= 0xd800u && c <= 0xdfffu))
            return 0;
    }
    return 1;
}
static int equal(const char *a, const char *b) {
    for (uint32_t i = 0; i < 64u; ++i) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 0;
}
#include "ui_controls.inc"
/* Read-only host observation of our stable control storage. Never consumed
 * by firmware runtime paths, and never exposes module-owned pointers. */
const uint32_t fx_ui_controls_metadata[15]={0x46585543u,1u,sizeof(FxPlatformOwner),
    offsetof(FxPlatformOwner,ui_controls),offsetof(FxPlatformOwner,control_count),
    offsetof(FxPlatformOwner,controls),sizeof(FxUiControl),FX_UI_CONTROLS,sizeof(FxUiControlStore),
    offsetof(FxUiControlStore,blocks),offsetof(FxUiControlStore,directory),UI_CONTROL_CHUNK,
    sizeof(UiControlNode),offsetof(UiControlNode,control),FX_UI_CONTROL_BUDGET};

static int lock(FxPlatformOwner *s) {
    uint32_t z = 0u;
    return atomic_compare_exchange_strong_explicit(&s->ui_lock, &z, 1u, memory_order_acquire,
                                                   memory_order_relaxed);
}
static void unlock(FxPlatformOwner *s) {
    uint32_t revision=atomic_load_explicit(&s->revision,memory_order_relaxed);
    if(s->ui_presence_revision!=revision) {
        uint32_t visible=0u;
        for(uint32_t i=0u;i<s->control_count;++i) {
            uint32_t flags=ui_control_at(s,i)->flags;
            if((flags&(FX_UI_ROOT|FX_UI_RETIRED))==FX_UI_ROOT) {
                visible=(flags&FX_UI_VISIBLE)!=0u;break;
            }
            if((flags&(FX_UI_VISIBLE|FX_UI_RETIRED))==FX_UI_VISIBLE)visible=1u;
        }
        s->ui_presence_revision=revision;
        atomic_store_explicit(&s->ui_presence,s->control_count|(visible<<31),memory_order_release);
    }
    atomic_store_explicit(&s->ui_lock,0u,memory_order_release);
}
static int bind_lock(FxPlatformOwner *s) {
    uint32_t z = 0u;
    return atomic_compare_exchange_strong_explicit(&s->binding_lock, &z, 1u, memory_order_acquire,
                                                   memory_order_relaxed);
}
static void bind_unlock(FxPlatformOwner *s) {
    atomic_store_explicit(&s->binding_lock, 0u, memory_order_release);
}
static FxBindingLease *lease(FxPlatformOwner *s, uint64_t token) {
    return fx_binding_store_lease(&s->bindings,token);
}
static FxUiControl *control_any(FxPlatformOwner *s, uint32_t id) {
    uint32_t i=ui_control_index(s,id);
    return i==UINT32_MAX?0:ui_control_at(s,i);
}
static FxUiControl *control(FxPlatformOwner *s,uint32_t id) {
    FxUiControl *c=control_any(s,id);return c && !(c->flags&FX_UI_RETIRED)?c:0;
}
static FxResult absent_control(FxPlatformOwner *s,uint32_t id) {
    return control_any(s,id)?FX_STALE:FX_EMPTY;
}
FxResult fx_ui_page_index(FxPlatformOwner *s,uint32_t id,uint32_t *index) {
    if(!s || !index || !id)return FX_ARGUMENT;
    if(!lock(s))return FX_BUSY;
    FxResult result=FX_CAPABILITY;
    uint32_t i=ui_control_index(s,id);
    if(i!=UINT32_MAX) {
        const FxUiControl *c=ui_control_at(s,i);
        if(c->flags&FX_UI_RETIRED)result=FX_STALE;
        else if(c->kind==FX_UI_PAGE && (c->flags&FX_UI_VISIBLE)){*index=i;result=FX_OK;}
    }
    unlock(s);return result;
}
FxResult fx_ui_page_slot(FxPlatformOwner *s,uint32_t id,uint32_t *slot) {
    if(!s || !slot || !id)return FX_ARGUMENT;
    if(!lock(s))return FX_BUSY;
    FxUiControl *c=control(s,id);FxResult r=FX_CAPABILITY;
    if(c && c->kind==FX_UI_PAGE && (c->flags&FX_UI_VISIBLE)) {
        *slot=(c->flags&FX_UI_ROOT)?0u:ui_control_index(s,c->id)+1u;r=FX_OK;
    }
    if(!c && control_any(s,id))r=FX_STALE;
    unlock(s);return r;
}
FxResult fx_ui_retired_parent_slot(FxPlatformOwner *s,uint32_t id,uint32_t *slot) {
    if(!s || !slot)return FX_ARGUMENT;
    if(!id)return FX_EMPTY;
    if(!lock(s))return FX_BUSY;
    FxUiControl *c=control_any(s,id);FxResult r=FX_EMPTY;
    if(c && (c->flags&FX_UI_RETIRED)) {
        r=FX_INTEGRITY;
        for(uint32_t i=0;i<=s->control_count;++i) {
            if(!c->parent){*slot=UINT32_MAX;r=FX_OK;break;}
            c=control_any(s,c->parent);
            if(!c || c->kind!=FX_UI_PAGE)break;
            if(!(c->flags&FX_UI_RETIRED)) {
                *slot=(c->flags&FX_UI_ROOT)?0u:ui_control_index(s,c->id)+1u;r=FX_OK;break;
            }
        }
    }
    unlock(s);return r;
}
static uintptr_t binding_allocator(const FxPlatformOwner *s,const char *name) {
    const FxNamedBinding *b=fx_platform_find(s,name);
    return b && b->kind==FX_BIND_FUNCTION && b->abi==FX_CALL_U64_8?(uintptr_t)b->address:0u;
}
static void *binding_allocate(void *context,uint32_t bytes) {
    uintptr_t fn=binding_allocator(context,"rtos.memory.allocate");
    return ((void *(*)(uint32_t,uint32_t))fn)(bytes,0u);
}
static void binding_release(void *context,void *p) {
    ((void (*)(void *))binding_allocator(context,"rtos.memory.release"))(p);
}
static void binding_memory(FxPlatformOwner *s) {
    if(!s->bindings.memory.allocate && binding_allocator(s,"rtos.memory.allocate") && binding_allocator(s,"rtos.memory.release"))
        s->bindings.memory=(FxIndexMemory){s,binding_allocate,binding_release};
}
static uint32_t ui_event_capacity(const FxPlatformOwner *s) {
    uint32_t n=atomic_load_explicit(&s->ui_event_capacity,memory_order_acquire);return n?n:FX_UI_EVENTS;
}
static FxUiEvent *ui_event(FxPlatformOwner *s,uint32_t index) {
    return s->ui_event_buffer?&s->ui_event_buffer[index].event:&s->events[index];
}
static char *ui_event_text(FxPlatformOwner *s,uint32_t index) {
    return s->ui_event_buffer?s->ui_event_buffer[index].text:s->event_text[index];
}
FxResult fx_platform_init(FxPlatformOwner *s, const FxPlatform *p, uint32_t owner, uint32_t generation,
                          uint32_t caps) {
    if (!s || !p || p->abi != FX_PLATFORM_ABI || !p->bindings || !p->count || p->count > 256u || !owner ||
        owner > 65535u || !generation)
        return FX_ARGUMENT;
    if (s->platform)
        return FX_BUSY;
    for (uint32_t i = 0; i < p->count; ++i) {
        const FxNamedBinding *b = &p->bindings[i];
        if (!b->name || !text(b->name, 64u, 0) || b->kind < 1u || b->kind > 4u || !b->version ||
            !b->address || b->address > UINT64_MAX - b->bytes)
            return FX_INTEGRITY;
        for (uint32_t j = 0; j < i; ++j)
            if (equal(b->name, p->bindings[j].name))
                return FX_INTEGRITY;
    }
    memset(s, 0, sizeof(*s));
    s->platform = p;
    s->owner = owner;
    s->generation = generation;
    s->capabilities = caps;
    s->kind = FX_MODULE_EXTENSION;
    s->phase = FX_OWNER_RUNNING;
    fx_binding_store_init(&s->bindings,(FxIndexMemory){0});binding_memory(s);
    s->ui_event_memory=s->bindings.memory;
    s->ui_control_memory=s->bindings.memory;s->ui_control_budget=FX_UI_CONTROL_BUDGET;
    atomic_init(&s->ui_event_capacity,FX_UI_EVENTS);
    return FX_OK;
}
const FxNamedBinding *fx_platform_binding(const FxPlatformOwner *s,uint32_t index) {
    if(!s || !s->platform)return 0;
    if(index<s->platform->count)return &s->platform->bindings[index];
    index-=s->platform->count;
    return fx_binding_store_declaration(&s->bindings,index);
}
FxResult fx_platform_retain_context(FxPlatformOwner *s,FxProviderContext *node) {
    if(!s || !s->platform || !node || !node->context || !node->name || !text(node->name,64u,0))return FX_ARGUMENT;
    if(s->phase!=FX_OWNER_RUNNING)return FX_STATE;
    FxProviderContext *head=atomic_load_explicit(&s->provider_contexts,memory_order_acquire);
    for(FxProviderContext *at=head;at;at=at->next)if(at==node || at->context==node->context)return FX_STATE;
    node->next=head;
    return atomic_compare_exchange_strong_explicit(&s->provider_contexts,&head,node,memory_order_release,memory_order_relaxed)?FX_OK:FX_BUSY;
}
static uint32_t binding_total(const FxPlatformOwner *s) {return s->platform->count+atomic_load_explicit(&s->bindings.declarations,memory_order_acquire);}
static const FxNamedBinding *find_binding(const FxPlatformOwner *s,const char *name,uint32_t *index) {
    for(uint32_t i=0;i<s->platform->count;++i) {
        const FxNamedBinding *b=&s->platform->bindings[i];if(equal(b->name,name)){if(index)*index=i;return b;}
    }
    uint32_t declared=0u;const FxNamedBinding *b=fx_binding_store_find(&s->bindings,name,&declared,0);
    if(b && index)*index=s->platform->count+declared;
    return b;
}
const FxNamedBinding *fx_platform_find(const FxPlatformOwner *s,const char *name) {
    return s && s->platform && name?find_binding(s,name,0):0;
}
const FxNamedBinding *fx_platform_open_binding(const FxPlatformOwner *s,uint64_t token) {
    FxBindingLease *l=fx_binding_store_lease(&s->bindings,token);
    return l && l->attached==1u?fx_platform_binding(s,l->index):0;
}
static FxResult declare_binding(FxPlatformOwner *s,const FxServiceRequest *q,FxServiceReply *a) {
    if(q->bytes!=sizeof(FxBindingInfo) || q->arg[1] || q->arg[2] || q->arg[3])return FX_ARGUMENT;
    if(!(s->capabilities&FX_SDK_NATIVE))return FX_CAPABILITY;
    FxBindingInfo d;memcpy(&d,q->data,sizeof(d));
    if(!text(d.name,64u,0) || !d.version || d.reserved || d.available || !d.address ||
       d.flags&~7u || d.capabilities&~s->capabilities || d.address>UINT64_MAX-d.bytes)return FX_ARGUMENT;
    if(d.kind==FX_BIND_FUNCTION) {
        if((d.abi!=FX_CALL_U64_8 && d.abi!=FX_CALL_AAPCS64) || d.bytes || d.flags&~(uint32_t)FX_BIND_ESCAPE || (d.address&3u))return FX_ARGUMENT;
    } else if(d.kind==FX_BIND_TABLE) {
        if(d.abi!=FX_CALL_BYTES || !d.bytes || !(d.flags&(FX_BIND_READ|FX_BIND_WRITE)))return FX_ARGUMENT;
    } else return FX_CAPABILITY; /* A declaration is not a native callback installer. */
    if(fx_platform_find(s,d.name))return FX_STATE;
    FxNamedBinding declared={.name=d.name,.kind=d.kind,.abi=d.abi,.version=d.version,
        .flags=d.flags,.capabilities=d.capabilities,.bytes=d.bytes,.address=d.address};
    memcpy(declared.original,d.original,16u);uint32_t index=0u;binding_memory(s);
    FxResult r=fx_binding_store_declare(&s->bindings,&declared,&index);
    if(r==FX_OK)a->value[0]=s->platform->count+index;
    return r;
}
FxResult fx_platform_prepare_native(FxPlatformOwner *s,uint64_t handle,const void *data,uint32_t bytes,FxNamedBinding *out) {
    if(!s || !data || !out)return FX_ARGUMENT;
    FxBindingLease *l=lease(s,handle);
    if(!l || l->attached!=1u)return FX_STALE;
    const FxNamedBinding *b=fx_platform_binding(s,l->index);
    if(!b || b->kind!=FX_BIND_FUNCTION)return FX_CAPABILITY;
    if(b->abi==FX_CALL_U64_8) {
        if(bytes!=64u || !s->platform->io.call)return FX_CAPABILITY;
    } else if(b->abi==FX_CALL_AAPCS64) {
        if(bytes!=sizeof(FxNativeCallFrame) || !s->platform->io.call_frame)return FX_CAPABILITY;
        FxNativeCallFrame frame;memcpy(&frame,data,sizeof(frame));
        FxResult r=fx_native_call_frame_check(&frame);if(r!=FX_OK)return r;
    } else return FX_CAPABILITY;
    *out=*b;return FX_OK;
}
FxResult fx_platform_invoke_native(FxPlatformOwner *s,const FxNamedBinding *b,void *data,uint32_t bytes,uint64_t result[2]) {
    if(!s || !b || !data || !result || b->kind!=FX_BIND_FUNCTION)return FX_ARGUMENT;
    result[0]=result[1]=0u;
    if(b->abi==FX_CALL_AAPCS64) {
        if(bytes!=sizeof(FxNativeCallFrame) || !s->platform->io.call_frame)return FX_CAPABILITY;
        FxNativeCallFrame *frame=data;FxResult r=fx_native_call_frame_check(frame);if(r!=FX_OK)return r;
        memset(frame->result,0,sizeof(frame->result));memset(frame->vector_result,0,sizeof(frame->vector_result));
        r=s->platform->io.call_frame(s->platform->io.context,b,frame);
        if(r==FX_OK){result[0]=frame->result[0];result[1]=frame->result[1];}return r;
    }
    if(b->abi!=FX_CALL_U64_8 || bytes!=64u || !s->platform->io.call)return FX_CAPABILITY;
    return s->platform->io.call(s->platform->io.context,b,data,&result[0]);
}
static FxResult binding(FxPlatformOwner *s, const FxServiceRequest *q, FxServiceReply *a) {
    uint32_t op = (uint32_t)q->arg[0];
    if(op==FX_BIND_DECLARE)return declare_binding(s,q,a);
    if(op==FX_BIND_CAPACITY) {
        if(q->bytes || q->arg[1] || q->arg[2] || q->arg[3])return FX_ARGUMENT;
        binding_memory(s);FxBindingStore *store=&s->bindings;
        FxBindingCapacity info={.abi=1u,.bytes=sizeof(info),.lease_slots=store->lease_capacity,.active_leases=store->active,
            .declaration_slots=FX_BINDING_INLINE_DECLARATIONS+store->declaration_blocks*FX_BINDING_CHUNK,
            .declarations=atomic_load(&store->declarations),.budget=store->budget,.used=store->used,.peak=store->peak,
            .identities_remaining=(uint32_t)(FX_BINDING_IDENTITIES-s->next_token),.inline_leases=FX_BINDING_INLINE_LEASES,
            .inline_declarations=FX_BINDING_INLINE_DECLARATIONS,.profile_bindings=s->platform->count,.allocator_available=(uint32_t)(store->memory.allocate!=0)};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(op==FX_BIND_INFO) {
        FxPlatformInfo info={.abi=FX_PLATFORM_ABI,.bytes=sizeof(info),.binding_count=binding_total(s),
            .capabilities=s->capabilities,.ui_controls=FX_UI_CONTROLS,.ui_events=ui_event_capacity(s),
            .owner=s->owner,.generation=s->generation};
        memcpy(info.identity,s->platform->identity,32u);
        memcpy(a->value,s->platform->module_identity,32u);
        const char *names[2]={s->platform->model,s->platform->firmware};
        char *dest[2]={info.model,info.firmware};const uint32_t limits[2]={sizeof(info.model),sizeof(info.firmware)};
        for(uint32_t i=0;i<2u;++i)if(names[i])for(uint32_t j=0;j+1u<limits[i] && names[i][j];++j)dest[i][j]=names[i][j];
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if(op==FX_BIND_ENUMERATE) {
        if(q->arg[1]>=binding_total(s))return FX_EMPTY;
        const FxNamedBinding *b=fx_platform_binding(s,(uint32_t)q->arg[1]);
        FxBindingInfo info={.kind=b->kind,.abi=b->abi,.version=b->version,.flags=b->flags,
            .capabilities=b->capabilities,.bytes=b->bytes,
            .available=(b->capabilities&~s->capabilities)?0u:
                (b->kind==FX_BIND_HOOK || b->kind==FX_BIND_DEVICE)?(s->platform->io.resource?1u:0u):
                b->kind==FX_BIND_FUNCTION?(b->abi==FX_CALL_AAPCS64?(s->platform->io.call_frame?1u:0u):(s->platform->io.call?1u:0u)):1u};
        for(uint32_t i=0;i<63u && b->name[i];++i)info.name[i]=b->name[i];
        memcpy(info.original,b->original,16u);if(b->flags&FX_BIND_ESCAPE)info.address=b->address;
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if (op == FX_BIND_OPEN) {
        if (q->arg[2]>1u || q->arg[3] || q->bytes != 64u || !text((const char *)q->data, 64u, 0))
            return FX_ARGUMENT;
        uint32_t index=0u;const FxNamedBinding *b=find_binding(s,(const char *)q->data,&index);
        if(!b)return FX_CAPABILITY;
        if((b->capabilities&~s->capabilities) || (q->arg[1] && q->arg[1]!=b->version))return FX_CAPABILITY;
        FxBindingLease *l=q->arg[2]?0:fx_binding_store_existing(&s->bindings,index);
        if(l){a->value[0]=l->token;a->value[1]=b->kind;a->value[2]=b->abi;return l->attached==1u?FX_OK:FX_STATE;}
        if(s->next_token==FX_BINDING_IDENTITIES)return FX_BUDGET;
        binding_memory(s);uint64_t token=((uint64_t)s->owner<<48)|((uint64_t)s->generation<<16)|(s->next_token+1u);
        FxResult reserved=fx_binding_store_take(&s->bindings,token,index,&l);if(reserved!=FX_OK)return reserved;
        ++s->next_token;++s->binding_count;
        FxResult attached = FX_OK;
        if (b->kind == FX_BIND_HOOK || b->kind == FX_BIND_DEVICE) {
            attached = s->platform->io.resource
                           ? s->platform->io.resource(s->platform->io.context, FX_RESOURCE_ATTACH, b, s,
                                                      l->token, &l->cookie, q, a)
                           : FX_CAPABILITY;
            /* A nonzero cookie after failure represents uncertain native
             * ownership. Keep it pinned until an acknowledged detach. */
            if (attached != FX_OK && !l->cookie) {
                (void)fx_binding_store_release(&s->bindings,l);
                --s->binding_count;
                return attached;
            }
        }
        l->attached = attached == FX_OK ? 1u : 2u;
        a->value[0] = l->token;
        a->value[1] = b->kind;
        a->value[2] = b->abi;
        a->value[3] = b->flags;
        return attached;
    }
    FxBindingLease *l = lease(s, q->arg[1]);
    if (!l)
        return FX_STALE;
    const FxNamedBinding *b = fx_platform_binding(s,l->index);
    if (op == FX_BIND_DESCRIBE) {
        a->value[0] = b->kind;
        a->value[1] = b->abi;
        a->value[2] = b->version;
        a->value[3] = b->bytes;
        memcpy(a->data, b->original, 16u);
        a->bytes = 16u;
        if (b->flags & FX_BIND_ESCAPE) {
            memcpy(a->data + 16u, &b->address, 8u);
            a->bytes = 24u;
        }
        return FX_OK;
    }
    if (op == FX_BIND_CLOSE) {
        if (b->kind == FX_BIND_HOOK || b->kind == FX_BIND_DEVICE) {
            FxResult r = s->platform->io.resource(s->platform->io.context, FX_RESOURCE_DETACH, b, s, l->token,
                                                  &l->cookie, q, a);
            if (r != FX_OK)
                return r;
        }
        FxResult released=fx_binding_store_release(&s->bindings,l);
        if(released==FX_OK)--s->binding_count;
        return released;
    }
    if (l->attached != 1u)
        return FX_STATE;
    if (op == FX_BIND_COPY || op == FX_BIND_STORE) {
        uint64_t offset = q->arg[2], count = q->arg[3];
        if (b->kind != FX_BIND_TABLE || count > FX_SERVICE_BYTES || !count || offset > b->bytes ||
            count > b->bytes - offset)
            return FX_BOUNDS;
        if (op == FX_BIND_COPY) {
            if (!(b->flags & FX_BIND_READ) || !s->platform->io.read)
                return FX_CAPABILITY;
            FxResult r =
                s->platform->io.read(s->platform->io.context, b->address + offset, a->data, (uint32_t)count);
            if (r == FX_OK)
                a->bytes = (uint32_t)count;
            return r;
        }
        if (!(b->flags & FX_BIND_WRITE) || !s->platform->io.write)
            return FX_CAPABILITY;
        if (q->bytes != count)
            return FX_ARGUMENT;
        return s->platform->io.write(s->platform->io.context, b->address + offset, q->data, (uint32_t)count);
    }
    if (op != FX_BIND_CALL)
        return FX_ARGUMENT;
    if (b->kind == FX_BIND_HOOK || b->kind == FX_BIND_DEVICE)
        return s->platform->io.resource(s->platform->io.context, FX_RESOURCE_CALL, b, s, l->token, &l->cookie,
                                        q, a);
    FxNamedBinding snapshot;FxResult result=fx_platform_prepare_native(s,l->token,q->data,q->bytes,&snapshot);
    if(result!=FX_OK)return result;
    FxNativeCallFrame frame;memcpy(&frame,q->data,q->bytes);
    uint64_t values[2];result=fx_platform_invoke_native(s,&snapshot,&frame,q->bytes,values);
    a->value[0]=values[0];a->value[1]=values[1];
    if(result==FX_OK && snapshot.abi==FX_CALL_AAPCS64){memcpy(a->data,&frame,sizeof(frame));a->bytes=sizeof(frame);}
    return result;
}
static void ui_revision_next(FxPlatformOwner *s) {
    if(++s->revision==0u)++s->revision; /* Zero remains the legacy event stamp. */
}
static FxResult put(FxPlatformOwner *s, const FxServiceRequest *q) {
    if (q->bytes != sizeof(FxUiControl))
        return FX_ARGUMENT;
    FxUiControl c;
    memcpy(&c, q->data, sizeof(c));
    if (c.abi != FX_PLATFORM_ABI || c.bytes != sizeof(c) || !c.id || c.kind < FX_UI_PAGE ||
        c.kind > FX_UI_DOCUMENT || c.flags & ~7u || !text(c.label, sizeof(c.label), 0) ||
        !text(c.text, sizeof(c.text), 1) || c.option_count > FX_UI_OPTIONS ||
        (c.kind == FX_UI_CHOICE && (!c.option_count || c.value >= c.option_count)) ||
        ((c.kind == FX_UI_SWITCH || c.kind == FX_UI_DIALOG) && c.value > 1u))
        return FX_ARGUMENT;
    if(c.kind==FX_UI_DOCUMENT && (c.value || c.option_count))return FX_ARGUMENT;
    if(c.kind==FX_UI_EDIT) {
        uint32_t maximum=c.value?c.value:36u;
        if((maximum!=36u && maximum!=54u) || c.option_count)return FX_ARGUMENT;
        uint32_t n=0;while(c.text[n]) {
            if((uint8_t)c.text[n]<32u || (uint8_t)c.text[n]>126u)return FX_ARGUMENT;
            ++n;
        }
        if(n>maximum)return FX_ARGUMENT;
    }
    for (uint32_t i = 0; i < c.option_count; ++i)
        if (!text(c.options[i], 32u, 0))
            return FX_ARGUMENT;
    FxUiControl *existing = control_any(s, c.id);
    if(existing && (existing->flags&FX_UI_RETIRED))return FX_STALE;
    if (c.parent) {
        FxUiControl *p = control(s, c.parent);
        if (!p || p->kind != FX_UI_PAGE || p->id == c.id)
            return FX_ARGUMENT;
    }
    if (existing && (existing->kind != c.kind || existing->parent != c.parent ||
                     ((existing->flags^c.flags)&FX_UI_ROOT)))
        return FX_STATE;
    if((c.flags&FX_UI_ROOT) && (c.kind!=FX_UI_PAGE || c.parent))return FX_ARGUMENT;
    /* A direct root replaces only the implicit wrapper. Never orphan other
     * top-level controls or change a cached native page's slot identity. */
    for(uint32_t i=0;i<s->control_count;++i) {
        const FxUiControl *at=ui_control_at(s,i);if(at->id==c.id || (at->flags&FX_UI_RETIRED))continue;
        if(((c.flags&FX_UI_ROOT) && !at->parent) ||
           ((at->flags&FX_UI_ROOT) && !c.parent))return FX_STATE;
    }
    if (!existing) {
        FxResult r=ui_controls_grow(s);if(r!=FX_OK)return r;
        ui_control_set_order(s,s->control_count,s->control_count);
        existing=ui_control_at(s,s->control_count++);
        *existing=c;ui_control_insert(s,s->control_count-1u);
    }
    *existing = c;
    ui_revision_next(s);
    *ui_control_revision(s,ui_control_index(s,existing->id))=s->revision;
    return FX_OK;
}
static uint32_t retired_id(FxPlatformOwner *s,uint32_t id) {
    FxUiControl *c=control_any(s,id);return c && (c->flags&FX_UI_RETIRED);
}
/* Caller owns ui_lock. Logical retirement is atomic with queued-event purge;
 * storage/slot IDs remain retained for original callbacks until reboot. */
static FxResult remove_controls(FxPlatformOwner *s,uint32_t id,FxServiceReply *a) {
    FxUiControl *c=control(s,id);if(!c)return absent_control(s,id);
    uint32_t count=0u;
    ui_revision_next(s);
    /* PUT requires an existing parent and never permits reparenting. Thus
     * creation order is topological, including arbitrarily deep subtrees. */
    for(uint32_t i=0;i<s->control_count;++i) {
        FxUiControl *at=ui_control_at(s,i);
        if(!(at->flags&FX_UI_RETIRED) && (at==c || retired_id(s,at->parent))) {
            at->flags=(at->flags&~(uint32_t)(FX_UI_ENABLED|FX_UI_VISIBLE))|FX_UI_RETIRED;
            *ui_control_revision(s,i)=s->revision;++count;
        }
    }
    uint32_t kept=0u,old_count=s->event_count;
    for(uint32_t i=0;i<old_count;++i) {
        FxUiEvent e=(*ui_event(s,(s->event_head+i)%ui_event_capacity(s)));
        if(!retired_id(s,e.control)) {
            uint32_t destination=(s->event_head+kept++)%ui_event_capacity(s);
            (*ui_event(s,destination))=e;
            if(e.event==FX_INPUT_TEXT)memmove(ui_event_text(s,destination),
                ui_event_text(s,(s->event_head+i)%ui_event_capacity(s)),FX_UI_INPUT_TEXT);
        }
    }
    s->event_count=kept;
    uint64_t focus=atomic_load_explicit(&s->ui_focus,memory_order_acquire);
    while(((focus>>32)&1u) && retired_id(s,(uint32_t)focus)) {
        uint64_t epoch=focus>>33,next=(epoch+(epoch<UINT32_C(0x7fffffff)?1u:0u))<<33;
        if(atomic_compare_exchange_weak_explicit(&s->ui_focus,&focus,next,memory_order_acq_rel,memory_order_acquire))break;
    }
    a->value[0]=s->revision;a->value[1]=count;a->value[2]=old_count-kept;return FX_OK;
}
static FxResult reorder_controls(FxPlatformOwner *s,const FxServiceRequest *q,FxServiceReply *a) {
    uint32_t parent=(uint32_t)q->arg[1];
    if(q->arg[1]>UINT32_MAX || q->arg[2] || q->arg[3] || q->bytes%4u)return FX_ARGUMENT;
    if(parent) {
        FxUiControl *p=control(s,parent);
        if(!p)return absent_control(s,parent);
        if(p->kind!=FX_UI_PAGE)return FX_ARGUMENT;
    }
    uint32_t count=0u;
    for(uint32_t i=0;i<s->control_count;++i) {
        uint32_t at=ui_control_order(s,i);
        if(at>=s->control_count)return FX_INTEGRITY;
        const FxUiControl *c=ui_control_at(s,at);
        if(c->parent==parent && !(c->flags&FX_UI_RETIRED))++count;
    }
    if(q->bytes!=count*4u)return FX_ARGUMENT;
    for(uint32_t i=0;i<count;++i) {
        uint32_t id;memcpy(&id,q->data+i*4u,4u);FxUiControl *c=control(s,id);
        if(!c)return absent_control(s,id);
        if(c->parent!=parent)return FX_ARGUMENT;
        for(uint32_t j=0u;j<i;++j) {
            uint32_t other;memcpy(&other,q->data+j*4u,4u);if(other==id)return FX_ARGUMENT;
        }
    }
    uint32_t changed=0u;
    uint32_t next=0u;
    /* Every rejection was resolved before the first write. No several-KiB
     * scratch arrays or 32-bit descendant/duplicate masks on native stacks. */
    for(uint32_t i=0;i<s->control_count;++i) {
        uint32_t at=ui_control_order(s,i);const FxUiControl *c=ui_control_at(s,at);
        if(c->parent!=parent || (c->flags&FX_UI_RETIRED))continue;
        uint32_t id;memcpy(&id,q->data+next++*4u,4u);
        uint32_t slot=ui_control_index(s,id);changed|=at!=slot;ui_control_set_order(s,i,slot);
    }
    if(changed)ui_revision_next(s);
    a->value[0]=s->revision;return FX_OK;
}
static FxResult move_control(FxPlatformOwner *s,const FxServiceRequest *q,FxServiceReply *a) {
    if(q->bytes || !q->arg[1] || q->arg[1]>UINT32_MAX || q->arg[2]>UINT32_MAX || q->arg[3])return FX_ARGUMENT;
    uint32_t id=(uint32_t)q->arg[1],before=(uint32_t)q->arg[2];
    FxUiControl *c=control(s,id),*b=before?control(s,before):0;
    if(!c)return absent_control(s,id);
    if(before && !b)return absent_control(s,before);
    if(b && b->parent!=c->parent)return FX_ARGUMENT;
    uint32_t from=UINT32_MAX,to=UINT32_MAX;
    for(uint32_t i=0u;i<s->control_count;++i) {
        uint32_t index=ui_control_order(s,i);if(index>=s->control_count)return FX_INTEGRITY;
        const FxUiControl *at=ui_control_at(s,index);
        if(at==c)from=i;
        if(b?at==b:at->parent==c->parent && !(at->flags&FX_UI_RETIRED))to=b?i:i+1u;
    }
    if(from==UINT32_MAX || to==UINT32_MAX)return FX_INTEGRITY;
    if(to>from)--to;
    uint32_t index=ui_control_order(s,from);
    if(to>from)for(uint32_t i=from;i<to;++i)ui_control_set_order(s,i,ui_control_order(s,i+1u));
    else for(uint32_t i=from;i>to;--i)ui_control_set_order(s,i,ui_control_order(s,i-1u));
    ui_control_set_order(s,to,index);
    if(to!=from)ui_revision_next(s);
    a->value[0]=s->revision;return FX_OK;
}
static void ui_control_capacity(FxPlatformOwner *s,FxServiceReply *a) {
    uint32_t live=0u;
    for(uint32_t i=0u;i<s->control_count;++i)live+=(ui_control_at(s,i)->flags&FX_UI_RETIRED)==0u;
    FxUiControlCapacity info={.abi=1u,.bytes=sizeof(info),.slots=ui_controls_slots(s),.identities=s->control_count,
        .live=live,.retired=s->control_count-live,.native_window=FX_UI_CONTROLS,.flags=FX_UI_CONTROL_NATIVE_BOUNDED,
        .used=s->ui_controls?s->ui_controls->used:0u,.peak=s->ui_controls?s->ui_controls->peak:0u,.limit=s->ui_control_budget};
    if(s->ui_control_memory.allocate && s->ui_control_memory.release)info.flags|=FX_UI_CONTROL_ALLOCATOR;
    memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);
}
/* Caller owns ui_lock. Append before newer input and after older queued
 * events. A retiring control or terminal owner no longer has a recipient. */
static void flush_cancel(FxPlatformOwner *s) {
    if(atomic_load_explicit(&s->ui_cancel_state,memory_order_acquire)!=2u)return;
    if(s->phase!=FX_OWNER_RUNNING || !control(s,s->ui_cancel.control)) {
        atomic_store_explicit(&s->ui_cancel_state,0u,memory_order_release);return;
    }
    if(s->event_count==ui_event_capacity(s) || s->event_serial==UINT64_MAX)return;
    FxUiEvent event=s->ui_cancel;event.serial=++s->event_serial;
    (*ui_event(s,(s->event_head+s->event_count)%ui_event_capacity(s)))=event;++s->event_count;
    atomic_store_explicit(&s->ui_cancel_state,0u,memory_order_release);
}
FxResult fx_ui_defer_cancel(FxPlatformOwner *s,const FxUiControl *observed,uint32_t revision,uint32_t value) {
    if(!s || !observed || !observed->id || !revision)return FX_ARGUMENT;
    if(s->phase!=FX_OWNER_RUNNING)return FX_STATE;
    uint32_t empty=0u;
    if(!atomic_compare_exchange_strong_explicit(&s->ui_cancel_state,&empty,1u,memory_order_acquire,memory_order_relaxed))return FX_BUSY;
    s->ui_cancel=(FxUiEvent){0u,observed->id,FX_INPUT_CANCEL,value,revision};
    atomic_store_explicit(&s->ui_cancel_state,2u,memory_order_release);
    if(lock(s)){flush_cancel(s);unlock(s);}
    fx_platform_wake(s);return FX_OK;
}
static FxResult input(FxPlatformOwner *s, uint32_t id, uint32_t event, uint32_t value,
                      const FxUiControl *observed,uint32_t revision,const char *payload) {
    flush_cancel(s);
    FxUiControl *c = control(s, id);
    if(!c)return absent_control(s,id);
    if (!(c->flags & FX_UI_VISIBLE) || !(c->flags & FX_UI_ENABLED))
        return FX_EMPTY;
    if (event < FX_INPUT_ACTIVATE || event > FX_INPUT_TEXT ||
        (event==FX_INPUT_TEXT && (!observed || !payload || value>=FX_UI_INPUT_TEXT)))
        return FX_ARGUMENT;
    const FxUiControl *view=observed?observed:c;
    if(observed && (!revision || view->id!=id || view->kind!=c->kind || view->parent!=c->parent ||
       (view->flags&(FX_UI_ENABLED|FX_UI_VISIBLE))!=(FX_UI_ENABLED|FX_UI_VISIBLE)))return FX_STALE;
    if ((view->kind == FX_UI_SWITCH && event == FX_INPUT_CHANGE && value > 1u) ||
        (view->kind == FX_UI_CHOICE && event == FX_INPUT_CHANGE && value >= view->option_count))
        return FX_ARGUMENT;
    if (s->event_count == ui_event_capacity(s) || s->event_serial == UINT64_MAX) {
        atomic_fetch_add(&s->dropped_inputs, 1u);
        return FX_BUSY;
    }
    FxUiEvent e = {++s->event_serial, id, event, value, revision};
    uint32_t slot=(s->event_head+s->event_count)%ui_event_capacity(s);
    (*ui_event(s,slot))=e;
    if(event==FX_INPUT_TEXT) {
        memmove(ui_event_text(s,slot),payload,value+1u);
        memset(ui_event_text(s,slot)+value+1u,0,FX_UI_INPUT_TEXT-value-1u);
    }
    ++s->event_count;fx_platform_wake(s);
    if (event == FX_INPUT_CHANGE && (c->kind == FX_UI_SWITCH || c->kind == FX_UI_CHOICE)) {
        int same=c->kind!=FX_UI_CHOICE || (c->option_count==view->option_count &&
            !memcmp(c->options,view->options,c->option_count*sizeof(c->options[0])));
        if(same) {
            c->value = value;
            ui_revision_next(s);
            (*ui_control_revision(s,ui_control_index(s,c->id)))=s->revision;
        }
    }
    return FX_OK;
}
FxResult fx_ui_route(FxPlatformOwner *s, uint32_t id, uint32_t event, uint32_t value) {
    if (!s || s->phase != FX_OWNER_RUNNING)
        return FX_STATE;
    if (!lock(s))
        return FX_BUSY;
    FxResult r = s->phase == FX_OWNER_RUNNING ? input(s, id, event, value,0,0u,0) : FX_STATE;
    unlock(s);
    return r;
}
FxResult fx_ui_route_observed(FxPlatformOwner *s,const FxUiControl *observed,uint32_t revision,
                              uint32_t event,uint32_t value) {
    if(!s || !observed)return FX_ARGUMENT;
    if(s->phase!=FX_OWNER_RUNNING)return FX_STATE;
    if(!lock(s))return FX_BUSY;
    FxResult r=s->phase==FX_OWNER_RUNNING?input(s,observed->id,event,value,observed,revision,0):FX_STATE;
    unlock(s);return r;
}
FxResult fx_ui_observed_active(FxPlatformOwner *s,const FxUiControl *observed) {
    if(!s || !observed)return FX_ARGUMENT;
    if(!lock(s))return FX_BUSY;
    FxResult r=FX_OK;
    if(s->phase!=FX_OWNER_RUNNING)r=FX_STATE;
    else if(observed->id) {
        const FxUiControl *c=control(s,observed->id);
        if(!c)r=absent_control(s,observed->id);
        else if(c->kind!=observed->kind || c->parent!=observed->parent)r=FX_STALE;
        else if((c->flags&(FX_UI_ENABLED|FX_UI_VISIBLE))!=(FX_UI_ENABLED|FX_UI_VISIBLE))r=FX_EMPTY;
    } else if(observed->parent) {
        const FxUiControl *parent=control(s,observed->parent);
        if(!parent)r=absent_control(s,observed->parent);
        else if(parent->kind!=FX_UI_PAGE)r=FX_INTEGRITY;
        else if(!(parent->flags&FX_UI_VISIBLE))r=FX_EMPTY;
    }
    unlock(s);return r;
}
FxResult fx_ui_route_text_observed(FxPlatformOwner *s,const FxUiControl *observed,uint32_t revision,
                                  const char *payload,uint32_t bytes) {
    if(!s || !observed || !payload || !bytes || bytes>FX_UI_INPUT_TEXT || payload[bytes-1u] ||
       !text(payload,bytes,1))return FX_ARGUMENT;
    for(uint32_t i=0;i+1u<bytes;++i)if(!payload[i])return FX_ARGUMENT;
    if(s->phase!=FX_OWNER_RUNNING)return FX_STATE;
    if(!lock(s))return FX_BUSY;
    FxResult r=s->phase==FX_OWNER_RUNNING?
        input(s,observed->id,FX_INPUT_TEXT,bytes-1u,observed,revision,payload):FX_STATE;
    unlock(s);return r;
}
FxResult fx_ui_presence(FxPlatformOwner *s,uint32_t *count,int *visible) {
    if(!s || !count || !visible)return FX_ARGUMENT;
    uint32_t summary=atomic_load_explicit(&s->ui_presence,memory_order_acquire);
    *count=summary&UINT32_C(0x7fffffff);*visible=(summary>>31)!=0u;return FX_OK;
}
FxResult fx_ui_snapshot_range(FxPlatformOwner *s,uint32_t first,FxUiControl *out,uint32_t capacity,
    uint32_t *count,uint32_t *total,uint32_t *revision,uint32_t *versions,uint32_t *order) {
    if(!s || !count || !total || !revision || (capacity && !out))return FX_ARGUMENT;
    if(!lock(s))return FX_BUSY;
    if(first>s->control_count){unlock(s);return FX_BOUNDS;}
    uint32_t n=s->control_count-first;if(n>capacity)n=capacity;
    for(uint32_t i=0u;i<n;++i) {
        uint32_t index=first+i;out[i]=*ui_control_at(s,index);
        if(versions)versions[i]=*ui_control_revision(s,index);
        if(order)order[i]=ui_control_order(s,index);
    }
    *count=n;*total=s->control_count;*revision=s->revision;unlock(s);return FX_OK;
}
FxResult fx_ui_snapshot(FxPlatformOwner *s, FxUiControl *out, uint32_t capacity, uint32_t *count,
                        uint32_t *revision) {
    return fx_ui_snapshot_versioned(s,out,capacity,count,revision,0);
}
FxResult fx_ui_project_page(FxPlatformOwner *s,uint32_t parent,uint32_t first,FxUiControl *parent_copy,
    FxUiControl *out,uint32_t capacity,uint32_t *count,uint32_t *total,uint32_t *revision,
    uint32_t *versions,uint8_t *order) {
    if(!s || !parent_copy || !count || !total || !revision || capacity>FX_UI_CONTROLS || (capacity && !out))return FX_ARGUMENT;
    if(!lock(s))return FX_BUSY;
    FxUiControl *p=0;
    if(parent) {
        p=control(s,parent);
        if(!p){FxResult result=absent_control(s,parent);unlock(s);return result;}
        if(p->kind!=FX_UI_PAGE){unlock(s);return FX_ARGUMENT;}
    } else for(uint32_t i=0u;i<s->control_count;++i) {
        FxUiControl *at=ui_control_at(s,i);
        if((at->flags&(FX_UI_ROOT|FX_UI_RETIRED))==FX_UI_ROOT){p=at;parent=at->id;break;}
    }
    uint32_t selected[FX_UI_CONTROLS],display[FX_UI_CONTROLS],n=0u,all=0u;
    for(uint32_t i=0u;i<s->control_count;++i) {
        uint32_t slot=ui_control_order(s,i);
        if(slot>=s->control_count){unlock(s);return FX_INTEGRITY;}
        const FxUiControl *c=ui_control_at(s,slot);
        if(c->parent!=parent || (c->flags&(FX_UI_VISIBLE|FX_UI_RETIRED))!=FX_UI_VISIBLE)continue;
        if(all++>=first && n<capacity){selected[n]=display[n]=slot;++n;}
    }
    for(uint32_t i=1u;i<n;++i) {
        uint32_t slot=selected[i],j=i;
        while(j && selected[j-1u]>slot){selected[j]=selected[j-1u];--j;}
        selected[j]=slot;
    }
    FxUiControl wrapper={.abi=1u,.bytes=sizeof(wrapper),.kind=FX_UI_PAGE,.flags=FX_UI_ROOT|(all?FX_UI_VISIBLE:0u)};
    *parent_copy=p?*p:wrapper;
    for(uint32_t i=0u;i<n;++i) {
        out[i]=*ui_control_at(s,selected[i]);
        if(versions)versions[i]=*ui_control_revision(s,selected[i]);
        if(order) {
            uint32_t j=0u;while(j<n && selected[j]!=display[i])++j;
            order[i]=(uint8_t)j;
        }
    }
    *count=n;*total=all;*revision=s->revision;unlock(s);return FX_OK;
}
FxResult fx_ui_snapshot_selected(FxPlatformOwner *s,const FxUiControl *selected,uint32_t count,
    FxUiControl *out,uint32_t *versions,uint32_t *revision) {
    if(!s || !selected || !out || !versions || !revision || count>FX_UI_CONTROLS)return FX_ARGUMENT;
    if(!lock(s))return FX_BUSY;
    for(uint32_t i=0u;i<count;++i) {
        uint32_t index=selected[i].id?ui_control_index(s,selected[i].id):UINT32_MAX;
        if(selected[i].id && index==UINT32_MAX){unlock(s);return FX_INTEGRITY;}
        out[i]=selected[i].id?*ui_control_at(s,index):selected[i];
        versions[i]=selected[i].id?*ui_control_revision(s,index):s->revision;
    }
    *revision=s->revision;unlock(s);return FX_OK;
}
FxResult fx_ui_snapshot_versioned(FxPlatformOwner *s, FxUiControl *out, uint32_t capacity, uint32_t *count,
                                  uint32_t *revision,uint32_t control_revisions[FX_UI_CONTROLS]) {
    return fx_ui_snapshot_layout(s,out,capacity,count,revision,control_revisions,0);
}
FxResult fx_ui_snapshot_layout(FxPlatformOwner *s,FxUiControl *out,uint32_t capacity,uint32_t *count,
                               uint32_t *revision,uint32_t control_revisions[FX_UI_CONTROLS],
                               uint8_t order[FX_UI_CONTROLS]) {
    if (!s || !out || !count || !revision)
        return FX_ARGUMENT;
    if (!lock(s))
        return FX_BUSY;
    if (capacity < s->control_count) {
        unlock(s);
        return FX_BOUNDS;
    }
    /* The byte-order compatibility API cannot encode wide slot indices. */
    if(order && s->control_count>256u){unlock(s);return FX_BOUNDS;}
    for(uint32_t i=0u;i<s->control_count;++i) {
        out[i]=*ui_control_at(s,i);
        if(control_revisions)control_revisions[i]=*ui_control_revision(s,i);
        if(order)order[i]=(uint8_t)ui_control_order(s,i);
    }
    *count = s->control_count;
    *revision = s->revision;
    unlock(s);
    return FX_OK;
}
static FxResult reference(FxPlatformOwner *s,_Atomic uint32_t *counter,int retain) {
    if (!s)
        return FX_ARGUMENT;
    if (retain) {
        uint32_t n = atomic_load(counter);
        for(;;) {
            if(s->phase!=FX_OWNER_RUNNING && s->phase!=FX_OWNER_FINISHED)return FX_STATE;
            if(n==UINT32_MAX)return FX_BUDGET;
            if(atomic_compare_exchange_weak(counter,&n,n+1u))break;
        }
        /* Stop publishes its phase before checking the references. Cover a
         * concurrent close/stop without waiting on the worker's UI lock. */
        if(s->phase!=FX_OWNER_RUNNING && s->phase!=FX_OWNER_FINISHED) {
            atomic_fetch_sub(counter,1u);return FX_STATE;
        }
        return FX_OK;
    }
    uint32_t old = atomic_load(counter);
    while (old && !atomic_compare_exchange_weak(counter, &old, old - 1u)) {
    }
    return old ? FX_OK : FX_STALE;
}
FxResult fx_ui_reference(FxPlatformOwner *s,int retain) {
    return s?reference(s,&s->ui_references,retain):FX_ARGUMENT;
}
FxResult fx_ui_page_reference(FxPlatformOwner *s,int retain) {
    return s?reference(s,&s->ui_page_references,retain):FX_ARGUMENT;
}
FxResult fx_ui_focus_read(const FxPlatformOwner *s,FxUiFocus *out) {
    if(!s || !out)return FX_ARGUMENT;
    uint64_t word=atomic_load_explicit(&s->ui_focus,memory_order_acquire);
    *out=(FxUiFocus){.abi=1u,.bytes=sizeof(*out),.control=(uint32_t)word,
        .foreground=(uint32_t)((word>>32)&1u),.epoch=word>>33,
        .dropped_inputs=atomic_load(&s->dropped_inputs),.phase=atomic_load(&s->phase)};
    return FX_OK;
}
static void focus_event(FxPlatformOwner *s,uint32_t id,uint32_t event,uint32_t epoch) {
    if(!id || s->phase!=FX_OWNER_RUNNING)return;
    if(!lock(s)){atomic_fetch_add(&s->dropped_inputs,1u);return;}
    if(s->phase==FX_OWNER_RUNNING) {
        FxUiControl *c=control(s,id);
        FxResult r=c?input(s,id,event,c->value,0,0u,0):FX_EMPTY;
        if(r==FX_OK)(*ui_event(s,(s->event_head+s->event_count-1u)%ui_event_capacity(s))).reserved=epoch;
    }
    unlock(s);
}
FxResult fx_ui_focus_open(FxPlatformOwner *s,uint32_t id,uint64_t *token) {
    if(!s || !token)return FX_ARGUMENT;
    if(s->phase!=FX_OWNER_RUNNING && s->phase!=FX_OWNER_FINISHED)return FX_STATE;
    uint64_t old=atomic_load_explicit(&s->ui_focus,memory_order_acquire),next;
    for(;;) {
        if(((old>>32)&1u) && (uint32_t)old==id){*token=old;return FX_OK;}
        if((old>>33)==UINT32_C(0x7fffffff))return FX_BUDGET;
        next=(((old>>33)+1u)<<33)|UINT64_C(0x100000000)|id;
        if(atomic_compare_exchange_weak_explicit(&s->ui_focus,&old,next,memory_order_acq_rel,memory_order_acquire))break;
    }
    *token=next;
    if((old>>32)&1u)focus_event(s,(uint32_t)old,FX_INPUT_BLUR,(uint32_t)(next>>33));
    focus_event(s,id,FX_INPUT_FOCUS,(uint32_t)(next>>33));return FX_OK;
}
FxResult fx_ui_focus_close(FxPlatformOwner *s,uint64_t token) {
    if(!s || !((token>>32)&1u))return FX_ARGUMENT;
    /* Exhaustion forbids another open, but must never prevent the final
     * page from clearing foreground. No future token can reuse this epoch. */
    uint64_t epoch=token>>33;
    uint64_t next=(epoch+(epoch<UINT32_C(0x7fffffff)?1u:0u))<<33,expected=token;
    if(!atomic_compare_exchange_strong_explicit(&s->ui_focus,&expected,next,memory_order_acq_rel,memory_order_acquire))return FX_STALE;
    focus_event(s,(uint32_t)token,FX_INPUT_BLUR,(uint32_t)(next>>33));return FX_OK;
}
static FxResult poll_event(FxPlatformOwner *s,void *out,uint32_t capacity,uint32_t *bytes) {
    *bytes=0u;
    flush_cancel(s);
    if(!s->event_count)return FX_EMPTY;
    const FxUiEvent *event=&(*ui_event(s,s->event_head));
    uint32_t size=event->event==FX_INPUT_TEXT?sizeof(FxUiTextEvent):sizeof(FxUiEvent);
    if(capacity<size)return FX_BOUNDS; /* Never consume a truncated text result. */
    memcpy(out,event,sizeof(*event));
    if(event->event==FX_INPUT_TEXT)memcpy((uint8_t *)out+sizeof(*event),ui_event_text(s,s->event_head),FX_UI_INPUT_TEXT);
    s->event_head=(s->event_head+1u)%ui_event_capacity(s);--s->event_count;*bytes=size;
    flush_cancel(s);
    return FX_OK;
}
FxResult fx_ui_poll_message(FxPlatformOwner *s,void *out,uint32_t capacity,uint32_t *bytes) {
    if(!s || !out || !bytes)return FX_ARGUMENT;
    *bytes=0u;
    if(!lock(s))return FX_BUSY;
    FxResult result=s->phase==FX_OWNER_RUNNING?poll_event(s,out,capacity,bytes):FX_STATE;
    unlock(s);return result;
}
FxResult fx_ui_poll_event(FxPlatformOwner *s,FxUiEvent *out) {
    uint32_t bytes;return fx_ui_poll_message(s,out,sizeof(*out),&bytes);
}
/* Caller owns ui_lock. This path runs only on a normal service worker;
 * callback enqueue/peek/pop never allocate or keep a queue element pointer. */
static FxResult ui_queue(FxPlatformOwner *s,const FxServiceRequest *q,FxServiceReply *a) {
    if(q->bytes || q->arg[1]>UINT32_MAX || q->arg[2] || q->arg[3])return FX_ARGUMENT;
    uint32_t wanted=(uint32_t)q->arg[1];
    if(wanted) {
        if(s->phase!=FX_OWNER_RUNNING)return FX_STATE;
        uint32_t capacity=FX_UI_EVENTS;
        while(capacity<wanted) {
            if(capacity>FX_UI_EVENT_BUDGET/(2u*(uint32_t)sizeof(FxUiTextEvent)))return FX_BUDGET;
            capacity*=2u;
        }
        if(capacity<s->event_count)return FX_BUSY;
        if(capacity!=ui_event_capacity(s)) {
            FxIndexMemory memory=s->ui_event_memory;
            uint32_t bytes=capacity>FX_UI_EVENTS?capacity*(uint32_t)sizeof(FxUiTextEvent):0u;
            if(bytes>FX_UI_EVENT_BUDGET-s->ui_event_bytes)return FX_BUDGET;
            FxUiTextEvent *next=0;FxAllocationObserver observer={0};
            if(bytes) {
                if(!memory.allocate || !memory.release)return FX_CAPABILITY;
                next=memory.allocate(memory.context,bytes);if(!next)return FX_BUDGET;
                observer=fx_platform_account_observer(s,"ui.events");
                fx_allocation_observe(observer,bytes,FX_ALLOC_REQUESTED);
                uint32_t overlap=s->ui_event_bytes+bytes;
                if(s->ui_event_peak<overlap)s->ui_event_peak=overlap;
                if(s->phase!=FX_OWNER_RUNNING) {
                    memory.release(memory.context,next);fx_allocation_observe(observer,bytes,FX_ALLOC_FREE_PENDING);return FX_STATE;
                }
            }
            for(uint32_t i=0u;i<s->event_count;++i) {
                uint32_t at=(s->event_head+i)%ui_event_capacity(s);FxUiEvent event=*ui_event(s,at);
                if(next) {
                    next[i].event=event;
                    if(event.event==FX_INPUT_TEXT)memcpy(next[i].text,ui_event_text(s,at),FX_UI_INPUT_TEXT);
                } else {
                    s->events[i]=event;
                    if(event.event==FX_INPUT_TEXT)memcpy(s->event_text[i],ui_event_text(s,at),FX_UI_INPUT_TEXT);
                }
            }
            FxUiTextEvent *old=s->ui_event_buffer;uint32_t old_bytes=s->ui_event_bytes;
            FxAllocationObserver old_observer=s->ui_event_observer;
            s->ui_event_buffer=next;s->ui_event_bytes=bytes;s->ui_event_observer=observer;s->event_head=0u;
            atomic_store_explicit(&s->ui_event_capacity,capacity,memory_order_release);
            if(next)fx_allocation_observe(observer,bytes,FX_ALLOC_PUBLISHED);
            if(old){memory.release(memory.context,old);fx_allocation_observe(old_observer,old_bytes,FX_ALLOC_FREE_PUBLISHED);}
        }
        flush_cancel(s);
    }
    FxUiQueueInfo info={1u,sizeof(info),ui_event_capacity(s),s->event_count,s->ui_event_bytes,
        s->ui_event_peak,FX_UI_EVENT_BUDGET,atomic_load(&s->dropped_inputs),atomic_load(&s->ui_cancel_state)};
    memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
}
static FxResult ui(FxPlatformOwner *s, const FxServiceRequest *q, FxServiceReply *a) {
    if(q->arg[0]==FX_UI_QUEUE) {
        if(!lock(s))return FX_BUSY;
        FxResult result=ui_queue(s,q,a);unlock(s);return result;
    }
    if(q->arg[0]==FX_UI_CONTROL_CAPACITY) {
        if(q->bytes || q->arg[1] || q->arg[2] || q->arg[3])return FX_ARGUMENT;
        if(!lock(s))return FX_BUSY;
        ui_control_capacity(s,a);unlock(s);return FX_OK;
    }
    if(q->arg[0]==FX_UI_QUERY || q->arg[0]==FX_UI_FOCUS || q->arg[0]==FX_UI_STATUS) {
        if(q->bytes || q->arg[1] || q->arg[2] || q->arg[3])return FX_ARGUMENT;
        if(q->arg[0]==FX_UI_FOCUS) {
            FxUiFocus focus;FxResult r=fx_ui_focus_read(s,&focus);
            if(r==FX_OK){memcpy(a->data,&focus,sizeof(focus));a->bytes=sizeof(focus);}return r;
        }
        if(q->arg[0]==FX_UI_STATUS) {
            if(!lock(s))return FX_BUSY;
            uint64_t focus=atomic_load_explicit(&s->ui_focus,memory_order_acquire);
            uint64_t submitted=atomic_load_explicit(&s->ui_submission,memory_order_acquire);
            FxUiStatus status={1u,sizeof(status),s->revision,(uint32_t)(submitted>>32),(uint32_t)focus,
                (uint32_t)((focus>>32)&1u),(uint32_t)(focus>>33),(uint32_t)submitted,atomic_load(&s->phase),0u};
            unlock(s);memcpy(a->data,&status,sizeof(status));a->bytes=sizeof(status);return FX_OK;
        }
        FxUiInfo info={1u,sizeof(info),FX_UI_CAP_ROOT|FX_UI_CAP_FOCUS|FX_UI_CAP_LIVE_ROWS|FX_UI_CAP_LIVE_TITLES|
            FX_UI_CAP_EVENT_REVISION|FX_UI_CAP_LIVE_PICKER|FX_UI_CAP_REMOVE|FX_UI_CAP_REORDER|FX_UI_CAP_SUBMISSION|FX_UI_CAP_MODAL|FX_UI_CAP_TEXT_EVENTS|FX_UI_CAP_EDIT|FX_UI_CAP_DOCUMENT|FX_UI_CAP_QUEUE|FX_UI_CAP_CONTROL_CAPACITY|FX_UI_CAP_MOVE,FX_UI_CONTROLS,
            ui_event_capacity(s),FX_UI_OPTIONS,FX_UI_TEXT,0u};
        memcpy(a->data,&info,sizeof(info));a->bytes=sizeof(info);return FX_OK;
    }
    if (!lock(s))
        return FX_BUSY;
    if (s->phase != FX_OWNER_RUNNING) {
        unlock(s);
        return FX_STATE;
    }
    FxResult r = FX_ARGUMENT;
    uint32_t op = (uint32_t)q->arg[0];
    if (op == FX_UI_PUT) {
        r = put(s, q);
        if(r==FX_OK){a->value[0]=s->revision;a->value[1]=s->event_serial;}
    }
    else if (op == FX_UI_HIDE) {
        FxUiControl *c = control(s, (uint32_t)q->arg[1]);
        if (c) {
            c->flags &= ~(uint32_t)FX_UI_VISIBLE;
            ui_revision_next(s);
            (*ui_control_revision(s,ui_control_index(s,c->id)))=s->revision;
            a->value[0]=s->revision;
            r = FX_OK;
        } else
            r = absent_control(s,(uint32_t)q->arg[1]);
    } else if (op == FX_UI_GET) {
        FxUiControl *c = control(s, (uint32_t)q->arg[1]);
        if (c) {
            memcpy(a->data, c, sizeof(*c));
            a->bytes = sizeof(*c);
            a->value[0]=(*ui_control_revision(s,ui_control_index(s,c->id)));
            a->value[1]=s->revision;a->value[2]=s->event_serial;
            r = FX_OK;
        } else
            r = absent_control(s,(uint32_t)q->arg[1]);
    } else if(op==FX_UI_REMOVE) {
        if(!q->bytes && q->arg[1] && q->arg[1]<=UINT32_MAX && !q->arg[2] && !q->arg[3])
            r=remove_controls(s,(uint32_t)q->arg[1],a);
    } else if(op==FX_UI_REORDER) {
        r=reorder_controls(s,q,a);
    } else if(op==FX_UI_MOVE) {
        r=move_control(s,q,a);
    } else if (op == FX_UI_POLL) {
        a->value[0] = atomic_load(&s->dropped_inputs);
        r=poll_event(s,a->data,sizeof(a->data),&a->bytes);
    }
    /* Modules publish controls/poll events. Only the native input service may
     * route input; an EL0 request cannot pretend to be a physical UI event. */
    unlock(s);
    return r;
}
FxResult fx_platform_dispatch(FxPlatformOwner *s, const FxServiceRequest *q, FxServiceReply *a) {
    if (!s || !s->platform || !q || !a || q->bytes > FX_SERVICE_BYTES || q->arg[0] > UINT32_MAX)
        return FX_ARGUMENT;
    if (q->operation == FX_S_LIFECYCLE) {
        uint32_t op = (uint32_t)q->arg[0];
        if (op == FX_LIFE_QUERY) {
            a->value[0] = s->kind;
            a->value[1] = s->phase;
            a->value[2] = s->binding_count;
            a->value[3] = (uint64_t)atomic_load(&s->ui_references)+atomic_load(&s->ui_page_references);
            return FX_OK;
        }
        if (s->phase != FX_OWNER_RUNNING)
            return FX_STATE;
        if (op == FX_LIFE_CONFIGURE && !s->configured && !s->binding_count && !s->control_count &&
            q->arg[1] >= FX_MODULE_APP && q->arg[1] <= FX_MODULE_SERVICE) {
            s->kind = (uint32_t)q->arg[1];
            s->configured = 1u;
            return FX_OK;
        }
        if (op == FX_LIFE_FINISH && s->kind == FX_MODULE_APP) {
            if (!lock(s))
                return FX_BUSY;
            /* A native hook may report Panic without taking ui_lock. Never
             * let a late FINISH overwrite that terminal fault. */
            uint32_t running=FX_OWNER_RUNNING;
            if(!atomic_compare_exchange_strong(&s->phase,&running,FX_OWNER_FINISHED)) {
                unlock(s);return FX_STATE;
            }
            /* FINISH makes retained controls read-only, including an already
             * open native picker. Publish this as a real UI revision before
             * the worker ceases issuing service requests. */
            for(uint32_t i=0;i<s->control_count;++i)ui_control_at(s,i)->flags&=~(uint32_t)FX_UI_ENABLED;
            ui_revision_next(s);
            for(uint32_t i=0;i<s->control_count;++i)*ui_control_revision(s,i)=s->revision;
            unlock(s);
            return FX_OK;
        }
        return FX_STATE;
    }
    if (s->phase != FX_OWNER_RUNNING)
        return FX_STATE;
    if (q->operation == FX_S_PLATFORM) {
        if (!bind_lock(s))
            return FX_BUSY;
        FxResult r = s->phase == FX_OWNER_RUNNING ? binding(s, q, a) : FX_STATE;
        bind_unlock(s);
        return r;
    }
    if (q->operation == FX_S_UI)
        return ui(s, q, a);
    return FX_CAPABILITY;
}
FxResult fx_platform_stop(FxPlatformOwner *s, uint32_t panic) {
    if (!s || !s->platform)
        return FX_ARGUMENT;
    uint32_t phase=atomic_load(&s->phase);
    if(phase==FX_OWNER_QUIESCED)return FX_OK;
    if(panic) {
        /* Latch before attempting either lock: native callbacks can report a
         * fault while the GUI or a resource call owns them. BUSY defers only
         * provider notification, never publication of the fault itself. */
        uint32_t zero=0u;
        (void)atomic_compare_exchange_strong(&s->first_error,&zero,FX_STATE);
        do {
            if(phase==FX_OWNER_QUIESCED)return FX_OK;
        } while(!atomic_compare_exchange_strong(&s->phase,&phase,FX_OWNER_FAULT));
    } else {
        if(phase==FX_OWNER_FAULT)return FX_STATE;
        if(!lock(s))return FX_BUSY;
        phase=atomic_load(&s->phase);
        for(;;) {
            if(phase==FX_OWNER_FAULT || phase==FX_OWNER_QUIESCED) {
                unlock(s);return phase==FX_OWNER_FAULT?FX_STATE:FX_OK;
            }
            if(atomic_compare_exchange_strong(&s->phase,&phase,FX_OWNER_STOPPING))break;
        }
        unlock(s);
    }
    fx_platform_wake(s);
    if (!bind_lock(s))
        return FX_BUSY;
    FxResult result = FX_OK;
    for(uint32_t i=0;i<s->bindings.lease_capacity;++i) {
        if(!panic && atomic_load(&s->phase)==FX_OWNER_FAULT) {result=FX_STATE;break;}
        FxBindingLease *l=fx_binding_store_lease_at(&s->bindings,i);
        if(!l)
            continue;
        const FxNamedBinding *b = fx_platform_binding(s,l->index);
        if (b->kind == FX_BIND_HOOK || b->kind == FX_BIND_DEVICE) {
            FxResult r = panic && l->panic_status ? (FxResult)(l->panic_status-1u) :
                s->platform->io.resource(s->platform->io.context,
                                                  panic ? FX_RESOURCE_PANIC : FX_RESOURCE_DETACH, b, s,
                                                  l->token, &l->cookie, 0, 0);
            if (panic) {
                /* Do not lose contended notifications or repeat providers
                 * that already replied. Visit every lease even when one is
                 * busy; BUSY takes precedence until all pending work replies.
                 * A permanent error is retained, not blindly retried. */
                if(r!=FX_BUSY)l->panic_status=(uint32_t)r+1u;
                if(r==FX_BUSY || result==FX_OK)result=r;
                continue;
            }
            /* The in-flight callback can fault even when detach returns OK.
             * Keep its lease and every remaining lease for fault reporting;
             * an acknowledged native side effect is not rolled back here. */
            if(atomic_load(&s->phase)==FX_OWNER_FAULT) {result=FX_STATE;break;}
            if (r != FX_OK) {
                uint32_t zero=0u;
                if (r != FX_BUSY)
                    (void)atomic_compare_exchange_strong(&s->first_error,&zero,(uint32_t)r);
                result = r;
                continue;
            }
        }
        if (!panic) {
            (void)fx_binding_store_release(&s->bindings,l);
            --s->binding_count;
        }
    }
    bind_unlock(s);
    if (panic)
        return result; /* Retain image, callbacks and uncertain native IO. */
    if (result != FX_OK)
        return result;
    if (atomic_load(&s->ui_references))
        return FX_BUSY;
    /* Published GUI storage and core callbacks are retained through reboot.
     * A concurrent fault takes precedence over successful ordinary cleanup. */
    phase=FX_OWNER_STOPPING;
    if(atomic_compare_exchange_strong(&s->phase,&phase,FX_OWNER_QUIESCED) || phase==FX_OWNER_QUIESCED)return FX_OK;
    return FX_STATE;
}
FxResult fx_platform_notify_locked(FxPlatformOwner *s, uint64_t token, uint32_t event, uint32_t value) {
    if (!s || s->phase != FX_OWNER_RUNNING)
        return FX_STATE;
    FxBindingLease *l = lease(s, token);
    if (!l || l->attached != 1u) {
        return FX_STALE;
    }
    if (fx_platform_binding(s,l->index)->kind != FX_BIND_HOOK) {
        return FX_CAPABILITY;
    }
    if (!lock(s)) {
        return FX_BUSY;
    }
    flush_cancel(s);
    FxResult r = FX_OK;
    if (s->phase != FX_OWNER_RUNNING)
        r = FX_STATE;
    else if (s->event_count == ui_event_capacity(s) || s->event_serial == UINT64_MAX) {
        atomic_fetch_add(&s->dropped_inputs, 1u);
        r = FX_BUSY;
    } else {
        (*ui_event(s,(s->event_head + s->event_count) % ui_event_capacity(s))) =
            (FxUiEvent){++s->event_serial, 0u, event, value, (uint32_t)(token & 65535u)};
        ++s->event_count;
    }
    unlock(s);
    return r;
}
FxResult fx_platform_notify(FxPlatformOwner *s,uint64_t token,uint32_t event,uint32_t value) {
    if(!s || s->phase!=FX_OWNER_RUNNING)return FX_STATE;
    if(!bind_lock(s))return FX_BUSY;
    FxResult r=fx_platform_notify_locked(s,token,event,value);bind_unlock(s);
    if(r==FX_OK)fx_platform_wake(s);
    return r;
}
