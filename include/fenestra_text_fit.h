#ifndef FENESTRA_TEXT_FIT_H
#define FENESTRA_TEXT_FIT_H
#include "fenestra_platform.h"
/* Presentation buffers only. measure must invalidate its native text cache
 * before measuring the current buffer; a negative result means unavailable. */
typedef int32_t (*FxTextMeasure)(void *context,uint32_t column);
FxResult fx_text_fit_pair(uint16_t label[FX_UI_TEXT],uint16_t value[FX_UI_TEXT],
                          int32_t width,int32_t gap,FxTextMeasure measure,void *context);
#endif
