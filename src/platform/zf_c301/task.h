#ifndef FX_ZF_C301_TASK_H
#define FX_ZF_C301_TASK_H
#include "fenestra.h"

/* Experimental raw native task requests. A caller must establish original
 * code identity, task/IRQ context, stack ownership and native policy before
 * supplying the Zf C3.01 functions at 0x40185064 and 0x401855e0. This is not
 * a complete task lifecycle or a usable camera binding. */
typedef struct {
    uint32_t attributes,reserved;
    uintptr_t argument,entry;
    uint32_t priority,reserved2;
    uint64_t stack_bytes;
    uintptr_t stack;
    uint32_t native_mode,affinity;
} FxZfNativeTaskRequest;

typedef struct {
    int32_t (*create)(const FxZfNativeTaskRequest *);
    int32_t (*start)(int32_t);
} FxZfNativeTaskOps;

typedef struct {
    void (*entry)(void *);
    void *argument;
    void *stack;
    uint32_t stack_bytes;
    uint32_t priority;
    uint32_t native_mode;
} FxZfTaskConfig;

enum {
    FX_ZF_TASK_EMPTY,
    FX_ZF_TASK_CREATE_FAILED,
    FX_ZF_TASK_CREATED,
    FX_ZF_TASK_START_FAILED,
    FX_ZF_TASK_START_ACCEPTED
};
typedef struct {
    uint32_t phase;
    int32_t id;
    int32_t start_result;
    uint32_t reserved;
    uintptr_t stack;
    uint64_t stack_bytes;
} FxZfTaskScope;

/* Scope and its backing storage must begin zero and be used by one serialized
 * owner. Scope, config and ops must be naturally aligned; the two native
 * backend entry points must be four-byte aligned. Scope, config, ops and the
 * complete caller-owned stack must not overlap; entry and argument start
 * addresses must be outside the stack. The
 * stack must be exclusive, writable, inactive storage, separate from the
 * caller's active stack; this API cannot establish those properties.
 * The caller must keep the complete entry code and argument object valid,
 * exclusive of the stack, until native execution has actually stopped.
 * The stack remains caller-owned in every state. A positive ID remains
 * retained even if start fails; no stop, delete, retry or release is implied.
 * START_ACCEPTED means the native request returned zero, not that the entry
 * ran or that resources can later be reclaimed. Priority and native mode are
 * explicit inputs, not inferred from a different camera. */
FxResult fx_zf_task_start(FxZfTaskScope *,const FxZfTaskConfig *,const FxZfNativeTaskOps *);
#endif
