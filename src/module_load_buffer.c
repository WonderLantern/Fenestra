#include "fenestra_module_load_buffer.h"

FxResult fx_module_load_buffer_account(FxModuleLoadBuffer *s,FxAccountRow *row) {
    if(!s)return FX_ARGUMENT;
    if(s->data || s->retained)return FX_BUSY;
    s->account=row;return FX_OK;
}
FxResult fx_module_load_buffer_open(FxModuleLoadBuffer *s,FxIndexMemory memory,uint32_t package) {
    if(!s || !memory.allocate || !memory.release || package<=128u || package>FX_CARD_LIMIT)return FX_ARGUMENT;
    if(s->data || s->retained)return FX_BUSY;
    uint8_t *data=memory.allocate(memory.context,package+1u);
    if(!data)return FX_BUDGET;
    s->memory=memory;s->data=data;s->bytes=package+1u;
    if(s->bytes>s->peak)s->peak=s->bytes;
    if(s->account)(void)fx_account_acquire(s->account,s->bytes,1u,0u);
    return FX_OK;
}
FxResult fx_module_load_buffer_finish(FxModuleLoadBuffer *s,FxFileCard *file,
                                     const FxCardReceipt *receipt,FxResult native_result) {
    if(!s)return FX_ARGUMENT;
    if(s->retained)return FX_CLOSE;
    if(!s->data)return FX_OK;
    FxResult result=FX_OK;
    if(native_result==FX_CLOSE || (file && file->close_error) ||
       (receipt && receipt->cleanup!=FX_OK))result=FX_CLOSE;
    else if(file && file->opened)result=FX_BUSY;
    else if(file && file->buffer && file->buffer!=s->data)result=FX_INTEGRITY;
    if(result!=FX_OK){s->retained=1u;if(s->account)(void)fx_account_retain(s->account,s->bytes,1u);return result;}
    if(file){file->valid=0u;file->buffer=0;file->bytes=file->position=0u;}
    uint8_t *data=s->data;uint32_t bytes=s->bytes;
    s->data=0;s->bytes=0u;s->released_bytes+=bytes;++s->releases;
    s->memory.release(s->memory.context,data);
    if(s->account)(void)fx_account_release(s->account,bytes,1u,0u);
    return FX_OK;
}
