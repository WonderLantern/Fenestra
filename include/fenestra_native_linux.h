#ifndef FENESTRA_NATIVE_LINUX_H
#define FENESTRA_NATIVE_LINUX_H
#include <stdint.h>
#include <stdatomic.h>
#include "fenestra_allocation_observer.h"
#include "fenestra_transfers.h"
#define FX_USB_LOG 8192u
typedef struct {
    uint8_t *allocation,*shared;
    uint64_t nonce,heartbeat,session;
    uint32_t generation,sequence,hooked;
    _Atomic uint32_t log_lock,command_running,command_completions,dropped;
    _Atomic uint32_t launch_possible;
    uint64_t log_end;
    uint8_t log[FX_USB_LOG];
    FxAllocationObserver observer;
    FxTransfers transfers;
} FxLinuxBridge;
#include "fenestra_ptp.h"
typedef struct {
    FxLinuxBridge *(*state)(void);
    FxPtp *(*ptp)(void);
    const volatile uint8_t *ready;
    const volatile uint64_t *pool_address;
    const volatile uint32_t *pool_size;
    _Atomic uintptr_t *callback;
    const volatile uintptr_t *queue;
    uint64_t pool,queue_block_end;
    uint32_t pool_bytes;
    const char *bootstrap_token;
    uint32_t (*receive)(uint32_t,const uint8_t *);
    uint8_t *(*allocate)(uint32_t);
    void (*release)(void *);
    int32_t (*offset)(const void *);
    int32_t (*try_lock)(void *);
    int32_t (*unlock)(void *);
    int32_t (*enqueue)(uintptr_t,const void *,uint32_t);
    int32_t (*notify)(uintptr_t);
    uint32_t release_opcode;
    uint32_t release_zero_offset;
} FxNativeLinuxProfile;
extern const FxNativeLinuxProfile fx_native_linux_profile;
/* Trusted initialization before this bridge becomes callable. */
void fx_linux_account(FxLinuxBridge *,FxAllocationObserver);
void fx_linux_account_packets(FxLinuxBridge *,FxIndexMemory,FxAllocationObserver requests,FxAllocationObserver metadata);
void fx_linux_tick(uint64_t session,uint32_t generation,int enabled);
void fx_linux_revoke(void);
uint32_t fx_linux_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
int fx_linux_quiesced(void);
uint32_t fx_linux_ready(void);
#endif
