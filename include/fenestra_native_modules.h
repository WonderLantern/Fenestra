#ifndef FENESTRA_NATIVE_MODULES_H
#define FENESTRA_NATIVE_MODULES_H
#include "fenestra_module_manager.h"
#include "fenestra_native_ui.h"
#include "fenestra_file_observer.h"
typedef struct {
    FxModuleManager manager;FxNativeUi ui;
    int32_t directory;uint32_t poisoned;
    void *(*allocate_index)(uint32_t);void (*release_index)(void *);
    FxFileObserver file_observer;
} FxNativeModules;
FxResult fx_native_modules_init(FxNativeModules *,const FxPlatform *,const FxNativeMenuProfile *,
    void *(*allocate)(uint32_t),uint32_t texts,uint32_t rows,const uintptr_t callbacks[5],int active);
/* Explicit menu actions only: native serialized card APIs on Nikon's task. */
FxResult fx_native_modules_open(FxNativeModules *);
void fx_native_modules_input(FxNativeUi *);
/* Target returns actual retained loaded state, not an inference from card. */
uint32_t fx_native_module_loaded(const char *id);
#endif
