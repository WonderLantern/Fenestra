#include "fenestra_card_payload.h"
#include "fenestra_memory.h"

enum { POLL_MS=25u, READ_CHUNK=65536u };

FxResult fx_card_payload_parse(const void *file,uint32_t bytes,uint32_t capacity,FxCardPayloadHeader *out) {
    if(!file || !out || bytes<sizeof(*out))return FX_ARGUMENT;
    FxCardPayloadHeader header;
    memcpy(&header,file,sizeof(header));
    if(header.magic!=FX_CARD_PAYLOAD_MAGIC || header.header_bytes!=sizeof(header))return FX_INTEGRITY;
    if(header.image_bytes!=bytes-sizeof(header) || header.image_bytes<4u ||
       header.image_bytes>capacity || (header.entry_offset&3u) || header.entry_offset>header.image_bytes-4u)
        return FX_BOUNDS;
    *out=header;
    return FX_OK;
}

static uint64_t elapsed(const FxCardBootIO *io,uint64_t began) {
    return io->clock_ms(io->context)-began;
}

static FxResult wait_for_mount(const FxCardBootIO *io,uint32_t timeout) {
    const uint64_t began=io->clock_ms(io->context);
    /* Also bound polling if the native clock stops advancing. */
    const uint32_t attempts=(timeout-1u)/POLL_MS+1u;
    for(uint32_t i=0;i<attempts;++i) {
        const FxCardMountState state=io->mount_state(io->context);
        const uint64_t spent=elapsed(io,began);
        if(spent>=timeout)return FX_TIMEOUT;
        if(state==FX_CARD_MOUNT_READY)return FX_OK;
        if(state!=FX_CARD_MOUNT_PENDING)return FX_IO;
        const uint32_t remaining=timeout-(uint32_t)spent;
        const FxResult result=io->wait_ms(io->context,remaining<POLL_MS?remaining:POLL_MS);
        if(result!=FX_OK)return result;
    }
    return FX_TIMEOUT;
}

static FxResult read_body(const FxCardBootIO *io,uint32_t minimum,uint32_t maximum,int32_t fd,
                          uint64_t began,uint32_t timeout,uint8_t *buffer,uint32_t *size) {
    uint32_t loaded=0u;
    for(uint32_t i=0;i<FX_READ_CALL_LIMIT;++i) {
        if(elapsed(io,began)>=timeout)return FX_TIMEOUT;
        if(io->mount_state(io->context)!=FX_CARD_MOUNT_READY)return FX_IO;
        if(elapsed(io,began)>=timeout)return FX_TIMEOUT;
        uint32_t requested=maximum+1u-loaded,actual=0u;
        if(requested>READ_CHUNK)requested=READ_CHUNK;
        if(io->files.read(io->files.context,fd,buffer+loaded,requested,&actual))return FX_IO;
        if(actual>requested || actual>maximum-loaded)return FX_BOUNDS;
        if(!actual) {
            *size=loaded;
            return loaded>=minimum?FX_OK:FX_BOUNDS;
        }
        loaded+=actual;
    }
    return FX_IO;
}

FxResult fx_card_boot_read(const FxCardBootIO *io,const char *path,uint32_t minimum,uint32_t maximum,
    uint32_t mount_timeout,uint32_t load_timeout,
    FxResult (*accept)(void *,const void *,uint32_t),void *context) {
    if(!io || !path || !minimum || minimum>maximum ||
       maximum>FX_CARD_PAYLOAD_CAPACITY+FX_CARD_PAYLOAD_HEADER_BYTES ||
       !mount_timeout || !load_timeout || !io->mount_state || !io->clock_ms || !io->wait_ms ||
       !io->allocate || !io->release || !accept || !io->files.open || !io->files.read || !io->files.close)
        return FX_ARGUMENT;
    FxResult result=wait_for_mount(io,mount_timeout);
    if(result!=FX_OK)return result;

    const uint64_t began=io->clock_ms(io->context);
    const int32_t fd=io->files.open(io->files.context,path,FX_TEXT_READ);
    if(fd<0)return (uint32_t)fd==io->missing_file_error?FX_EMPTY:FX_IO;

    uint8_t *buffer=0;
    uint32_t loaded=0u;
    if(elapsed(io,began)>=load_timeout)result=FX_TIMEOUT;
    else {
        buffer=io->allocate(io->context,maximum+1u);
        result=buffer?read_body(io,minimum,maximum,fd,began,load_timeout,buffer,&loaded):FX_BUDGET;
    }
    if(io->files.close(io->files.context,fd) && result==FX_OK)result=FX_CLOSE;
    if(result==FX_OK) {
        if(elapsed(io,began)>=load_timeout)result=FX_TIMEOUT;
        else if(io->mount_state(io->context)!=FX_CARD_MOUNT_READY)result=FX_IO;
        else if(elapsed(io,began)>=load_timeout)result=FX_TIMEOUT;
        else result=accept(context,buffer,loaded);
    }
    if(buffer)io->release(io->context,buffer);
    return result;
}
