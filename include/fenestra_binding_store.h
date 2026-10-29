#ifndef FENESTRA_BINDING_STORE_H
#define FENESTRA_BINDING_STORE_H
#include "fenestra_module_index.h"
#include "fenestra_allocation_observer.h"
#include <stdatomic.h>

/* Internal owner storage; SDK messages remain pointer-free and unchanged. */
#define FX_BINDING_INLINE_LEASES 8u
#define FX_BINDING_INLINE_DECLARATIONS 4u
#define FX_BINDING_METADATA_BUDGET (1024u * 1024u)
#define FX_BINDING_IDENTITIES 65535u
#define FX_BINDING_CHUNK 16u
#define FX_BINDING_DIRECTORY 64u
typedef struct {
    const char *name;
    uint32_t kind,abi,version,flags,capabilities,bytes;
    uint64_t address;
    uint8_t original[16];
} FxNamedBinding;
/* cookie's ADDRESS is call-scoped. A provider retains its own state plus
 * owner/token, never a pointer to a reusable lease field after detach ACK. */
typedef struct {uint64_t token,cookie;uint32_t index,attached,panic_status;} FxBindingLease;
typedef struct {FxBindingLease value;uint32_t id,next_token,next_binding,next_free;} FxBindingSlot;
typedef struct FxBindingDeclaration FxBindingDeclaration;
struct FxBindingDeclaration {
    FxNamedBinding value;char name[64];uint32_t index,hash,bit,reserved;
    _Atomic uintptr_t child[2],collision;
};
typedef struct {FxBindingDeclaration entries[FX_BINDING_CHUNK];} FxBindingDeclarationBlock;
typedef struct { _Atomic(FxBindingDeclarationBlock *) block[FX_BINDING_DIRECTORY];} FxBindingDeclarationLeaf;
typedef struct { _Atomic(FxBindingDeclarationLeaf *) leaf[FX_BINDING_DIRECTORY];} FxBindingDeclarationDirectory;
typedef struct {
    FxIndexMemory memory;FxAllocationObserver observer;uint32_t budget,used,peak,active;
    FxBindingSlot inline_leases[FX_BINDING_INLINE_LEASES];
    FxBindingDeclaration inline_declarations[FX_BINDING_INLINE_DECLARATIONS];
    FxBindingSlot **lease_chunks;uint32_t chunks,index_capacity,free_lease,lease_capacity;
    uint32_t inline_buckets[32];uint32_t *buckets,bucket_count;
    _Atomic uint32_t declarations;
    _Atomic uintptr_t names;
    _Atomic(FxBindingDeclarationDirectory *) directory;
    uint32_t declaration_blocks;
} FxBindingStore;
/* Only one writer under the owner's binding_lock. Lease readers also hold
 * it; immutable declarations/find are safe for concurrent native callbacks.
 * Published owner, declarations, directory nodes and names never move. */
void fx_binding_store_init(FxBindingStore *,FxIndexMemory);
FxBindingLease *fx_binding_store_lease_at(const FxBindingStore *,uint32_t index);
FxBindingLease *fx_binding_store_lease(const FxBindingStore *,uint64_t token);
FxBindingLease *fx_binding_store_existing(const FxBindingStore *,uint32_t binding);
FxResult fx_binding_store_take(FxBindingStore *,uint64_t token,uint32_t binding,FxBindingLease **);
FxResult fx_binding_store_release(FxBindingStore *,FxBindingLease *);
FxResult fx_binding_store_declare(FxBindingStore *,const FxNamedBinding *,uint32_t *);
const FxNamedBinding *fx_binding_store_declaration(const FxBindingStore *,uint32_t index);
const FxNamedBinding *fx_binding_store_find(const FxBindingStore *,const char *,uint32_t *index,uint32_t *steps);
/* Trusted unpublished owner, or externally proven absence of ALL readers
 * and retained descriptor/callback references. Not a module unload API. */
FxResult fx_binding_store_destroy(FxBindingStore *);
#endif
