#ifndef FENESTRA_FILE_CARD_H
#define FENESTRA_FILE_CARD_H
#include "fenestra_text.h"
#include "fenestra_file_observer.h"

/* Snapshot an explicitly requested native file before publishing any module.
 * Owns no controller, filesystem mount, DMA queue or native task. The Loader
 * may consume this exact buffer in place; arbitrary partial overlap is not an
 * IO contract. Owner invalidates the snapshot before releasing its storage. */
typedef struct {
    uint8_t *buffer;
    const char *module_path;
    uint32_t bytes,position,opened,valid;
    uint32_t native_error,close_error,read_calls;
    FxResult result;
} FxFileCard;
FxResult fx_file_card_load(FxFileCard *,const FxTextFiles *,const char *native_path,
                          const char *module_path,uint8_t *,uint32_t capacity,uint32_t expected_bytes);
FxResult fx_file_card_load_observed(FxFileCard *,const FxTextFiles *,const char *,const char *,uint8_t *,uint32_t,uint32_t,FxFileObserver);
void fx_file_card_io(FxFileCard *,FxCardIO *);
#endif
