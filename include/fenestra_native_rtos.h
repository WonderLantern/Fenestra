#ifndef FENESTRA_NATIVE_RTOS_H
#define FENESTRA_NATIVE_RTOS_H
#include "fenestra_ptp.h"
#include <stddef.h>
#define FX_RTOS_OUTPUT 8192u
enum { FX_RTOS_IDLE,FX_RTOS_QUEUED,FX_RTOS_RUNNING,FX_RTOS_DONE,
       FX_RTOS_EXPIRED,FX_RTOS_REVOKED };
typedef struct {
    _Atomic uint32_t phase,closed,output_lock,output_bytes,dropped;
    uint32_t job,generation,timeout_ms,kind;
    int32_t task;
    uint64_t session,deadline,result,address,args[8];
    char command[512];
    uint8_t output[FX_RTOS_OUTPUT];
    uint8_t stack[32768] __attribute__((aligned(16)));
} FxRtos;
typedef struct {
    uint32_t attributes,reserved;uintptr_t context,entry;
    uint32_t priority,reserved2;uint64_t stack_bytes;uintptr_t stack;
    uint32_t processor,affinity;
} FxRtosNativeTask;
_Static_assert(sizeof(FxRtosNativeTask)==56 && offsetof(FxRtosNativeTask,entry)==16 &&
    offsetof(FxRtosNativeTask,stack)==40 && offsetof(FxRtosNativeTask,affinity)==52,"native task ABI");
typedef struct {
    FxRtos *(*state)(void);
    FxPtp *(*ptp)(void);
    void (*command)(const char *,char *);
    int32_t (*format)(char *,uint64_t,const char *,const void *);
    int32_t (*create)(const FxRtosNativeTask *);
    int32_t (*resume)(int32_t);
    int32_t (*delay)(uint32_t);
} FxNativeRtosProfile;
extern const FxNativeRtosProfile fx_native_rtos_profile;
uint32_t fx_rtos_debug(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
uint32_t fx_rtos_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
uint32_t fx_rtos_vprintf(const char *,const void *,uintptr_t);
void fx_rtos_revoke(void);
int fx_rtos_quiesced(void);
/* One iteration, also called by the dedicated task. It never executes a job
 * from the USB callback or the core polling task. */
void fx_rtos_worker_step(FxRtos *);
int fx_rtos_read(uint64_t,uint32_t,uint8_t *,uint64_t *);
#endif
