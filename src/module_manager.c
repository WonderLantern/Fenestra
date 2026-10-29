#include "fenestra_module_manager.h"
#include "fenestra_memory.h"
static void account_locked(FxModuleManager *m) {
    if(atomic_load(&m->accounting_ready)==2u){
        (void)fx_module_index_account(&m->index,m->index_observer);
        m->inspect_observer=m->package_observer;
        atomic_store(&m->accounting_ready,3u);
    }
}
static int acquire(FxModuleManager *m) {
    uint32_t zero=0;
    if(!atomic_compare_exchange_strong(&m->lock,&zero,1u))return 0;
    account_locked(m);return 1;
}
static void release(FxModuleManager *m){
    account_locked(m);if(atomic_load(&m->shutdown))fx_module_index_retain(&m->index);
    atomic_store(&m->lock,0u);
    /* A publisher can queue after the first check but lose its try-lock
     * before unlock. The post-unlock SC check closes that handoff window. */
    uint32_t zero=0u;
    if(atomic_load(&m->accounting_ready)==2u && atomic_compare_exchange_strong(&m->lock,&zero,1u)){
        account_locked(m);if(atomic_load(&m->shutdown))fx_module_index_retain(&m->index);
        atomic_store(&m->lock,0u);
    }
}
FxResult fx_module_manager_account(FxModuleManager *m,FxAllocationObserver indexes,FxAllocationObserver packages) {
    if(!m)return FX_ARGUMENT;
    uint32_t zero=0u;
    if(atomic_compare_exchange_strong(&m->accounting_ready,&zero,1u)){
        m->index_observer=indexes;m->package_observer=packages;atomic_store(&m->accounting_ready,2u);
    }else{
        if(zero==1u)return FX_BUSY;
        if(m->index_observer.context!=indexes.context || m->index_observer.event!=indexes.event ||
           m->package_observer.context!=packages.context || m->package_observer.event!=packages.event)return FX_STATE;
    }
    if(acquire(m))release(m);
    return FX_OK;
}
static void copy_text(char *out,const char *in,uint32_t cap) {
    uint32_t i=0;for(;i+1u<cap && in[i];++i)out[i]=in[i];out[i]=0;
}
static int filename(const char *file,char id[16],uint32_t *enabled) {
    if(!fx_service_name(file,32u,1))return 0;
    uint32_t n=0;while(n<32u && file[n])++n;
    *enabled=!(n>9u && !memcmp(file+n-9u,".disabled",9u));
    if(!*enabled)n-=9u;
    if(n<5u || n>19u || memcmp(file+n-4u,".wlm",4u))return 0;
    memset(id,0,16u);memcpy(id,file,n-4u);
    return fx_service_name(id,16u,0);
}
static FxResult inspect(FxModuleManager *m,FxManagedModule *e,uint64_t size) {
    FxModule module;
    FxResult r=fx_module_directory_inspect_accounted(&m->io.directory,e->file,size,m->firmware,m->capabilities,
        m->io.memory,m->scratch,sizeof(m->scratch),&module,e->digest,m->inspect_observer);
    if(r!=FX_OK)return r;
    e->valid=1u;e->error=0u;e->bytes=(uint32_t)size;e->image_bytes=module.bytes;
    e->required=(uint32_t)module.required_capabilities;return FX_OK;
}

static FxResult put(FxModuleManager *m,uint32_t id,uint32_t kind,const char *label,const char *text,uint32_t value,uint32_t flags) {
    /* These are serialized by manager.lock. Keep copied wire messages off
     * Nikon's original GUI/task stack; do not stack multi-KiB temporaries. */
    FxServiceRequest *q=&m->ui_request;fx_ram_zero(q,sizeof(*q));
    q->operation=FX_S_UI;q->bytes=sizeof(FxUiControl);
    FxUiControl *c=(FxUiControl *)(void *)q->data;
    c->abi=FX_PLATFORM_ABI;c->bytes=sizeof(*c);c->id=id;c->parent=id==1u?0u:1u;
    c->kind=kind;c->flags=flags;c->value=value;
    copy_text(c->label,label,sizeof(c->label));copy_text(c->text,text,sizeof(c->text));
    /* A retry must not manufacture revisions for unchanged earlier rows. */
    q->arg[0]=FX_UI_GET;q->arg[1]=id;q->bytes=0u;
    FxResult r=fx_platform_dispatch(&m->ui,q,&m->ui_reply);
    if(r==FX_OK && fx_ram_equal(c,m->ui_reply.data,sizeof(*c))){
        m->last_ui_revision=(uint32_t)m->ui_reply.value[0];m->last_ui_serial=m->ui_reply.value[2];return FX_OK;}
    if(r!=FX_OK && r!=FX_EMPTY)return r;
    q->arg[0]=FX_UI_PUT;q->arg[1]=0u;q->bytes=sizeof(*c);
    r=fx_platform_dispatch(&m->ui,q,&m->ui_reply);
    if(r==FX_OK){m->last_ui_revision=(uint32_t)m->ui_reply.value[0];m->last_ui_serial=m->ui_reply.value[1];}
    return r;
}
static uint32_t decimal(char *out,uint32_t value) {
    char digits[10];uint32_t count=0u;
    do{digits[count++]=(char)('0'+value%10u);value/=10u;}while(value);
    for(uint32_t i=0;i<count;++i)out[i]=digits[count-i-1u];
    return count;
}
static char folded(char c){return c>='A' && c<='Z'?(char)(c+('a'-'A')):c;}
static int matches(const FxManagedModule *e,const char *query) {
    if(!query[0])return 1;
    for(uint32_t i=0u;i<sizeof(e->id) && e->id[i];++i){
        uint32_t n=0u;
        while(query[n] && i+n<sizeof(e->id) && e->id[i+n] && folded(query[n])==folded(e->id[i+n]))++n;
        if(!query[n])return 1;
    }
    return 0;
}
static uint32_t matched_count(const FxModuleManager *m) {
    if(!m->filter[0])return m->count;
    uint32_t count=0u;for(uint32_t i=0u;i<m->count;++i)count+=(uint32_t)matches(&m->entries[i],m->filter);return count;
}
static uint32_t pages_for(uint32_t count){return count?(count-1u)/FX_MANAGED_ROWS+1u:1u;}
static void invalidate_rows(FxModuleManager *m) {
    /* A desired projection can change while copied UI publication is BUSY.
     * No old queued toggle may escape through a not-yet-published mapping. */
    for(uint32_t i=0u;i<FX_MANAGED_ROWS;++i)m->rows[i].revision=0u;
}
static void project(FxModuleManager *m) {
    m->view_start=m->page*FX_MANAGED_ROWS;
    m->view_count=m->filtered_count-m->view_start;
    if(m->view_count>FX_MANAGED_ROWS)m->view_count=FX_MANAGED_ROWS;
    if(!m->filter[0]){
        for(uint32_t i=0u;i<m->view_count;++i)m->view_slots[i]=m->view_start+i;
    }else{
        uint32_t position=0u,used=0u;
        for(uint32_t i=0u;i<m->count && used<m->view_count;++i)if(matches(&m->entries[i],m->filter)){
            if(position++>=m->view_start)m->view_slots[used++]=i;
        }
    }
}
static void publish(FxModuleManager *m) {
    m->ui_pending=1u;
    m->filtered_count=matched_count(m);uint32_t pages=pages_for(m->filtered_count);
    if(m->page>=pages)m->page=pages-1u;
    const char *status=m->uncertain?"Restart":m->loading?"Loading":m->error==FX_BUDGET?"Resource limit":m->error?"Card error":!m->scanned?"Not scanned":
        m->filter[0] && !m->filtered_count?"No matches":m->activated?"Next boot":"On Activate";
    char title[FX_UI_TEXT]="Modules";
    uint32_t at=7u;
    if(pages>1u){title[at++]=' ';at+=decimal(title+at,m->page+1u);title[at++]='/';at+=decimal(title+at,pages);}
    copy_text(title+at," - ",sizeof(title)-at);at+=3u;copy_text(title+at,status,sizeof(title)-at);
    if(put(m,1u,FX_UI_PAGE,title,"",0u,FX_UI_VISIBLE|FX_UI_ENABLED|FX_UI_ROOT)!=FX_OK)return;
    /* Keep stable IDs/slots, but move status into the title: Search + Refresh
     * + ten modules + Previous/Next still fit the fourteen-child native view. */
    if(put(m,2u,FX_UI_ACTION,"Changes",status,0u,0u)!=FX_OK)return;
    if(put(m,3u,FX_UI_ACTION,"Refresh","",0u,m->uncertain?FX_UI_VISIBLE:3u)!=FX_OK)return;
    project(m);
    for(uint32_t i=0;i<FX_MANAGED_ROWS;++i) {
        FxManagedModule empty={0};FxManagedModule *e=i<m->view_count?&m->entries[m->view_slots[i]]:&empty;
        const char *why=!e->present?"Not on card":e->error==FX_CAPABILITY?"Incompatible":e->error?"Invalid module":!e->valid?"Not checked":
            m->activated && e->current!=e->enabled?"After restart":"";
        /* Invalid files can still be disabled. Never silently load them. */
        /* Card membership and session residency are different: removing a
         * file never unloads its code or callbacks from the current session. */
        uint32_t flags=(e->present || (m->activated && e->current))?FX_UI_VISIBLE:0u;
        if(e->present && !m->uncertain && !m->loading && (!e->error || e->enabled))flags|=FX_UI_ENABLED;
        char label[FX_UI_TEXT]={0};copy_text(label,e->id,sizeof(label));
        if(e->error) {
            uint32_t n=0;while(label[n])++n;
            copy_text(label+n,e->error==FX_CAPABILITY?" (incompatible)":" (invalid)",sizeof(label)-n);
        }
        if(!label[0])copy_text(label," ",sizeof(label));
        int remapped=!m->rows[i].revision || memcmp(m->rows[i].id,e->id,16u)!=0;
        if(put(m,4u+i,FX_UI_SWITCH,label,why,e->enabled,flags)!=FX_OK)return;
        if(remapped){memcpy(m->rows[i].id,e->id,16u);m->rows[i].revision=m->last_ui_revision;
            m->rows[i].serial_floor=m->last_ui_serial;}
    }
    if(put(m,FX_MANAGER_PREVIOUS,FX_UI_ACTION,"Previous page","",0u,pages>1u?(FX_UI_VISIBLE|(m->page?FX_UI_ENABLED:0u)):0u)!=FX_OK)return;
    if(put(m,FX_MANAGER_NEXT,FX_UI_ACTION,"Next page","",0u,pages>1u?(FX_UI_VISIBLE|(m->page+1u<pages?FX_UI_ENABLED:0u)):0u)!=FX_OK)return;
    if(put(m,FX_MANAGER_SEARCH,FX_UI_EDIT,"Search modules",m->filter,36u,FX_UI_VISIBLE|FX_UI_ENABLED)!=FX_OK)return;
    if(!m->search_ready || m->search_revision!=m->last_ui_revision){
        m->search_revision=m->last_ui_revision;m->search_serial_floor=m->last_ui_serial;m->search_ready=1u;
    }
    FxServiceRequest *q=&m->ui_request;fx_ram_zero(q,sizeof(*q));
    q->operation=FX_S_UI;q->arg[0]=FX_UI_MOVE;q->arg[1]=FX_MANAGER_SEARCH;q->arg[2]=2u;
    if(fx_platform_dispatch(&m->ui,q,&m->ui_reply)!=FX_OK)return;
    m->ui_pending=0u;
}
static FxResult filter_locked(FxModuleManager *m,const char *query,uint32_t bytes) {
    if(!query || !bytes || bytes>FX_MANAGER_FILTER_BYTES || query[bytes-1u])return FX_ARGUMENT;
    for(uint32_t i=0u;i+1u<bytes;++i)if((uint8_t)query[i]<32u || (uint8_t)query[i]>126u)return FX_ARGUMENT;
    char next[FX_MANAGER_FILTER_BYTES]={0};memcpy(next,query,bytes);
    if(!memcmp(m->filter,next,sizeof(next)))return FX_OK;
    memcpy(m->filter,next,sizeof(next));
    m->page=0u;m->search_ready=0u;invalidate_rows(m);publish(m);return FX_OK;
}
FxResult fx_module_manager_filter(FxModuleManager *m,const char *query,uint32_t bytes) {
    if(!m || atomic_load(&m->shutdown))return FX_STATE;
    if(!acquire(m))return FX_BUSY;
    FxResult r=atomic_load(&m->shutdown)?FX_STATE:filter_locked(m,query,bytes);release(m);return r;
}
FxResult fx_module_manager_init(FxModuleManager *m,const FxPlatform *p,const FxModuleManagerIO *io,int active) {
    if(!m || !p || !io || !io->directory.open || !io->directory.next || !io->directory.close || !io->directory.read || !io->rename)return FX_ARGUMENT;
    memset(m,0,sizeof(*m));m->io=*io;m->capabilities=FX_SDK_CAPS;m->activated=active?1u:0u;m->loading=m->activated;
    memcpy(m->firmware,p->module_identity,32u);
    FxResult r=fx_module_index_init(&m->index,io->memory,(uint32_t)sizeof(FxManagedModule),
        io->index_budget?io->index_budget:FX_MODULE_INDEX_BUDGET);
    if(r!=FX_OK)return r;
    r=fx_platform_init(&m->ui,p,65535u,1u,FX_SDK_CAPS);
    if(r==FX_OK)publish(m);
    return r;
}
static FxResult scan_locked(FxModuleManager *m) {
    if(m->uncertain)return FX_CLOSE;
    FxModuleIndex next_index;
    FxResult cloned=fx_module_index_init(&next_index,m->index.memory,m->index.stride,m->index.budget);
    if(cloned!=FX_OK)return cloned;
    (void)fx_module_index_account(&next_index,m->index.observer);
    cloned=fx_module_index_reserve(&next_index,m->count);
    if(cloned!=FX_OK){m->error=(uint32_t)cloned;publish(m);return cloned;}
    if(m->count)memcpy(next_index.records,m->entries,(size_t)m->count*sizeof(*m->entries));
    next_index.count=m->count;fx_module_index_rehash(&next_index);
    FxManagedModule *next=next_index.records;uint32_t count=m->count;
    for(uint32_t i=0;i<count;++i)next[i].present=0u;
    FxModuleDirectory *io=&m->io.directory;FxResult r=io->open(io->context);uint32_t end=0;
    if(r==FX_EMPTY){r=FX_OK;end=1u;}
    else if(r==FX_OK) {
        for(uint32_t attempt=0;attempt<65536u;++attempt) {
            FxModuleDirectoryEntry item={0};r=io->next(io->context,&item);
            if(r==FX_EMPTY){r=FX_OK;end=1u;break;}
            if(r!=FX_OK)break;
            if(item.attributes&0x18u)continue;
            /* FAT/exFAT and Nikon rename may return upper-case names. One
             * case-insensitive identity must survive that round trip. */
            for(uint32_t i=0;i<32u && item.name[i];++i)
                if(item.name[i]>='A' && item.name[i]<='Z')item.name[i]=(char)(item.name[i]+('a'-'A'));
            char id[16];uint32_t enabled;
            if(!filename(item.name,id,&enabled))continue;
            uint32_t slot=0;FxResult found=fx_module_index_find(&next_index,id,&slot);
            if(found==FX_EMPTY){r=fx_module_index_add(&next_index,id,&slot);if(r!=FX_OK)break;}
            else if(found!=FX_OK){r=found;break;}
            next=next_index.records;count=next_index.count;
            FxManagedModule *e=&next[slot];
            if(e->present){r=FX_INTEGRITY;break;} /* .wlm + .wlm.disabled conflict */
            uint32_t current=e->current;
            memset(e,0,sizeof(*e));memcpy(e->id,id,16u);memcpy(e->file,item.name,32u);
            e->present=1u;e->enabled=enabled;
            e->current=m->activated?(m->io.current?m->io.current(m->io.context,id):current):enabled;
            e->bytes=item.bytes<=UINT32_MAX?(uint32_t)item.bytes:0u;
            /* Parked modules consume an identity, not package IO or an execution owner.
             * Enabling performs full preflight; Activate independently revalidates all enabled files. */
            FxResult valid=item.bytes<=FX_MODULE_HEADER || item.bytes>FX_NATIVE_MODULE_PACKAGE_LIMIT?FX_BOUNDS:FX_OK;
            if(valid==FX_OK && enabled)valid=inspect(m,e,item.bytes);
            e->valid=enabled && valid==FX_OK;e->error=(uint32_t)valid;
            if(valid==FX_CLOSE){r=valid;m->uncertain=1u;break;}
        }
        FxResult closed=io->close(io->context);
        if(closed!=FX_OK){r=closed;m->uncertain=1u;}
    }
    if(r==FX_OK && !end)r=FX_BUDGET;
    if(r==FX_OK){
        uint32_t kept=0u;
        for(uint32_t i=0;i<count;++i)if(next[i].present || (m->activated && next[i].current)){
            if(kept!=i){next[kept]=next[i];}++kept;}
        next_index.count=kept;fx_module_index_rehash(&next_index);
        (void)fx_module_index_shrink(&next_index); /* Failure retains a valid, larger snapshot. */
        FxModuleIndex old=m->index;m->index=next_index;next_index=old;
        m->entries=m->index.records;m->count=m->index.count;m->scanned=1u;
    }
    fx_module_index_destroy(&next_index);
    m->error=(uint32_t)r;publish(m);return r;
}
FxResult fx_module_manager_scan(FxModuleManager *m) {
    if(!m || atomic_load(&m->shutdown))return FX_STATE;
    if(!acquire(m))return FX_BUSY;
    FxResult r=atomic_load(&m->shutdown)?FX_STATE:scan_locked(m);
    release(m);return r;
}
static FxResult enable_locked(FxModuleManager *m,uint32_t slot,uint32_t enabled) {
    FxResult r=FX_OK;
    if(slot>=m->count || !m->entries[slot].present)r=FX_EMPTY;
    else if(m->uncertain)r=FX_CLOSE;
    else if(m->loading)r=FX_BUSY;
    else {
        FxManagedModule *e=&m->entries[slot];
        if(enabled && e->error)r=(FxResult)e->error;
        else if(enabled!=e->enabled) {
            if(enabled) {
                uint32_t selected=0u;
                for(uint32_t i=0u;i<m->count;++i)selected+=m->entries[i].present && m->entries[i].enabled;
                if(selected>=FX_MODULE_OWNER_LIMIT)r=FX_BUDGET;
                else r=inspect(m,e,e->bytes); /* Re-read after scan; changed files are not trusted. */
                if(r!=FX_OK) {
                    if(r!=FX_BUDGET){e->valid=0u;e->error=(uint32_t)r;}
                    if(r==FX_CLOSE)m->uncertain=1u;
                    m->error=(uint32_t)r;publish(m);return r;
                }
            }
            char file[32]={0};copy_text(file,e->id,16u);uint32_t n=0;while(file[n])++n;
            copy_text(file+n,enabled?".wlm":".wlm.disabled",32u-n);
            r=m->io.rename(m->io.context,e->file,file);
            if(r==FX_OK){memcpy(e->file,file,32u);e->enabled=enabled;if(!m->activated)e->current=enabled;}
            /* Native close/rename uncertainty retains potentially live handles.
             * Recovery is reboot, not a rescan of those same handles. */
            else if(r==FX_CLOSE)m->uncertain=1u;
        }
    }
    m->error=(uint32_t)r;publish(m);return r;
}
FxResult fx_module_manager_enable(FxModuleManager *m,uint32_t slot,uint32_t enabled) {
    if(!m || enabled>1u || atomic_load(&m->shutdown))return FX_ARGUMENT;
    if(!acquire(m))return FX_BUSY;
    FxResult r=atomic_load(&m->shutdown)?FX_STATE:enable_locked(m,slot,enabled);
    release(m);return r;
}
FxResult fx_module_manager_freeze(FxModuleManager *m) {
    if(!m)return FX_OK;
    if(atomic_load(&m->shutdown))return FX_STATE;
    if(!acquire(m))return FX_BUSY;
    if(atomic_load(&m->shutdown)){release(m);return FX_STATE;}
    if(!m->activated){m->activated=1u;m->loading=1u;}publish(m);release(m);return FX_OK;
}
static void ready_locked(FxModuleManager *m) {
    if(atomic_exchange(&m->ready_requested,0u) && m->loading) {
        /* The pre-Activate selection is only intent. Resolve it against the
         * published resident set before showing current/next-boot status.
         * This callback reads worker state; no card rescan is required. */
        if(m->io.current)for(uint32_t i=0;i<m->count;++i)
            m->entries[i].current=m->io.current(m->io.context,m->entries[i].id)?1u:0u;
        m->loading=0u;publish(m);
    }
}
void fx_module_manager_ready(FxModuleManager *m) {
    if(!m || atomic_load(&m->shutdown))return;
    atomic_store(&m->ready_requested,1u);
    if(!acquire(m))return;
    if(!atomic_load(&m->shutdown))ready_locked(m);
    release(m);
}
void fx_module_manager_tick(FxModuleManager *m) {
    if(!m || atomic_load(&m->shutdown) || !acquire(m))return;
    if(atomic_load(&m->shutdown)){release(m);return;}
    ready_locked(m);
    if(m->ui_pending)publish(m);
    /* Do not dequeue an intent until the manager can actually handle it.
     * Native input remains in the existing bounded queue during contention. */
    if(m->loading){release(m);return;}
    for(uint32_t i=0;i<FX_UI_EVENTS && !atomic_load(&m->shutdown);++i) {
        FxUiTextEvent message;uint32_t bytes;
        if(fx_ui_poll_message(&m->ui,&message,sizeof(message),&bytes)!=FX_OK)break;
        FxUiEvent e=message.event;
        if(e.control==FX_MANAGER_SEARCH && e.event==FX_INPUT_TEXT) {
            if(m->search_ready && e.serial>m->search_serial_floor && (!e.reserved || e.reserved==m->search_revision) &&
               bytes==sizeof(message) && e.value<FX_MANAGER_FILTER_BYTES)
                (void)filter_locked(m,message.text,e.value+1u);
            continue;
        }
        if(e.control==3u && e.event==FX_INPUT_ACTIVATE)atomic_store(&m->scan_requested,1u);
        if(e.event==FX_INPUT_ACTIVATE && (e.control==FX_MANAGER_PREVIOUS || e.control==FX_MANAGER_NEXT)) {
            uint32_t pages=pages_for(matched_count(m)),before=m->page;
            if(e.control==FX_MANAGER_PREVIOUS && m->page)--m->page;
            if(e.control==FX_MANAGER_NEXT && m->page+1u<pages)++m->page;
            if(before!=m->page)invalidate_rows(m);
            publish(m);
        }
        if(e.control>=4u && e.control<4u+FX_MANAGED_ROWS && e.event==FX_INPUT_CHANGE) {
            uint32_t row=e.control-4u,slot;
            if(!m->rows[row].revision || e.serial<=m->rows[row].serial_floor ||
               (e.reserved && (int32_t)(e.reserved-m->rows[row].revision)<0)) {publish(m);continue;}
            if(fx_module_index_find(&m->index,m->rows[row].id,&slot)==FX_OK)
                (void)enable_locked(m,slot,e.value);
        }
    }
    if(!atomic_load(&m->shutdown) && atomic_exchange(&m->scan_requested,0u)) {
        if(scan_locked(m)==FX_BUSY)atomic_store(&m->scan_requested,1u);
    }
    release(m);
}
void fx_module_manager_shutdown(FxModuleManager *m){if(m){atomic_store(&m->shutdown,1u);if(acquire(m))release(m);}}

FxResult fx_module_manager_page(FxModuleManager *m,uint32_t page) {
    if(!m || atomic_load(&m->shutdown))return FX_STATE;
    if(!acquire(m))return FX_BUSY;
    if(atomic_load(&m->shutdown)){release(m);return FX_STATE;}
    uint32_t pages=pages_for(matched_count(m));
    FxResult r=page<pages?FX_OK:FX_BOUNDS;
    if(r==FX_OK){if(m->page!=page)invalidate_rows(m);m->page=page;publish(m);}
    release(m);return r;
}
