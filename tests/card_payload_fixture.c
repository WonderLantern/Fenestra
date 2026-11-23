#include "fenestra_card_payload.h"
#include <assert.h>
#include <string.h>

enum { CAPACITY=32, FILE_CAPACITY=CAPACITY+FX_CARD_PAYLOAD_HEADER_BYTES };
typedef struct {
    uint8_t file[FILE_CAPACITY+1],scratch[FILE_CAPACITY+1],image[CAPACITY];
    uint32_t bytes,position,closed,released,entered,image_bytes,entry;
    uint32_t opens,time,ready_at;
    int missing,fail_mount,fail_close,removed;
} Fixture;

static FxCardMountState mounted(void *context) {
    Fixture *f=context;
    if(f->fail_mount || (f->removed && f->closed))return FX_CARD_MOUNT_FAILED;
    return f->time>=f->ready_at?FX_CARD_MOUNT_READY:FX_CARD_MOUNT_PENDING;
}

static uint64_t now(void *context) {
    return ((Fixture *)context)->time;
}

static FxResult wait_ms(void *context,uint32_t ms) {
    Fixture *f=context;
    assert(ms && ms<=25u);
    f->time+=ms;
    return FX_OK;
}

static void *allocate(void *context,uint32_t bytes) {
    Fixture *f=context;
    assert(bytes==sizeof(f->scratch));
    return f->scratch;
}

static void release(void *context,void *buffer) {
    Fixture *f=context;
    assert(f->closed==1u && buffer==f->scratch && !f->released);
    ++f->released;
}

static int32_t open_file(void *context,const char *path,uint32_t mode) {
    Fixture *f=context;
    assert(!strcmp(path,FX_CARD_PAYLOAD_PATH) && mode==FX_TEXT_READ);
    assert(f->time>=f->ready_at && !f->fail_mount && !f->opens);
    ++f->opens;
    return f->missing?(int32_t)UINT32_C(0x80000010):0;
}

static uint32_t read_file(void *context,int32_t fd,void *buffer,uint32_t maximum,uint32_t *actual) {
    Fixture *f=context;
    assert(fd==0 && !f->closed);
    *actual=f->bytes-f->position;
    if(*actual>maximum)*actual=maximum;
    memcpy(buffer,f->file+f->position,*actual);
    f->position+=*actual;
    return 0u;
}

static uint32_t close_file(void *context,int32_t fd) {
    Fixture *f=context;
    assert(fd==0 && !f->closed);
    ++f->closed;
    return f->fail_close?1u:0u;
}

static FxResult load_image(void *context,const void *file,uint32_t bytes) {
    Fixture *f=context;
    assert(f->closed==1u && !f->released);
    FxCardPayloadHeader header;
    FxResult result=fx_card_payload_parse(file,bytes,CAPACITY,&header);
    if(result==FX_OK) {
        memcpy(f->image,(const uint8_t *)file+sizeof(header),header.image_bytes);
        f->image_bytes=header.image_bytes;
        f->entry=header.entry_offset;
        ++f->entered;
    }
    return result;
}

static FxResult read_payload(Fixture *f) {
    FxCardBootIO io={.context=f,.mount_state=mounted,.clock_ms=now,.wait_ms=wait_ms,
        .allocate=allocate,.release=release,.files={.context=f,.open=open_file,.read=read_file,.close=close_file},
        .missing_file_error=UINT32_C(0x80000010)};
    return fx_card_boot_read(&io,FX_CARD_PAYLOAD_PATH,FX_CARD_PAYLOAD_HEADER_BYTES+4u,FILE_CAPACITY,
                             100u,100u,load_image,f);
}

static Fixture example(const uint32_t *image,uint32_t bytes,uint32_t entry) {
    Fixture f={.bytes=FX_CARD_PAYLOAD_HEADER_BYTES+bytes};
    FxCardPayloadHeader header={FX_CARD_PAYLOAD_MAGIC,FX_CARD_PAYLOAD_HEADER_BYTES,bytes,entry};
    memcpy(f.file,&header,sizeof(header));
    memcpy(f.file+sizeof(header),image,bytes);
    return f;
}

int main(void) {
    const uint32_t first[]={0xd65f03c0u};
    const uint32_t second[]={0xd503201fu,0xd65f03c0u};
    Fixture f=example(first,sizeof(first),0u);
    assert(read_payload(&f)==FX_OK && f.entered==1u && f.released==1u && f.entry==0u);
    assert(f.image_bytes==sizeof(first) && !memcmp(f.image,first,sizeof(first)));
    f=example(second,sizeof(second),4u);
    assert(read_payload(&f)==FX_OK && f.entered==1u && f.released==1u && f.entry==4u);
    assert(f.image_bytes==sizeof(second) && !memcmp(f.image,second,sizeof(second)));

    f=example(second,sizeof(second),2u);
    assert(read_payload(&f)==FX_BOUNDS && !f.entered && f.released);
    f=example(second,sizeof(second),8u);
    assert(read_payload(&f)==FX_BOUNDS && !f.entered && f.released);
    f=example(second,sizeof(second),4u);f.file[0]=0u;
    assert(read_payload(&f)==FX_INTEGRITY && !f.entered && f.released);
    f=example(second,sizeof(second),4u);--f.bytes;
    assert(read_payload(&f)==FX_BOUNDS && !f.entered && f.released);
    f=example(second,sizeof(second),4u);f.bytes=sizeof(f.file);
    assert(read_payload(&f)==FX_BOUNDS && !f.entered && f.released);
    f=example(first,sizeof(first),0u);f.ready_at=50u;
    assert(read_payload(&f)==FX_OK && f.entered && f.time==50u);
    f=example(first,sizeof(first),0u);f.ready_at=100u;
    assert(read_payload(&f)==FX_TIMEOUT && !f.opens && !f.entered);
    f=example(first,sizeof(first),0u);f.fail_mount=1;
    assert(read_payload(&f)==FX_IO && !f.opens && !f.entered);
    f=example(first,sizeof(first),0u);f.missing=1;
    assert(read_payload(&f)==FX_EMPTY && f.opens && !f.closed && !f.entered);
    f=example(first,sizeof(first),0u);f.fail_close=1;
    assert(read_payload(&f)==FX_CLOSE && f.released && !f.entered);
    f=example(first,sizeof(first),0u);f.removed=1;
    assert(read_payload(&f)==FX_IO && f.released && !f.entered);
    return 0;
}
