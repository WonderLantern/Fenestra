#include "fenestra_records.h"
FxResult fx_record_check(const FxRecordProgram *p) {
    if(!p || p->abi!=1u || p->reserved || !p->count || p->count>FX_RECORD_EDITS ||
       !p->bytes || p->bytes>FX_RECORD_BYTES)return FX_ARGUMENT;
    for(uint32_t i=0;i<p->count;++i) {
        const FxRecordEdit *e=&p->edit[i];
        if((e->width!=1u && e->width!=2u && e->width!=4u && e->width!=8u) ||
           e->offset>p->bytes || e->width>p->bytes-e->offset ||
           (e->width<8u && ((e->before|e->after)>>(8u*e->width))))return FX_BOUNDS;
        for(uint32_t j=0;j<i;++j) {
            const FxRecordEdit *q=&p->edit[j];
            if(e->offset<q->offset+q->width && q->offset<e->offset+e->width)return FX_ARGUMENT;
        }
    }
    return FX_OK;
}
FxResult fx_record_apply(const FxRecordProgram *p,void *out,uint32_t bytes) {
    if(!out)return FX_ARGUMENT;
    if(!p)return FX_EMPTY;
    FxResult r=fx_record_check(p);if(r!=FX_OK)return r;
    if(bytes!=p->bytes)return FX_ARGUMENT;
    uint8_t *b=out;
    for(uint32_t i=0;i<p->count;++i) {
        const FxRecordEdit *e=&p->edit[i];uint64_t v=0;
        for(uint32_t j=0;j<e->width;++j)v|=(uint64_t)b[e->offset+j]<<(8u*j);
        if(v!=e->before)return FX_INTEGRITY;
    }
    for(uint32_t i=0;i<p->count;++i) {
        const FxRecordEdit *e=&p->edit[i];
        for(uint32_t j=0;j<e->width;++j)b[e->offset+j]=(uint8_t)(e->after>>(8u*j));
    }
    return FX_OK;
}
