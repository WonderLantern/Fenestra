#ifndef FENESTRA_STARTUP_FILES_H
#define FENESTRA_STARTUP_FILES_H
#include "fenestra_startup.h"
#include "fenestra_file_observer.h"
#include <stdatomic.h>
/* Normal serialized Nikon filesystem wrappers. These are platform bindings,
 * not sector/controller/ROM operations. Only fixed journal paths are exposed. */
typedef struct {
    int32_t (*open)(const char *,uint32_t,uint32_t);
    uint32_t (*read)(int32_t,void *,uint32_t,uint32_t *);
    uint32_t (*write)(int32_t,const void *,uint32_t,uint32_t *,void *);
    uint32_t (*close)(int32_t);
    uint32_t (*remove)(const char *);
    const char *paths[3];
    uint32_t missing;
} FxStartupFileProfile;
typedef struct {
    const FxStartupFileProfile *profile;
    const _Atomic uint32_t *shutdown,*cleanup_window;
    _Atomic uint32_t native_error;
    FxFileObserver observer;
} FxStartupFiles;
FxResult fx_startup_files(FxStartupIO *,FxStartupFiles *);
#endif
