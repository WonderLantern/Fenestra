#ifndef FENESTRA_Z8_C311_CARD_H
#define FENESTRA_Z8_C311_CARD_H
#include "fenestra_file_card.h"

/* Private platform-owned native fd; never supplied by a module. */
typedef struct {
    uint32_t magic,uncertain,native_error;
    int32_t fd;
} FxZ8CardScope;
FxResult fx_z8_card_files(FxTextFiles *,FxZ8CardScope *);
#endif
