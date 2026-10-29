#include "fenestra_video_configuration.h"
FxResult fx_video_configuration_check(const void *data,uint32_t bytes) {
    if(!data || bytes<sizeof(FxVideoConfigurationData))return FX_ARGUMENT;
    const FxVideoConfigurationData *p=data;
    if((p->abi!=FX_VIDEO_CONFIGURATION_ABI && p->abi!=FX_VIDEO_LEGACY_ABI) || p->bytes!=bytes ||
       !p->count || p->count>FX_VIDEO_MAX_MODES || p->reserved)return FX_ARGUMENT;
    if(p->abi==FX_VIDEO_LEGACY_ABI) {
        if(p->count>FX_VIDEO_MODES || bytes!=sizeof(FxVideoConfiguration))return FX_ARGUMENT;
    } else if(bytes!=fx_video_configuration_bytes(p->count))return FX_ARGUMENT;
    for(uint32_t i=0;i<p->count;++i) {
        const FxVideoConfigurationMode *m=&p->modes[i];
        FxResult r=fx_capture_profile_check(&m->profile);if(r!=FX_OK)return r;
        if(!m->label[0] || !m->native_codec || !m->native_family ||
           m->row_template>=109u || !m->stock_row || m->quick_template>=17u)return FX_ARGUMENT;
        uint32_t n=0;while(n<sizeof(m->label) && m->label[n]) {
            if((uint8_t)m->label[n]<32u || (uint8_t)m->label[n]>126u)return FX_ARGUMENT;
            ++n;
        }
        if(n==sizeof(m->label))return FX_ARGUMENT;
        for(uint32_t j=0;j<i;++j)if(p->modes[j].profile.mode_id==m->profile.mode_id)return FX_ARGUMENT;
        for(uint32_t j=0;j<FX_VIDEO_RECORDS;++j) {
            r=fx_record_check(&m->records[j]);if(r!=FX_OK)return r;
        }
    }
    return FX_OK;
}
