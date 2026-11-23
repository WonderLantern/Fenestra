#ifndef FENESTRA_CARD_BOOT_NATIVE_H
#define FENESTRA_CARD_BOOT_NATIVE_H

#include "fenestra_card_payload.h"

#define FX_BOOT_CONTROL_ADDRESS 0x401ff000
#define FX_BOOT_ALIAS_ADDRESS   0x70000000
#define FX_BOOT_PAGE            4096u

FxCardMountState fx_card_boot_mount_state(void *);
uint64_t fx_card_boot_clock_ms(void *);
FxResult fx_card_boot_wait_ms(void *,uint32_t);
void *fx_card_boot_allocate(void *,uint32_t);
void fx_card_boot_release(void *,void *);
int32_t fx_card_boot_open(void *,const char *,uint32_t);
uint32_t fx_card_boot_read_native(void *,int32_t,void *,uint32_t,uint32_t *);
uint32_t fx_card_boot_close(void *,int32_t);

/* Only borrow writable aliases for application DDR. */
FxResult fx_card_boot_map_alias(uintptr_t address,uintptr_t alias);
FxResult fx_card_boot_unmap_alias(uintptr_t alias);
void fx_card_boot_clean(uintptr_t alias,uint32_t bytes);
void fx_card_boot_sync_instructions(void);
#endif
