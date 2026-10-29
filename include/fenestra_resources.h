#ifndef FENESTRA_RESOURCES_H
#define FENESTRA_RESOURCES_H
#include "fenestra_platform.h"
#include "fenestra_native_mapping.h"
enum {FX_MEMORY_ALLOCATE=1u,FX_MEMORY_READ,FX_MEMORY_WRITE,FX_MEMORY_RELEASE};
/* Legacy ALLOCATE exports a native address and RELEASE retains its storage.
 * COPY_ALLOCATE keeps the address private and RELEASE frees it normally.
 * EXPORT permanently changes a copy buffer to escaped storage.
 * RECLAIM is the caller's explicit acknowledgement that ALL native users
 * and aliases have quiesced; it also works after a legacy RELEASE. */
enum {FX_MEMORY_COPY_ALLOCATE=5u,FX_MEMORY_EXPORT,FX_MEMORY_RECLAIM,FX_MEMORY_QUERY};
/* Bulk copies between a retained native buffer and this module's Loader-
 * validated image, on original tables after EL0 returns. No address escape. */
enum {FX_MEMORY_FROM_MODULE=9u,FX_MEMORY_TO_MODULE};
typedef struct {uint32_t abi,bytes,operations,max_buffers,max_bytes,retained_buffers,active_buffers,reserved;} FxMemoryInfo;
enum {FX_FILE_OPEN=1u,FX_FILE_READ,FX_FILE_WRITE,FX_FILE_CLOSE};
enum {FX_FILE_SEEK=5u};
enum {FX_SEEK_SET,FX_SEEK_CURRENT,FX_SEEK_END};
enum {FX_FILE_READ_ONLY,FX_FILE_CREATE_EXCLUSIVE};
enum {FX_HOOK_TICK=0x100u};
typedef struct {uint64_t handle;uint32_t bytes,offset;} FxMemoryRequest;
typedef struct {uint64_t handle;uint32_t bytes,offset;uint64_t module_address;} FxModuleMemoryRequest;
typedef struct {uint64_t handle;uint32_t bytes,mode;char name[64];} FxFileRequest;
/* Native 64-bit position; zero offset/CURRENT is tell. Seeking does not
 * extend or truncate a file. Successful reply.value[0] is the new position. */
typedef struct {uint64_t handle;int64_t offset;uint32_t whence,reserved;} FxFileSeekRequest;
_Static_assert(sizeof(FxFileSeekRequest)==24u,"copied 64-bit file seek ABI");
/* Bound native providers; no camera/module names or firmware addresses here. */
FxResult fx_native_resource(void *,uint32_t,const FxNamedBinding *,FxPlatformOwner *,uint64_t,uint64_t *,
    const FxServiceRequest *,FxServiceReply *);
/* Normal module worker before the next EL0 invocation, never IRQ. */
void fx_platform_pulse(FxPlatformOwner *,uint64_t time_ms);
#endif
