#ifndef FENESTRA_CRITICAL_H
#define FENESTRA_CRITICAL_H
#include "fenestra_platform.h"
/* core.critical/v1. One native call bracketed by the original CPU-owned
 * try-lock/unlock, in the same execution context and service request.
 * The caller supplies a live, initialized 16-byte native spinlock shared by
 * ALL relevant writers. A private lock does not exclude stock code or DMA.
 * The function must not sleep, switch CPU, manipulate this lock or IRQ state.
 * Pointers/code stay owned until return; no EL0 callback is invoked here. */
enum {FX_CRITICAL_QUERY=1u,FX_CRITICAL_CALL};
typedef struct {
    uint64_t lock,binding;
    uint32_t call_bytes,reserved;
    _Alignas(16) uint8_t call[sizeof(FxNativeCallFrame)];
} FxCriticalCall;
typedef struct {uint32_t abi,bytes,operations,lock_bytes;} FxCriticalInfo;
/* CALL: value[0..1] native integer returns; value[2] signed acquire result;
 * value[3] signed unlock result. Acquire -1009 -> FX_BUSY, no target call.
 * Other lock errors -> FX_IO. After acquisition unlock is attempted even
 * if the target faults the owner or its transport returns an error.
 * This is not exception unwinding or recovery from a nonreturning function.
 * AAPCS64 calls return the complete frame in data, after releasing the lock. */
_Static_assert(sizeof(FxCriticalCall)<=FX_SERVICE_BYTES,"critical call wire payload");
#endif
