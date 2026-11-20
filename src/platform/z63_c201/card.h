#ifndef FENESTRA_Z63_C201_CARD_H
#define FENESTRA_Z63_C201_CARD_H
#include "fenestra_file_card.h"

/* Private platform-owned native fd; never supplied by a module. */
typedef struct {
    uint32_t magic,uncertain,native_error;
    int32_t fd;
} FxZ63CardScope;
FxResult fx_z63_card_files(FxTextFiles *,FxZ63CardScope *);
#endif
