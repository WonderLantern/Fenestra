#ifndef FENESTRA_HOOKS_H
#define FENESTRA_HOOKS_H
#include "fenestra_platform.h"
/* core.hooks/v1: synchronous interception of an aligned native RAM function
 * pointer slot, AAPCS64 integer/pointer x0..x7 -> x0. Executable trampolines
 * belong to core; callback policy is copied data, never an EL0 code pointer.
 * Slot must be an owner-open writable TABLE; no ROM instruction rewriting. */
#define FX_HOOK_STEPS 32u
#define FX_HOOK_GATES_PER_PAGE 128u
#define FX_HOOK_PAGE_BYTES 4096u
#define FX_HOOK_BUDGET (8u*1024u*1024u)
enum { FX_INTERCEPT_QUERY,FX_INTERCEPT_BEGIN,FX_INTERCEPT_APPEND,FX_INTERCEPT_COMMIT,FX_INTERCEPT_STATUS };
/* Optional immutable native ABI failure return, configured before COMMIT.
 * Data is one u64; absent configuration keeps legacy zero. */
enum {FX_INTERCEPT_FAILURE_RETURN=5u,FX_INTERCEPT_RESOURCES=6u,FX_INTERCEPT_CAPACITY=7u};
/* RESOURCES retains its v1 prefix/meaning: total/free describe currently
 * allocated stable gates, NOT the largest possible budget-dependent count. */
typedef struct {uint32_t abi,bytes,page_bytes,gates_per_page,pages,failed_pages,budget,used,peak,reserved;} FxHookCapacity;
/* Approximate global snapshot; concurrent transitions need not sum to one instant. Retained
 * slots include restored callbacks with possible late native callers. */
typedef struct {uint32_t abi,bytes,total,free,reserved,unpublished,installed,retained;
    uint64_t storage_bytes,active_calls;} FxHookResources;
typedef struct {uint32_t abi,bytes,operations,reserved;} FxHookInfo;
enum { FX_VM_CONSTANT=1u,FX_VM_MOVE,FX_VM_ADD,FX_VM_SUBTRACT,FX_VM_AND,FX_VM_OR,
       FX_VM_XOR,FX_VM_SHIFT_LEFT,FX_VM_SHIFT_RIGHT,FX_VM_LOAD,FX_VM_STORE,
       FX_VM_EQUAL,FX_VM_SKIP_ZERO,FX_VM_CALL,FX_VM_RETURN };
/* reg0..7: original args, reg8: native result (zero before original),
 * reg9..15: scratch. Before and after share one frame. Exactly one original
 * automatic call unless RETURN short-circuits it; revoke forwards unchanged. LOAD/STORE width 1/2/4/8, address=reg[a]+value.
 * SKIP_ZERO skips value subsequent instructions; no backward jumps/loops.
 * CALL.value is an owner-open integer FUNCTION handle, resolved and retained
 * at COMMIT. CALL passes reg0..7 and stores x0 in dst. RETURN ends the callback
 * with reg8, including skipping the automatic original call when used before.
 * A native CALL may block: the module must obey the intercepted context ABI. */
typedef struct {uint32_t op,dst,a,b;uint64_t value;} FxHookInstruction;
typedef struct {
    uint32_t abi,bytes,before_count,after_count;
    uint64_t table,offset,expected;
    FxHookInstruction before[FX_HOOK_STEPS],after[FX_HOOK_STEPS];
} FxHookProgram;
/* Cells and immutable executable pages have reboot lifetime. A published
 * cell is never reused, even after native pointer restoration and drain. */
typedef struct { _Atomic uintptr_t slot;_Atomic uint32_t state,storage;
    _Atomic uint64_t gate;} FxHookCell;
typedef struct FxHookPage FxHookPage;
struct FxHookPage {FxHookPage *next;uint8_t *code;uint32_t ready,reserved;
    FxHookCell cells[FX_HOOK_GATES_PER_PAGE];};
typedef struct { _Atomic(FxHookPage *) pages;_Atomic uint32_t growing,used,peak;
    FxAllocationObserver page_observer;FxAllocationObserver (*owner_account)(FxPlatformOwner *);} FxHookRegistry;
/* Native backend seals an owned full page, preserving memory attributes.
 * Failure with retained=1 means permissions could not be restored: keep the
 * allocation pinned and never expose a gate from that page. */
FxResult fx_native_hook_page(FxPlatformOwner *,uint8_t *,FxHookCell *,uint32_t *retained);
#if defined(__aarch64__)
void fx_hook_page_encode(uint8_t *,FxHookCell *);
#endif
uint64_t fx_hook_invoke(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,FxHookCell *);
FxHookRegistry *fx_native_hooks(void);
FxResult fx_hook_program_check(const FxHookProgram *);
FxResult fx_hook_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);
#endif
