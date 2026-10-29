#ifndef FENESTRA_C110_SERVICES_H
#define FENESTRA_C110_SERVICES_H
#include "fenestra_services.h"
#include "fenestra_platform.h"
#include "fenestra_runtime_accounts.h"
#include "fenestra_file_observer.h"
#include <stdatomic.h>
#define FX_C110_FILE_STREAMS 2u
typedef struct {
    uint64_t id,position;
    int32_t fd;
    uint32_t failed,eof;
    FxCapacityClaim claim;
} FxC110FileStream;
typedef struct {
    char id[16];
    _Atomic uint32_t menu_ready,menu_value,menu_action;
    char menu_label[32];
    uint32_t native_calls,file_calls,last_native_error;
    uint32_t io_revoked,close_uncertain;
    uint64_t next_stream;
    FxC110FileStream streams[FX_C110_FILE_STREAMS];
    uint32_t owner,generation,capabilities;
    _Atomic(FxPlatformOwner *) platform;
    const FxModule *module;
    void *module_image;
    void *worker_wait; /* Loader-owned stable scheduler; never copied from EL0. */
    FxRuntimeAccounts *accounting;
} FxC110Services;
FxResult fx_c110_module_read(const char *,uint8_t *,uint32_t,uint32_t *);
FxResult fx_c110_modules_read(FxModuleInventory *,uint8_t *,uint32_t,const uint8_t firmware[32]);
FxResult fx_c110_service_call(void *,const FxServiceRequest *,FxServiceReply *);
FxResult fx_c110_service_stop(void *,uint32_t);
#endif
