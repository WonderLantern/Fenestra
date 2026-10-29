#ifndef FENESTRA_Z9_C532_CARD_H
#define FENESTRA_Z9_C532_CARD_H
#include "fenestra_file_card.h"

/* Private platform-owned native fd; never supplied by a module. */
typedef struct {
    uint32_t magic,uncertain,native_error;
    int32_t fd;
} FxZ9CardScope;
FxResult fx_z9_card_files(FxTextFiles *,FxZ9CardScope *);
#endif
