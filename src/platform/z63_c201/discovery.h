#ifndef FX_Z63_DISCOVERY_H
#define FX_Z63_DISCOVERY_H
#include "fenestra_services.h"
#include "card.h"
/* Zero-initialize before first use; a live/uncertain scope cannot reset. */
typedef struct {uint32_t magic;int32_t directory;uint32_t uncertain,native_error;FxZ63CardScope card;FxTextFiles files;} FxZ63Directory;
FxResult fx_z63_directory_init(FxZ63Directory *,FxModuleDirectory *);
#endif
