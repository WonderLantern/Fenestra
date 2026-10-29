#ifndef FENESTRA_TEXT_H
#define FENESTRA_TEXT_H
#include "fenestra.h"
#include "fenestra_file_observer.h"

#define FX_TEXT_PATH "A:\\FENESTRA\\fenestra.txt"
#define FX_TEXT_CONTENT "Fenestra / WonderLantern\r\nTarget: Nikon ZR C1.10\r\nLoader: Active\r\n"
enum { FX_TEXT_IDLE,FX_TEXT_WRITING,FX_TEXT_SAVED,FX_TEXT_FAILED };
enum { FX_TEXT_CREATE=1,FX_TEXT_READ=2 };
typedef struct {
    uint32_t magic,version,state,result,native_error,bytes,attempts,step;
} FxTextReport;
typedef struct {
    void *context;
    int32_t (*open)(void *,const char *,uint32_t);
    uint32_t (*write)(void *,int32_t,const void *,uint32_t,uint32_t *);
    uint32_t (*read)(void *,int32_t,void *,uint32_t,uint32_t *);
    uint32_t (*close)(void *,int32_t);
} FxTextFiles;
/* Creates only this fixed file, refuses replacement, closes, then reads back.
 * Caller must admit this operation only after explicit local activation. */
FxResult fx_text_write(const FxTextFiles *,FxTextReport *);
FxResult fx_text_write_observed(const FxTextFiles *,FxTextReport *,FxFileObserver);
/* Per-operation ownership. Never exposed to EL0 modules. */
typedef struct { int32_t fd; uint32_t mode,uncertain; } FxC110FileScope;
void fx_c110_text_files(FxTextFiles *,FxC110FileScope *);
/* C1.10 registry/mount readiness errors; never classify arbitrary I/O as retryable. */
int fx_c110_card_not_ready(uint32_t native_error);
#endif
