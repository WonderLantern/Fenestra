#include "module_controller.h"
#include <stddef.h>
_Static_assert(offsetof(FxZfModuleController,stage)==140,"stage");
_Static_assert(offsetof(FxZfModuleController,source_directory)==152,"directory");
_Static_assert(offsetof(FxZfModuleController,directory_bytes)==192,"bytes");
FxResult fx_zf_directory_next(void *context,FxModuleDirectoryEntry *out) {
    FxZfModuleController *c=context;c->stage=FX_ZF_LOAD_DIRECTORY_NEXT;
    FxResult r=c->source_directory.next(c->source_directory.context,out);
    if(r==FX_OK){
        c->stage=FX_ZF_LOAD_DIRECTORY_ENTRY;c->directory_bytes=out->bytes;
        /* Nikon FAT/exFAT enumeration can return uppercase names. Match the
         * manager's canonical identity before the shared loader sees them. */
        for(uint32_t i=0u;i<sizeof(out->name) && out->name[i];++i)
            if(out->name[i]>='A' && out->name[i]<='Z')out->name[i]=(char)(out->name[i]+('a'-'A'));
    }
    return r;
}
