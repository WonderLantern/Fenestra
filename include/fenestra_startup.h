#ifndef FENESTRA_STARTUP_H
#define FENESTRA_STARTUP_H
#include "fenestra.h"
#define FX_STARTUP_RECORD 64u
#define FX_STARTUP_AUTO 1u
#define FX_STARTUP_DEMO 2u
enum { FX_STARTUP_UNCHECKED,FX_STARTUP_READY,FX_STARTUP_ABNORMAL,FX_STARTUP_IO_ERROR,
       FX_STARTUP_ARMED,FX_STARTUP_RUNNING,FX_STARTUP_FAULT,FX_STARTUP_STOPPED };
enum { FX_STARTUP_FLAG,FX_STARTUP_CFG0,FX_STARTUP_CFG1 };
typedef struct {
    void *context;
    /* EMPTY means positively absent, never a mount/IO/permission failure. */
    FxResult (*read)(void *,uint32_t,uint8_t *,uint32_t,uint32_t *);
    FxResult (*create)(void *,uint32_t,const uint8_t *,uint32_t);
    FxResult (*remove)(void *,uint32_t);
} FxStartupIO;
typedef struct {
    uint32_t phase,desired,activation,generation,slot,flag_known,owned;
    FxResult error;
    uint64_t token;
    uint8_t owned_record[FX_STARTUP_RECORD];
    uint32_t record_identity;
} FxStartup;
void fx_startup_init(FxStartup *,int default_auto);
/* Existing C110 records retain identity110. Other platforms must opt in to
 * their own identity so a shared card cannot enable another camera's config. */
void fx_startup_init_profile(FxStartup *,int default_auto,uint32_t record_identity);
FxResult fx_startup_inspect(FxStartup *,const FxStartupIO *);
FxResult fx_startup_configure(FxStartup *,const FxStartupIO *,uint32_t desired);
FxResult fx_startup_arm(FxStartup *,const FxStartupIO *,uint64_t token);
void fx_startup_running(FxStartup *);
void fx_startup_fault(FxStartup *,FxResult);
FxResult fx_startup_clean_shutdown(FxStartup *,const FxStartupIO *,int quiesced);
FxResult fx_startup_clear_abnormal(FxStartup *,const FxStartupIO *);
#endif
