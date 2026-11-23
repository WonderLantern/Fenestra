#include "card_boot_native.h"
#include "card_boot_profile.h"
#include "fenestra_memory.h"
#include <stdatomic.h>

#define NATIVE(address,type) ((type)(uintptr_t)(address))
#define PHASE_R ((const _Atomic uint32_t *)(uintptr_t)FX_BOOT_CONTROL_ADDRESS)
#define PHASE_W ((_Atomic uint32_t *)(uintptr_t)FX_BOOT_ALIAS_ADDRESS)

enum { STARTED=1u, SKIPPED=2u, HANDED_OFF=3u, STACK_BYTES=16384u, MOUNT_MS=5000u, LOAD_MS=5000u };
typedef struct {
    uint32_t attributes,reserved;
    uintptr_t context,entry;
    uint32_t priority,reserved2;
    uint64_t stack_bytes;
    uintptr_t stack;
    uint32_t processor,affinity;
} BootTask;
_Static_assert(sizeof(BootTask)==56,"original task ABI");

extern uint64_t fx_card_boot_original(void *message,uint32_t synchronous);

static FxResult copy_at(uint32_t offset,const void *data,uint32_t bytes) {
    if(!data || !bytes || (offset&(FX_BOOT_PAGE-1u)) ||
       offset>=FX_CARD_PAYLOAD_CAPACITY || bytes>FX_CARD_PAYLOAD_CAPACITY-offset)return FX_BOUNDS;
    const uintptr_t destination=FX_CARD_PAYLOAD_ADDRESS+offset;
    const uintptr_t alias=FX_BOOT_ALIAS_ADDRESS+FX_BOOT_PAGE;
    uint32_t mapped=0u;
    FxResult result=FX_OK;
    while(mapped<bytes) {
        result=fx_card_boot_map_alias(destination+mapped,alias+mapped);
        if(result!=FX_OK)break;
        mapped+=FX_BOOT_PAGE;
    }
    if(result==FX_OK) {
        memcpy((void *)alias,data,bytes);
        fx_card_boot_clean(alias,bytes);
        fx_card_boot_sync_instructions();
        if(memcmp((const void *)destination,data,bytes))result=FX_INTEGRITY;
    }
    while(mapped) {
        mapped-=FX_BOOT_PAGE;
        FxResult cleanup=fx_card_boot_unmap_alias(alias+mapped);
        if(result==FX_OK)result=cleanup;
    }
    return result;
}

static FxResult stage(void *context,const void *data,uint32_t bytes) {
    if(atomic_load_explicit(PHASE_R,memory_order_acquire)!=STARTED)return FX_INTERRUPTED;
    FxCardPayloadHeader *header=context;
    FxResult result=fx_card_payload_parse(data,bytes,FX_CARD_PAYLOAD_CAPACITY,header);
    if(result!=FX_OK)return result;
    return copy_at(0u,(const uint8_t *)data+sizeof(*header),header->image_bytes);
}

static void worker(void *unused) {
    (void)unused;
    const FxCardBootIO io={
        .mount_state=fx_card_boot_mount_state,.clock_ms=fx_card_boot_clock_ms,
        .wait_ms=fx_card_boot_wait_ms,.allocate=fx_card_boot_allocate,.release=fx_card_boot_release,
        .files={.open=fx_card_boot_open,.read=fx_card_boot_read_native,.close=fx_card_boot_close},
        .missing_file_error=UINT32_C(0x80000010)
    };
    FxCardPayloadHeader header;
    FxResult result=fx_card_boot_read(&io,FX_CARD_PAYLOAD_PATH,FX_CARD_PAYLOAD_HEADER_BYTES+4u,
        FX_CARD_PAYLOAD_CAPACITY+FX_CARD_PAYLOAD_HEADER_BYTES,MOUNT_MS,LOAD_MS,stage,&header);
    uint32_t expected=STARTED;
    if(result!=FX_OK) {
        (void)atomic_compare_exchange_strong(PHASE_W,&expected,SKIPPED);
        return;
    }
    if(!atomic_compare_exchange_strong_explicit(PHASE_W,&expected,HANDED_OFF,
        memory_order_release,memory_order_relaxed))return;

    const FxCardPayloadBoot boot={
        .abi=FX_CARD_PAYLOAD_ABI,.bytes=sizeof(boot),.io=&io,.target=FX_BOOT_TARGET,
        .application=fx_boot_application_sha,.image_bytes=header.image_bytes,
        .capacity=FX_CARD_PAYLOAD_CAPACITY,.copy=copy_at
    };
    NATIVE(FX_CARD_PAYLOAD_ADDRESS+header.entry_offset,FxCardPayloadEntry)(&boot);
}

static void start(void) {
    if(atomic_load_explicit(PHASE_R,memory_order_acquire))return;
    if(fx_card_boot_mount_state(0)==FX_CARD_MOUNT_FAILED)return;
    if(fx_card_boot_map_alias(FX_BOOT_CONTROL_ADDRESS,FX_BOOT_ALIAS_ADDRESS)!=FX_OK)return;
    uint32_t expected=0u;
    if(!atomic_compare_exchange_strong(PHASE_W,&expected,STARTED))return;
    void *allocation=fx_card_boot_allocate(0,STACK_BYTES+15u);
    if(!allocation){atomic_store(PHASE_W,SKIPPED);return;}
    const uintptr_t stack=((uintptr_t)allocation+15u)&~(uintptr_t)15u;
    const BootTask task={.entry=(uintptr_t)worker,.priority=28u,.stack_bytes=STACK_BYTES,.stack=stack,
        .processor=1u,.affinity=UINT32_MAX};
    const int32_t id=NATIVE(FX_BOOT_TASK_CREATE,int32_t (*)(const BootTask *))(&task);
    if(id<1)fx_card_boot_release(0,allocation);
    /* Once created, the original RTOS may retain its task stack. */
    if(id<1 || NATIVE(FX_BOOT_TASK_RESUME,int32_t (*)(int32_t))(id))atomic_store(PHASE_W,SKIPPED);
}

uint64_t fx_card_boot_dispatch(void *message,uint32_t synchronous) {
    const uint32_t event=message?*(const uint32_t *)((const uint8_t *)message+32u):UINT32_MAX;
    if(event==FX_BOOT_SHUTDOWN && atomic_load_explicit(PHASE_R,memory_order_acquire)==STARTED) {
        uint32_t expected=STARTED;
        (void)atomic_compare_exchange_strong(PHASE_W,&expected,SKIPPED);
    }
    const uint64_t result=fx_card_boot_original(message,synchronous);
    if(event==FX_BOOT_APP_READY)start();
    return result;
}
