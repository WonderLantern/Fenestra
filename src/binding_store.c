#include "fenestra_binding_store.h"
#include "fenestra_memory.h"

static void *allocate(FxBindingStore *s,uint32_t bytes) {
    if(!s->memory.allocate || !s->memory.release || s->used>s->budget || bytes>s->budget-s->used)return 0;
    void *p=s->memory.allocate(s->memory.context,bytes);
    if(p){s->used+=bytes;if(s->used>s->peak)s->peak=s->used;fx_allocation_observe(s->observer,bytes,FX_ALLOC_REQUESTED);}return p;
}
static void release(FxBindingStore *s,void *p,uint32_t bytes,uint32_t published) {
    s->memory.release(s->memory.context,p);s->used-=bytes;
    fx_allocation_observe(s->observer,bytes,published?FX_ALLOC_FREE_PUBLISHED:FX_ALLOC_FREE_PENDING);
}
static uint32_t mix(uint64_t token) {
    uint32_t x=(uint32_t)(token^(token>>32));x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);
}
static FxBindingSlot *slot(const FxBindingStore *s,uint32_t id) {
    uint32_t index=id-1u;
    if(index<FX_BINDING_INLINE_LEASES)return (FxBindingSlot *)&s->inline_leases[index];
    index-=FX_BINDING_INLINE_LEASES;return &s->lease_chunks[index/FX_BINDING_CHUNK][index%FX_BINDING_CHUNK];
}
void fx_binding_store_init(FxBindingStore *s,FxIndexMemory memory) {
    memset(s,0,sizeof(*s));s->memory=memory;s->budget=FX_BINDING_METADATA_BUDGET;s->used=s->peak=(uint32_t)sizeof(*s);
    s->free_lease=1u;s->lease_capacity=FX_BINDING_INLINE_LEASES;s->buckets=s->inline_buckets;s->bucket_count=16u;
    for(uint32_t i=0;i<FX_BINDING_INLINE_LEASES;++i) {
        s->inline_leases[i].id=i+1u;s->inline_leases[i].next_free=i+1u<FX_BINDING_INLINE_LEASES?i+2u:0u;
    }
}
FxBindingLease *fx_binding_store_lease_at(const FxBindingStore *s,uint32_t index) {
    if(!s || index>=s->lease_capacity)return 0;
    FxBindingLease *l=&slot(s,index+1u)->value;return l->token?l:0;
}
FxBindingLease *fx_binding_store_lease(const FxBindingStore *s,uint64_t token) {
    if(!s || !token || !s->bucket_count)return 0;
    for(uint32_t id=s->buckets[mix(token)&(s->bucket_count-1u)];id;) {
        FxBindingSlot *p=slot(s,id);if(p->value.token==token)return &p->value;id=p->next_token;
    }
    return 0;
}
FxBindingLease *fx_binding_store_existing(const FxBindingStore *s,uint32_t binding) {
    if(!s || !s->bucket_count)return 0;
    FxBindingSlot *first=0;
    for(uint32_t id=s->buckets[s->bucket_count+(mix(binding)&(s->bucket_count-1u))];id;) {
        FxBindingSlot *p=slot(s,id);
        if(p->value.index==binding && (!first || p->id<first->id))first=p;
        id=p->next_binding;
    }
    return first?&first->value:0;
}
static void insert(FxBindingStore *s,FxBindingSlot *p) {
    uint32_t a=mix(p->value.token)&(s->bucket_count-1u),b=s->bucket_count+(mix(p->value.index)&(s->bucket_count-1u));
    p->next_token=s->buckets[a];p->next_binding=s->buckets[b];s->buckets[a]=s->buckets[b]=p->id;
}
FxResult fx_binding_store_take(FxBindingStore *s,uint64_t token,uint32_t binding,FxBindingLease **out) {
    if(!s || !s->bucket_count || !token || !out || *out)return FX_ARGUMENT;
    if(fx_binding_store_lease(s,token))return FX_STATE;
    uint32_t buckets=s->bucket_count,*table=s->buckets;
    if((s->active+1u)*2u>buckets) {
        buckets*=2u;table=allocate(s,buckets*8u);if(!table)return s->memory.allocate?FX_BUDGET:FX_CAPABILITY;
        memset(table,0,buckets*8u);
    }
    if(!s->free_lease) {
        uint32_t capacity=s->index_capacity;FxBindingSlot **index=s->lease_chunks;
        if(s->chunks==capacity) {
            capacity=capacity?capacity*2u:4u;
            index=allocate(s,capacity*(uint32_t)sizeof(*index));
            if(!index)goto failed_index;
            memset(index,0,capacity*sizeof(*index));
            if(s->chunks)memcpy(index,s->lease_chunks,s->chunks*sizeof(*index));
        }
        FxBindingSlot *chunk=allocate(s,(uint32_t)sizeof(*chunk)*FX_BINDING_CHUNK);
        if(!chunk) {
            if(index!=s->lease_chunks)release(s,index,capacity*(uint32_t)sizeof(*index),0u);
            goto failed_index;
        }
        memset(chunk,0,sizeof(*chunk)*FX_BINDING_CHUNK);
        for(uint32_t i=0;i<FX_BINDING_CHUNK;++i) {
            chunk[i].id=s->lease_capacity+i+1u;chunk[i].next_free=i+1u<FX_BINDING_CHUNK?chunk[i].id+1u:0u;
        }
        fx_allocation_observe(s->observer,(uint32_t)sizeof(*chunk)*FX_BINDING_CHUNK,FX_ALLOC_PUBLISHED);index[s->chunks]=chunk;
        if(index!=s->lease_chunks) {
            if(s->lease_chunks)release(s,s->lease_chunks,s->index_capacity*(uint32_t)sizeof(*index),1u);
            fx_allocation_observe(s->observer,capacity*(uint32_t)sizeof(*index),FX_ALLOC_PUBLISHED);
            s->lease_chunks=index;s->index_capacity=capacity;
        }
        ++s->chunks;s->free_lease=s->lease_capacity+1u;s->lease_capacity+=FX_BINDING_CHUNK;
    }
    if(table!=s->buckets) {
        uint32_t *old=s->buckets,old_count=s->bucket_count;
        fx_allocation_observe(s->observer,buckets*8u,FX_ALLOC_PUBLISHED);s->buckets=table;s->bucket_count=buckets;
        for(uint32_t i=0;i<s->lease_capacity;++i){FxBindingSlot *p=slot(s,i+1u);if(p->value.token)insert(s,p);}
        if(old!=s->inline_buckets)release(s,old,old_count*8u,1u);
    }
    FxBindingSlot *p=slot(s,s->free_lease);s->free_lease=p->next_free;p->next_free=0u;
    p->value=(FxBindingLease){.token=token,.index=binding,.attached=2u};insert(s,p);++s->active;*out=&p->value;return FX_OK;
failed_index:
    if(table!=s->buckets)release(s,table,buckets*8u,0u);
    return s->memory.allocate?FX_BUDGET:FX_CAPABILITY;
}
FxResult fx_binding_store_release(FxBindingStore *s,FxBindingLease *lease) {
    if(!s || !lease || !lease->token || fx_binding_store_lease(s,lease->token)!=lease)return FX_STALE;
    FxBindingSlot *p=(FxBindingSlot *)(void *)lease;
    uint32_t a=mix(lease->token)&(s->bucket_count-1u),b=s->bucket_count+(mix(lease->index)&(s->bucket_count-1u));
    uint32_t *link=&s->buckets[a];while(*link!=p->id)link=&slot(s,*link)->next_token;*link=p->next_token;
    link=&s->buckets[b];while(*link!=p->id)link=&slot(s,*link)->next_binding;*link=p->next_binding;
    memset(&p->value,0,sizeof(p->value));p->next_token=p->next_binding=0u;p->next_free=s->free_lease;s->free_lease=p->id;--s->active;return FX_OK;
}

static uint32_t name_hash(const char *name) {
    uint32_t h=2166136261u;for(uint32_t i=0;i<64u && name[i];++i)h=(h^(uint8_t)name[i])*16777619u;return h;
}
static int name_equal(const char *a,const char *b) {
    for(uint32_t i=0;i<64u;++i){if(a[i]!=b[i])return 0;if(!a[i])return 1;}return 0;
}
static uint32_t side(uint32_t hash,uint32_t bit){return (hash>>bit)&1u;}
static uintptr_t leaf(FxBindingDeclaration *p){return (uintptr_t)p|1u;}
static FxBindingDeclaration *declaration(uintptr_t p){return (FxBindingDeclaration *)(p&~(uintptr_t)1u);}
const FxNamedBinding *fx_binding_store_find(const FxBindingStore *s,const char *name,uint32_t *index,uint32_t *steps) {
    if(!s || !name)return 0;
    uint32_t visits=0u,hash=name_hash(name);uintptr_t node=atomic_load_explicit(&s->names,memory_order_acquire);
    while(node && !(node&1u)) {
        FxBindingDeclaration *p=(void *)node;++visits;node=atomic_load_explicit(&p->child[side(hash,p->bit)],memory_order_acquire);
    }
    FxBindingDeclaration *p=node?declaration(node):0;
    if(p && p->hash==hash)for(;p;p=(void *)atomic_load_explicit(&p->collision,memory_order_acquire)) {
        ++visits;if(name_equal(p->name,name)){if(index)*index=p->index;if(steps)*steps=visits;return &p->value;}
    }
    if(steps)*steps=visits;
    return 0;
}
const FxNamedBinding *fx_binding_store_declaration(const FxBindingStore *s,uint32_t index) {
    if(!s || index>=atomic_load_explicit(&s->declarations,memory_order_acquire))return 0;
    if(index<FX_BINDING_INLINE_DECLARATIONS)return &s->inline_declarations[index].value;
    index-=FX_BINDING_INLINE_DECLARATIONS;uint32_t chunk=index/FX_BINDING_CHUNK;
    FxBindingDeclarationDirectory *dir=atomic_load_explicit(&s->directory,memory_order_acquire);
    FxBindingDeclarationLeaf *part=atomic_load_explicit(&dir->leaf[chunk/FX_BINDING_DIRECTORY],memory_order_acquire);
    FxBindingDeclarationBlock *block=atomic_load_explicit(&part->block[chunk%FX_BINDING_DIRECTORY],memory_order_acquire);
    return &block->entries[index%FX_BINDING_CHUNK].value;
}
static void publish_name(FxBindingStore *s,FxBindingDeclaration *p) {
    uintptr_t node=atomic_load_explicit(&s->names,memory_order_acquire),old=node;
    if(!node){atomic_store_explicit(&s->names,leaf(p),memory_order_release);return;}
    while(!(node&1u)) {
        FxBindingDeclaration *b=(void *)node;node=atomic_load_explicit(&b->child[side(p->hash,b->bit)],memory_order_acquire);
    }
    FxBindingDeclaration *previous=declaration(node);
    if(previous->hash==p->hash) {
        atomic_store_explicit(&p->collision,atomic_load_explicit(&previous->collision,memory_order_relaxed),memory_order_relaxed);
        atomic_store_explicit(&previous->collision,(uintptr_t)p,memory_order_release);return;
    }
    uint32_t difference=p->hash^previous->hash,bit=0u;while(difference>>=1u)++bit;
    _Atomic uintptr_t *link=&s->names;node=old;
    while(!(node&1u)) {
        FxBindingDeclaration *b=(void *)node;if(b->bit<=bit)break;
        link=&b->child[side(p->hash,b->bit)];node=atomic_load_explicit(link,memory_order_acquire);
    }
    p->bit=bit;uint32_t direction=side(p->hash,bit);
    atomic_store_explicit(&p->child[direction],leaf(p),memory_order_relaxed);
    atomic_store_explicit(&p->child[direction^1u],node,memory_order_relaxed);
    atomic_store_explicit(link,(uintptr_t)p,memory_order_release);
}
FxResult fx_binding_store_declare(FxBindingStore *s,const FxNamedBinding *value,uint32_t *out) {
    if(!s || !value || !value->name || !out || !value->name[0])return FX_ARGUMENT;
    uint32_t bytes=0u;while(bytes<64u && value->name[bytes])++bytes;if(bytes==64u)return FX_ARGUMENT;
    if(fx_binding_store_find(s,value->name,0,0))return FX_STATE;
    uint32_t index=atomic_load_explicit(&s->declarations,memory_order_relaxed);if(index>=FX_BINDING_IDENTITIES)return FX_BUDGET;
    FxBindingDeclaration *p;FxBindingDeclarationDirectory *dir=0,*old_dir=0;
    FxBindingDeclarationLeaf *part=0,*old_part=0;FxBindingDeclarationBlock *block=0;
    uint32_t chunk=0u;int created_block=0;
    if(index<FX_BINDING_INLINE_DECLARATIONS)p=&s->inline_declarations[index];
    else {
        uint32_t local=index-FX_BINDING_INLINE_DECLARATIONS;chunk=local/FX_BINDING_CHUNK;
        old_dir=dir=atomic_load_explicit(&s->directory,memory_order_acquire);
        if(!dir){dir=allocate(s,(uint32_t)sizeof(*dir));if(!dir)goto failed;memset(dir,0,sizeof(*dir));}
        old_part=part=atomic_load_explicit(&dir->leaf[chunk/FX_BINDING_DIRECTORY],memory_order_acquire);
        if(!part){part=allocate(s,(uint32_t)sizeof(*part));if(!part)goto failed;memset(part,0,sizeof(*part));}
        block=atomic_load_explicit(&part->block[chunk%FX_BINDING_DIRECTORY],memory_order_acquire);
        if(!block){block=allocate(s,(uint32_t)sizeof(*block));if(!block)goto failed;memset(block,0,sizeof(*block));created_block=1;}
        p=&block->entries[local%FX_BINDING_CHUNK];
    }
    memset(p,0,sizeof(*p));memcpy(p->name,value->name,bytes+1u);p->value=*value;p->value.name=p->name;p->index=index;p->hash=name_hash(p->name);
    if(index>=FX_BINDING_INLINE_DECLARATIONS) {
        if(created_block){fx_allocation_observe(s->observer,(uint32_t)sizeof(*block),FX_ALLOC_PUBLISHED);atomic_store_explicit(&part->block[chunk%FX_BINDING_DIRECTORY],block,memory_order_release);++s->declaration_blocks;}
        if(part!=old_part){fx_allocation_observe(s->observer,(uint32_t)sizeof(*part),FX_ALLOC_PUBLISHED);atomic_store_explicit(&dir->leaf[chunk/FX_BINDING_DIRECTORY],part,memory_order_release);}
        if(dir!=old_dir){fx_allocation_observe(s->observer,(uint32_t)sizeof(*dir),FX_ALLOC_PUBLISHED);atomic_store_explicit(&s->directory,dir,memory_order_release);}
    }
    publish_name(s,p);atomic_store_explicit(&s->declarations,index+1u,memory_order_release);*out=index;return FX_OK;
failed:
    if(part && part!=old_part)release(s,part,(uint32_t)sizeof(*part),0u);
    if(dir && dir!=old_dir)release(s,dir,(uint32_t)sizeof(*dir),0u);
    return s->memory.allocate?FX_BUDGET:FX_CAPABILITY;
}
FxResult fx_binding_store_destroy(FxBindingStore *s) {
    if(!s || !s->bucket_count)return FX_ARGUMENT;
    if(s->active)return FX_BUSY;
    FxBindingDeclarationDirectory *dir=atomic_load_explicit(&s->directory,memory_order_acquire);
    if(dir) {
        for(uint32_t i=0;i<FX_BINDING_DIRECTORY;++i) {
            FxBindingDeclarationLeaf *part=atomic_load_explicit(&dir->leaf[i],memory_order_relaxed);if(!part)continue;
            for(uint32_t j=0;j<FX_BINDING_DIRECTORY;++j){FxBindingDeclarationBlock *block=atomic_load_explicit(&part->block[j],memory_order_relaxed);if(block)release(s,block,(uint32_t)sizeof(*block),1u);}
            release(s,part,(uint32_t)sizeof(*part),1u);
        }
        release(s,dir,(uint32_t)sizeof(*dir),1u);
    }
    for(uint32_t i=0;i<s->chunks;++i)release(s,s->lease_chunks[i],(uint32_t)sizeof(FxBindingSlot)*FX_BINDING_CHUNK,1u);
    if(s->lease_chunks)release(s,s->lease_chunks,s->index_capacity*(uint32_t)sizeof(*s->lease_chunks),1u);
    if(s->buckets!=s->inline_buckets)release(s,s->buckets,s->bucket_count*8u,1u);
    memset(s,0,sizeof(*s));return FX_OK;
}
