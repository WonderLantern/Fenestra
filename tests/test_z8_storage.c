#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "storage.h"
static unsigned present, opens, closes, close_error, publish_failed_fd;
static char queried[8][32], created[64], last_read[64], removed[64];
static unsigned char flag[64];
static unsigned flag_present, corrupt_config;
static void copy(char *to,size_t capacity,const char *from) {
    size_t n=strlen(from);assert(n<capacity);memcpy(to,from,n+1u);
}
static uint32_t open_dir(const char *path,int32_t *fd) {
    unsigned slot=path[0]=='A'?0u:1u;
    assert(!strcmp(path+1,":\\FENESTRA"));
    assert(opens<8u);copy(queried[opens++],32u,path);
    if(!(present&(1u<<slot))) {
        if(publish_failed_fd)*fd=7;
        return 0x80000010u;
    }
    *fd=(int32_t)(20u+slot);return 0u;
}
static uint32_t close_dir(int32_t fd){assert(fd==20 || fd==21);++closes;return close_error;}
static const FxStartupFileProfile profile={.paths={
    "A:\\FENESTRA\\boot-z8.flag","A:\\FENESTRA\\startup-z8.0","A:\\FENESTRA\\startup-z8.1"}};
static void reset(void) {
    present=opens=closes=close_error=publish_failed_fd=flag_present=corrupt_config=0u;
    memset(queried,0,sizeof(queried));created[0]=last_read[0]=removed[0]=0;
}
static FxResult journal_read(void *context,uint32_t id,uint8_t *data,uint32_t cap,uint32_t *bytes) {
    FxZ8Storage *s=context;assert(cap==65u);copy(last_read,sizeof(last_read),s->profile.paths[id]);*bytes=0;
    if(id==FX_STARTUP_FLAG && flag_present){memcpy(data,flag,64);*bytes=64;return FX_OK;}
    if(id==FX_STARTUP_CFG0 && corrupt_config){memset(data,0,64);*bytes=64;return FX_OK;}
    return FX_EMPTY;
}
static FxResult journal_create(void *context,uint32_t id,const uint8_t *data,uint32_t bytes) {
    FxZ8Storage *s=context;assert(id==FX_STARTUP_FLAG && bytes==64u && !flag_present);
    copy(created,sizeof(created),s->profile.paths[id]);memcpy(flag,data,64);flag_present=1;return FX_OK;
}
static FxResult journal_remove(void *context,uint32_t id) {
    FxZ8Storage *s=context;assert(id==FX_STARTUP_FLAG && flag_present);
    copy(removed,sizeof(removed),s->profile.paths[id]);flag_present=0;return FX_OK;
}
int main(void) {
    FxZ8Storage s;FxStartup journal;
    reset();present=2u;fx_z8_storage_init(&s,&profile);
    assert(!fx_z8_storage_drive(&s));
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_OK);
    assert(fx_z8_storage_drive(&s)=='B' && opens==2u && closes==1u);
    assert(!strcmp(s.prefix,"B:\\FENESTRA\\"));
    for(unsigned i=0;i<3u;++i)assert(s.profile.paths[i][0]=='B' && profile.paths[i][0]=='A');
    FxStartupIO io={&s,journal_read,journal_create,journal_remove};
    fx_startup_init_profile(&journal,0,0x00080311u);
    assert(fx_startup_inspect(&journal,&io)==FX_OK && journal.phase==FX_STARTUP_READY);
    assert(fx_startup_arm(&journal,&io,123u)==FX_OK);
    assert(!strcmp(created,"B:\\FENESTRA\\boot-z8.flag"));
    fx_startup_running(&journal);
    assert(fx_startup_clean_shutdown(&journal,&io,1)==FX_OK);
    assert(!strcmp(removed,created) && !flag_present);

    reset();present=1u;fx_z8_storage_init(&s,&profile);
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_OK);
    assert(fx_z8_storage_drive(&s)=='A' && opens==1u && closes==1u);
    reset();present=3u;fx_z8_storage_init(&s,&profile);
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_OK);
    assert(fx_z8_storage_drive(&s)=='A' && opens==1u);
    present=2u;
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_OK);
    assert(fx_z8_storage_drive(&s)=='A' && opens==1u); /* no mid-boot failover */

    reset();fx_z8_storage_init(&s,&profile);
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_IO);
    assert(!fx_z8_storage_drive(&s) && closes==0u);
    present=2u;
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_OK && fx_z8_storage_drive(&s)=='B');
    corrupt_config=1u;fx_startup_init_profile(&journal,0,0x00080311u);
    assert(fx_startup_inspect(&journal,&io)==FX_INTEGRITY);
    assert(journal.phase==FX_STARTUP_IO_ERROR && !created[0]);
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_OK && opens==4u);

    reset();present=3u;close_error=0x80000005u;fx_z8_storage_init(&s,&profile);
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_CLOSE);
    assert(!fx_z8_storage_drive(&s) && opens==1u && closes==1u);
    close_error=0u;
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_CLOSE && opens==1u);
    reset();present=2u;publish_failed_fd=1u;fx_z8_storage_init(&s,&profile);
    assert(fx_z8_storage_select(&s,open_dir,close_dir)==FX_CLOSE && opens==1u && !closes);
    puts("PASS: SD and CF selection, pinned journal lifecycle, retry, corrupt records and uncertain ownership");
    return 0;
}
