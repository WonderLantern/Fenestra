#ifndef FENESTRA_NATIVE_UI_READER_H
#define FENESTRA_NATIVE_UI_READER_H
#include "fenestra_native_ui.h"
#include "fenestra_native_title.h"
#include "fenestra_ui_document.h"
#include "fenestra_text_page.h"
typedef struct FxNativeUiReaderProfile {FxNativeTitleProfile title;uintptr_t measure,bounds;uint16_t width,height;} FxNativeUiReaderProfile;
typedef struct FxReaderHistory {struct FxReaderHistory *previous;uint32_t offset;} FxReaderHistory;
typedef struct FxNativeUiReader {
    FxUiDocument *document;FxReaderHistory *history;
    FxTextPage work;uint16_t display[FX_TEXT_PAGE_UNITS];
    uint32_t opened,pending,measuring,offset,next,ready,version,bytes;FxResult result;
    uint32_t retry_pending; /* Retained navigation intent after history allocation failure. */
} FxNativeUiReader;
int fx_native_ui_reader_current(const FxNativeUi *);
FxResult fx_native_ui_reader_begin(FxNativeUi *);
void fx_native_ui_reader_close(FxNativeUi *);
FxResult fx_native_ui_reader_option(FxNativeUi *,uint32_t);
int fx_native_ui_reader_enabled(const FxNativeUi *,uint32_t);
FxResult fx_native_ui_reader_refresh(FxNativeUi *,uint32_t *);
int fx_native_ui_reader_text(FxNativeUi *,uint32_t,const uint16_t **,uint32_t *);
#endif
