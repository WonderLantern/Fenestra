/* Ordinary, model-neutral .wlm: 250 ms of 440 Hz triangle-wave PCM.
 * No hardware address, native callback, SIMD, decoder or core rebuild per tone. */
#include "fenestra_audio_sdk.h"
static FxSdkClient client;
static uint64_t binding;
static FxAudioInfo info;
static uint32_t stage,submitted,next_frames,oscillator;
static int16_t pcm[4096];

enum {APP,OPEN,QUERY,CONFIGURE,FIRST,START,WRITE,DRAIN,CLOSE,FINISH,DONE};
static FxResult submit(const FxServiceModule *api) {
    switch(stage) {
        case APP:return fx_sdk_lifecycle(&client,api,FX_LIFE_CONFIGURE,FX_MODULE_APP);
        case OPEN:return fx_sdk_open_instance(&client,api,FX_AUDIO_OUTPUT_BINDING,FX_AUDIO_ABI);
        case QUERY:return fx_sdk_audio_query(&client,api,binding);
        case CONFIGURE: {
            FxAudioConfig config={FX_AUDIO_ABI,sizeof(config),info.sample_rate,1u,FX_AUDIO_S16_LE,
                                  info.max_queue_frames<8192u?info.max_queue_frames:8192u,FX_AUDIO_GAIN_ONE/4u,0u};
            return fx_sdk_audio_configure(&client,api,binding,&config);
        }
        case FIRST:case WRITE: {
            uint32_t remaining=info.sample_rate/4u-submitted;
            next_frames=remaining<4096u?remaining:4096u;
            if(next_frames>info.max_submit_frames)next_frames=info.max_submit_frames;
            if(next_frames>info.max_queue_frames)next_frames=info.max_queue_frames;
            /* Rebuild from acknowledged position on BUSY; retries never advance
             * the oscillator or reuse a buffer while its request is pending. */
            uint32_t phase=oscillator;
            for(uint32_t i=0u;i<next_frames;++i) {
                uint32_t ramp=(uint32_t)((uint64_t)phase*65536u/info.sample_rate);
                int32_t triangle=ramp<32768u?(int32_t)ramp*2-32768:98303-(int32_t)ramp*2;
                pcm[i]=(int16_t)triangle;
                phase=(phase+440u)%info.sample_rate;
            }
            return fx_sdk_audio_write(&client,api,binding,pcm,next_frames);
        }
        case START:return fx_sdk_audio_control(&client,api,binding,FX_AUDIO_START,0u);
        case DRAIN:return fx_sdk_audio_control(&client,api,binding,FX_AUDIO_DRAIN,0u);
        case CLOSE:return fx_sdk_platform(&client,api,FX_BIND_CLOSE,binding,0u,0u,0,0u);
        case FINISH:return fx_sdk_lifecycle(&client,api,FX_LIFE_FINISH,0u);
        default:return FX_OK;
    }
}
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base || base->bytes!=sizeof(FxServiceModule))return 1;
    const FxServiceModule *api=(const FxServiceModule *)base;
    if(api->service_abi!=FX_SERVICE_ABI)return 2;
    if(client.pending) {
        FxServiceReply reply;FxResult result=fx_sdk_take(&client,api,&reply);
        if(result==FX_BUSY)return 0;
        if(result!=FX_OK)return 3;
        if(reply.result==FX_BUSY)return submit(api)==FX_OK?0:4;
        /* Older cores may not expose the binding. Unsupported audio ends
         * normally; it must not panic a camera just because it cannot play. */
        if(stage==OPEN && reply.result==FX_CAPABILITY)stage=FINISH;
        else if(reply.result!=FX_OK)return 5;
        else switch(stage) {
            case APP:stage=OPEN;break;
            case OPEN:binding=reply.value[0];stage=QUERY;break;
            case QUERY:
                if(reply.bytes!=sizeof(info))return 6;
                for(uint32_t i=0u;i<sizeof(info);++i)((uint8_t *)&info)[i]=reply.data[i];
                if(info.abi!=FX_AUDIO_ABI || info.bytes!=sizeof(info))return 7;
                stage=(info.capabilities&FX_AUDIO_CAP_OUTPUT)?CONFIGURE:CLOSE;break;
            case CONFIGURE:stage=FIRST;break;
            case FIRST:case WRITE:
                submitted+=next_frames;
                oscillator=(uint32_t)((uint64_t)submitted*440u%info.sample_rate);
                stage=stage==FIRST?START:(submitted==info.sample_rate/4u?DRAIN:WRITE);break;
            case START:stage=submitted==info.sample_rate/4u?DRAIN:WRITE;break;
            case DRAIN:stage=CLOSE;break;
            case CLOSE:stage=FINISH;break;
            case FINISH:stage=DONE;break;
            default:break;
        }
    }
    return submit(api)==FX_OK?0:8;
}
