#ifndef FENESTRA_UI_DOCUMENT_H
#define FENESTRA_UI_DOCUMENT_H
#include "fenestra_platform.h"
#define FX_UI_DOCUMENT_BYTES 65536u
/* core.ui.document/v1. Text bytes exclude NUL; chunks may split UTF-8 units.
 * COMMIT validates the complete document and atomically replaces its version. */
enum {FX_DOCUMENT_QUERY,FX_DOCUMENT_BEGIN,FX_DOCUMENT_WRITE,FX_DOCUMENT_COMMIT,
      FX_DOCUMENT_ABORT,FX_DOCUMENT_CLEAR,FX_DOCUMENT_INFO,FX_DOCUMENT_READ};
typedef struct {uint32_t control,offset,bytes,reserved;uint64_t token;} FxUiDocumentRequest;
typedef struct {uint32_t abi,bytes,maximum,documents,chunk_bytes,reserved;} FxUiDocumentInfo;
typedef struct FxUiDocument FxUiDocument;
/* Core/UI only. A pin is an immutable version independent of later uploads.
 * Every successful acquire owns one reference; release consumes it exactly once. */
FxResult fx_ui_document_acquire(FxPlatformOwner *,uint32_t,FxUiDocument **);
void fx_ui_document_release(FxUiDocument *);
const char *fx_ui_document_text(const FxUiDocument *);
uint32_t fx_ui_document_bytes(const FxUiDocument *);
uint32_t fx_ui_document_version(const FxUiDocument *);
FxResult fx_ui_document_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,
                                uint64_t *,const FxServiceRequest *,FxServiceReply *);
#endif
