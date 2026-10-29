#ifndef FENESTRA_MACHINE_H
#define FENESTRA_MACHINE_H
#include "fenestra_platform.h"
/* core.machine/v1: explicit privileged native access, not an address sandbox.
 * A module supplies a valid mapped object/register and its real access width.
 * Cache ranges must be owned/synchronized by the module's native ABI contract. */
enum {FX_MACHINE_QUERY,FX_MACHINE_READ,FX_MACHINE_WRITE,FX_MACHINE_BARRIER,
      FX_MACHINE_CLEAN,FX_MACHINE_INVALIDATE,FX_MACHINE_CLEAN_INVALIDATE,FX_MACHINE_INSTRUCTIONS};
typedef struct {uint64_t address,value;uint32_t bytes,reserved;} FxMachineRequest;
typedef struct {uint32_t abi,bytes,operations,max_range,data_line,instruction_line;} FxMachineInfo;
FxResult fx_machine_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,const FxServiceRequest *,FxServiceReply *);
#endif
