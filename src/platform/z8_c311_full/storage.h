#ifndef FENESTRA_Z8_STORAGE_H
#define FENESTRA_Z8_STORAGE_H
#include "fenestra_startup_files.h"

/* Bind the startup journal and every module path to one volume for this boot.
 * A: is the original XQD/CFexpress slot; B: is the original SD slot. */
typedef struct {
    _Atomic uint32_t selected;
    uint32_t native_error, attempts;
    FxResult latched_error;
    FxStartupFileProfile profile;
    char prefix[13];
    char paths[3][32];
} FxZ8Storage;
void fx_z8_storage_init(FxZ8Storage *,const FxStartupFileProfile *);
FxResult fx_z8_storage_select(FxZ8Storage *,
    uint32_t (*)(const char *,int32_t *),uint32_t (*)(int32_t));
char fx_z8_storage_drive(const FxZ8Storage *);
/* Published by the boot-owned menu state after selection. Zero before ready. */
char fx_z8_card_drive(void);
#endif
