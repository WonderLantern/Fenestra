/* Z8 port derived from upstream src/platform/z9_c532/card.h.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#ifndef FENESTRA_Z8_C311_FULL_CARD_H
#define FENESTRA_Z8_C311_FULL_CARD_H
#include "fenestra_file_card.h"
#include "storage.h"

/* Private platform-owned native fd; never supplied by a module. */
typedef struct {
    uint32_t magic,uncertain,native_error;
    int32_t fd;
} FxZ8CardScope;
FxResult fx_z8_card_files(FxTextFiles *,FxZ8CardScope *);
#endif
