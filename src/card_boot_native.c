#include "card_boot_native.h"
#include "card_boot_profile.h"
#include "native_translation_impl.h"

#define NATIVE(address,type) ((type)(uintptr_t)(address))

FxCardMountState fx_card_boot_mount_state(void *unused) {
    (void)unused;
    uint32_t drive=NATIVE(FX_BOOT_DRIVE_INDEX,uint32_t (*)(const char *))("A:");
    if(drive>1u)return FX_CARD_MOUNT_PENDING;
    uint32_t status=NATIVE(FX_BOOT_DRIVE_STATUS,uint32_t (*)(uint32_t))(drive);
    if(status==0u)return FX_CARD_MOUNT_READY;
    if(status==UINT32_C(0x80001002) || status==UINT32_C(0x80001003))
        return FX_CARD_MOUNT_PENDING;
    return FX_CARD_MOUNT_FAILED;
}

uint64_t fx_card_boot_clock_ms(void *unused) {
    (void)unused;
    uint64_t ticks,frequency;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0":"=&r"(ticks),"=r"(frequency));
    if(!frequency || frequency>UINT64_MAX/1000u)return 0u;
    return (ticks/frequency)*1000u+((ticks%frequency)*1000u)/frequency;
}

FxResult fx_card_boot_wait_ms(void *unused,uint32_t ticks) {
    (void)unused;
    return NATIVE(FX_BOOT_DELAY,int32_t (*)(uint32_t))(ticks)?FX_IO:FX_OK;
}

void *fx_card_boot_allocate(void *unused,uint32_t bytes) {
    (void)unused;
    return NATIVE(FX_BOOT_ALLOCATE,void *(*)(uint32_t,uint32_t))(bytes,0u);
}

void fx_card_boot_release(void *unused,void *memory) {
    (void)unused;
    NATIVE(FX_BOOT_FREE,void (*)(void *))(memory);
}

int32_t fx_card_boot_open(void *unused,const char *path,uint32_t mode) {
    (void)unused;
    if(mode!=FX_TEXT_READ)return -1;
    return NATIVE(FX_BOOT_OPEN,int32_t (*)(const char *,uint32_t,uint32_t))(path,0u,0u);
}

uint32_t fx_card_boot_read_native(void *unused,int32_t fd,void *buffer,uint32_t bytes,uint32_t *actual) {
    (void)unused;
    return NATIVE(FX_BOOT_READ,uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))(fd,buffer,bytes,actual);
}

uint32_t fx_card_boot_close(void *unused,int32_t fd) {
    (void)unused;
    return NATIVE(FX_BOOT_CLOSE,uint32_t (*)(int32_t))(fd);
}

#define PHYS_MASK UINT64_C(0x0000fffffffff000)

static int32_t unmap(uintptr_t alias) {
    int32_t result=NATIVE(FX_BOOT_UNMAP,int32_t (*)(uintptr_t,uint64_t))(alias,FX_BOOT_PAGE);
    NATIVE(FX_BOOT_INVALIDATE_ALL,void (*)(void))();
    return result;
}

FxResult fx_card_boot_map_alias(uintptr_t address,uintptr_t alias) {
    if((address|alias)&(FX_BOOT_PAGE-1u))return FX_ARGUMENT;
    if(*(const volatile uint8_t *)(uintptr_t)(FX_BOOT_MMU_CONTEXT+0x38u))
        return FX_CAPABILITY;
    uint64_t read=fx_translation_probe(address,FX_TRANSLATE_EL1_READ);
    uint64_t physical=read&PHYS_MASK;
    /* Physical application DDR, independently verified on the supported
     * memory layout. No identity address, device/ROM or flash destination. */
    if((read&1u) || physical<UINT64_C(0xa00000000) || physical>=UINT64_C(0xa80000000) ||
       !(fx_translation_probe(address,FX_TRANSLATE_EL1_WRITE)&1u))return FX_CAPABILITY;
    if(!(fx_translation_probe(alias,FX_TRANSLATE_EL1_READ)&1u))return FX_BUSY;
    uint32_t attributes=0u;
    if(NATIVE(FX_BOOT_ATTRIBUTES,int32_t (*)(uintptr_t,uint32_t *))
        (FX_BOOT_MMU_CONTEXT,&attributes))return FX_CAPABILITY;
    if(NATIVE(FX_BOOT_MAP,int32_t (*)(uintptr_t,uint64_t,uint64_t,uint32_t))
        (alias,FX_BOOT_PAGE,physical,attributes))return FX_IO;
    NATIVE(FX_BOOT_INVALIDATE_ALL,void (*)(void))();
    uint64_t writable=fx_translation_probe(alias,FX_TRANSLATE_EL1_WRITE);
    if(!(writable&1u) && (writable&PHYS_MASK)==physical)return FX_OK;
    return unmap(alias)?FX_CLOSE:FX_CAPABILITY;
}

FxResult fx_card_boot_unmap_alias(uintptr_t alias) {
    return unmap(alias)?FX_CLOSE:FX_OK;
}

void fx_card_boot_clean(uintptr_t alias,uint32_t bytes) {
    uint64_t ctr;
    __asm__ volatile("mrs %0,ctr_el0":"=r"(ctr));
    uintptr_t line=(uintptr_t)4u<<((ctr>>16u)&15u);
    uintptr_t end=alias+bytes;
    for(uintptr_t p=alias&~(line-1u);p<end;p+=line)
        __asm__ volatile("dc cvau,%0"::"r"(p):"memory");
}

void fx_card_boot_sync_instructions(void) {
    __asm__ volatile("dsb ish; ic ialluis; dsb ish; isb":::"memory");
}
