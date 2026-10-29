#ifndef FENESTRA_MODULE_BUS_H
#define FENESTRA_MODULE_BUS_H
#include "fenestra_platform.h"
#include "fenestra_slot_pool.h"
#define FX_BUS_BUDGET (4u * 1024u * 1024u)
#define FX_BUS_DEFAULT_QUEUE 8u
#define FX_BUS_BYTES 128u
enum {FX_BUS_PUBLISH=1u,FX_BUS_LOOKUP,FX_BUS_SEND,FX_BUS_POLL,FX_BUS_REMOVE,FX_BUS_QUERY,FX_BUS_ENUM,
    FX_BUS_RESERVE,FX_BUS_CAPACITY};
typedef struct {uint64_t endpoint;uint32_t capacity,reserved;} FxBusQueueRequest;
typedef struct {uint32_t abi,bytes,budget,used,peak,endpoint_slots,active_endpoints,allocated_queues,
    queued_messages,queue_slots,queued_peak,default_queue,max_queue,max_payload,reserved[2];} FxBusCapacity;
typedef struct {uint32_t abi,bytes,limit,allocated,queued,peak,owner,generation;uint64_t endpoint;} FxBusEndpointCapacity;
_Static_assert(sizeof(FxBusCapacity)==64u,"bus capacity ABI");
_Static_assert(sizeof(FxBusEndpointCapacity)==40u,"endpoint capacity ABI");
typedef struct {char name[64];uint32_t version,reserved;} FxEndpointRequest;
typedef struct {
    uint32_t abi,bytes,operations,max_endpoints,max_queue,max_payload,active;
} FxModuleBusInfo;
typedef struct {
    char name[64];uint64_t endpoint;
    uint32_t version,owner,generation,reserved;
} FxEndpointInfo;
_Static_assert(sizeof(FxModuleBusInfo)==28u,"bus information ABI");
_Static_assert(sizeof(FxEndpointInfo)==88u,"endpoint information ABI");
typedef struct {
    uint64_t endpoint,correlation;uint32_t sender,generation,bytes,reserved;
    uint8_t data[FX_BUS_BYTES];
} FxModuleMessage;
typedef struct {
    FxPlatformOwner *owner;uint64_t token;char name[64];
    uint32_t version,hash,next,head,count,capacity,limit,peak;FxModuleMessage *queue;
    FxAllocationObserver queue_observer;
} FxModuleEndpoint;
typedef struct {
    _Atomic uint32_t lock;FxSlotBudget budget;FxSlotPool endpoints;
    FxAllocationObserver metadata;
    FxAllocationObserver (*owner_queue)(FxPlatformOwner *);
    uint32_t inline_buckets[16],*buckets,bucket_count,allocated_queues,queued,queue_slots,queued_peak;
} FxModuleBus;
/* Zero-initialized bus; idempotent while live. No allocation until PUBLISH. */
FxResult fx_module_bus_init(FxModuleBus *,FxIndexMemory);
FxResult fx_module_bus_call(FxModuleBus *,FxPlatformOwner *,const FxServiceRequest *,FxServiceReply *);
FxResult fx_module_bus_stop(FxModuleBus *,FxPlatformOwner *);
/* Unpublished/quiescent bus only; not a module hot-unload facility. */
FxResult fx_module_bus_destroy(FxModuleBus *);
/* Each target owns one lazily allocated bus, retained until reboot. */
FxModuleBus *fx_native_module_bus(void);
#endif
