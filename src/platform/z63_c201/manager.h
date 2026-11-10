#ifndef FX_Z63_MANAGER_H
#define FX_Z63_MANAGER_H
#include "fenestra_native_modules.h"
#include "discovery.h"
typedef struct {FxNativeModules native;FxZ63Directory directory;FxModuleDirectory files;uint32_t uncertain;} FxZ63Manager;
FxResult fx_z63_manager_init(FxZ63Manager *,const FxNativeMenuProfile *,void *(*)(uint32_t),uint32_t,uint32_t,const uintptr_t[5],int);
#endif
