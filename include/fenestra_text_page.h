#ifndef FENESTRA_TEXT_PAGE_H
#define FENESTRA_TEXT_PAGE_H
#include "fenestra_platform.h"
#define FX_TEXT_PAGE_UNITS 512u
typedef struct {uint16_t text[FX_TEXT_PAGE_UNITS];uint32_t units,next;} FxTextPage;
typedef FxResult (*FxPageMeasure)(void *,const uint16_t *,uint32_t,int32_t dimensions[2]);
/* Heap-owned work buffer. Measurement uses the actual native font/layout.
 * next is an input BYTE offset; inserted wrapping never consumes input.
 * CRLF/CR become one display newline. No code point or surrogate is split. */
FxResult fx_text_page(FxTextPage *,const char *,uint32_t bytes,uint32_t offset,
                      int32_t width,int32_t height,FxPageMeasure,void *);
#endif
