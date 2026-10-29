#ifndef FENESTRA_RECORDS_H
#define FENESTRA_RECORDS_H
#include "fenestra.h"
/* Copied, finite record transformations. No pointers, callbacks or target
 * knowledge. All guards are checked before the first destination write. */
#define FX_RECORD_EDITS 64u
#define FX_RECORD_BYTES 512u
typedef struct { uint32_t offset,width;uint64_t before,after; } FxRecordEdit;
typedef struct { uint32_t abi,bytes,count,reserved;FxRecordEdit edit[FX_RECORD_EDITS]; } FxRecordProgram;
FxResult fx_record_check(const FxRecordProgram *);
FxResult fx_record_apply(const FxRecordProgram *,void *,uint32_t);
#endif
