#ifndef FENESTRA_EXTENSION_SERVICES_H
#define FENESTRA_EXTENSION_SERVICES_H
#include "fenestra_services.h"
#include "fenestra_overlay.h"

/* Additive SDK operations. These bits are NOT in FX_SDK_CAPS: the current
 * C1.10 backend has no qualified native ROM/file interception or XM6 driver. */
#define FX_SDK_SUBSTITUTION (1u<<22)
#define FX_SDK_ACCELERATOR (1u<<23)
enum { FX_S_SUBSTITUTION=17u,FX_S_ACCELERATOR,FX_S_FILE_OPEN,FX_S_FILE_CHUNK,FX_S_FILE_CLOSE };
/* Card streams use the existing FILES grant and module-prefixed filenames.
 * OPEN:data32 filename, reply value0 opaque handle,value1 UINT64_MAX (size
 * unknown),value2 max chunk,value3 stream byte limit. CHUNK:arg0 handle,
 * arg1 count1..768; reply data/bytes, value0 sequential position,value1 EOF.
 * CLOSE:arg0 handle. No native fd/address is exported. Manifest size+SHA and
 * a final EOF check are required before sealing a model snapshot. */
enum { FX_BINDING_SUBSTITUTION_DESCRIBE=0x200u,FX_BINDING_ACCELERATOR_DESCRIBE=0x300u };
enum { FX_SUBST_QUERY,FX_SUBST_BEGIN,FX_SUBST_FEED,FX_SUBST_SEAL,
       FX_SUBST_PUBLISH,FX_SUBST_ABORT,FX_SUBST_RESTORE,FX_SUBST_STATUS };
/* BEGIN:arg1 target,arg2 expected revision,arg3 byte count;
 * data=original SHA256,new SHA256,schema SHA256 (96 bytes).
 * FEED:arg1 stage,arg2 sequential offset,data=copied chunk.
 * PUBLISH:arg1 stage,arg2 expected revision,arg3 native consumer lease.
 * RESTORE:arg1 target,arg2 expected revision,arg3 native consumer lease.
 * SEAL/ABORT:arg1 stage. STATUS:arg1 target.
 * Stage IDs and leases are 64-bit opaque values, never native addresses. */
typedef struct { FxOverlay *store;FxOverlayActor actor; } FxOverlayService;
FxResult fx_overlay_service_call(void *,const FxServiceRequest *,FxServiceReply *);
FxResult fx_overlay_service_stop(void *,uint32_t);

/* Accelerator ABI contract for a future platform driver. Unknown ownership
 * never means idle. An image/program/weights are resources of the module;
 * Fenestra owns leases, copied descriptors, bounds, completion and fault state.
 * No general DSP reset, MMIO write or "run" capability is supplied by this
 * declaration. A physical XM6 driver must qualify each capability separately. */
#define FX_ACCELERATOR_ABI 1u
enum { FX_ACCELERATOR_XM6=1u };
enum { FX_ACCELERATOR_UNKNOWN,FX_ACCELERATOR_ORIGINAL_OWNER,FX_ACCELERATOR_LEASED };
enum { FX_ACCELERATOR_LEASE=1u,FX_ACCELERATOR_PROGRAM=2u,FX_ACCELERATOR_BUFFERS=4u,
       FX_ACCELERATOR_SUBMIT=8u,FX_ACCELERATOR_COMPLETE=16u,FX_ACCELERATOR_DRAIN=32u };
enum { FX_ACCEL_QUERY,FX_ACCEL_ACQUIRE,FX_ACCEL_LOAD,FX_ACCEL_BUFFER,
       FX_ACCEL_SUBMIT,FX_ACCEL_POLL,FX_ACCEL_CANCEL,FX_ACCEL_DRAIN,FX_ACCEL_RELEASE };
typedef struct {
    uint32_t abi,bytes,kind,instance,capabilities,ownership,queue_depth,reserved;
    uint64_t shared_byte_limit,program_byte_limit;
    uint8_t firmware_sha256[32];
} FxAcceleratorDescription;
/* Future SUBMIT payload: all handles are native-validated resources. Buffer
 * ownership changes CPU->device->CPU with explicit cache synchronization;
 * completion includes this job ID AND resource epoch. Retain on uncertain stop.
 * A second NN consumes leased preview/frame resources and publishes results;
 * it does not silently replace the original AF/recognition result producer. */
typedef struct {
    uint32_t abi,bytes,generation,buffer_count;
    uint64_t lease,job,resource_epoch,program,model,input,output,deadline_ns;
} FxAcceleratorJob;
#endif
