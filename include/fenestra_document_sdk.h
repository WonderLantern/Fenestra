#ifndef FENESTRA_DOCUMENT_SDK_H
#define FENESTRA_DOCUMENT_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_ui_document.h"
/* Asynchronous copied-message API, like every other SDK service. Open
 * core.ui.document/v1 first; take each reply before submitting the next chunk.
 * BEGIN returns an upload token, COMMIT a published version. READ requires
 * that version, so replacement cannot silently combine two documents. */
static inline FxResult fx_sdk_document(FxSdkClient *client,const FxServiceModule *api,
    uint64_t binding,uint32_t operation,uint32_t control,uint64_t token,
    uint32_t offset,uint32_t bytes,const void *text) {
    if(operation>FX_DOCUMENT_READ)return FX_ARGUMENT;
    if(operation==FX_DOCUMENT_QUERY)
        return fx_sdk_platform(client,api,FX_BIND_CALL,binding,operation,0u,0,0u);
    FxUiDocumentRequest request={.control=control,.offset=offset,.bytes=bytes,.token=token};
    if(operation!=FX_DOCUMENT_WRITE)
        return fx_sdk_platform(client,api,FX_BIND_CALL,binding,operation,0u,&request,sizeof(request));
    if(bytes>FX_SERVICE_BYTES-sizeof(request) || (bytes && !text))return FX_ARGUMENT;
    uint8_t payload[FX_SERVICE_BYTES];const uint8_t *header=(const uint8_t *)&request,*source=text;
    for(uint32_t i=0;i<sizeof(request);++i)payload[i]=header[i];
    for(uint32_t i=0;i<bytes;++i)payload[sizeof(request)+i]=source[i];
    return fx_sdk_platform(client,api,FX_BIND_CALL,binding,operation,0u,payload,(uint32_t)sizeof(request)+bytes);
}
#endif
