#ifndef FENESTRA_IRQ_H
#define FENESTRA_IRQ_H
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
/* Normal original IRQ API, resolved by rtos.irq.* names. Original mappings
 * and native IRQ ABI apply. This descriptor is RETAINED, not copied.
 * callback(cookie, saved_context) executes in IRQ context; an EL0 module
 * entry is not a native IRQ callback. Keep code/cookie/descriptor alive. */
typedef struct {
    int32_t interrupt,priority,trigger;
    uint32_t reserved;
    uint64_t callback,cookie;
} FxNativeIrqDescriptor;
enum {FX_IRQ_TRIGGER_KEEP=-1,FX_IRQ_TRIGGER_LEVEL=0,FX_IRQ_TRIGGER_EDGE=2};
enum {FX_IRQ_ENABLED=1u,FX_IRQ_PENDING=2u,FX_IRQ_ACTIVE=4u};
_Static_assert(sizeof(FxNativeIrqDescriptor)==32u,"native IRQ descriptor size");
_Static_assert(offsetof(FxNativeIrqDescriptor,callback)==16u,"native IRQ callback offset");
_Static_assert(offsetof(FxNativeIrqDescriptor,cookie)==24u,"native IRQ cookie offset");
/* rtos.irq.mailbox(cookie,saved_context): core-owned executable callback for
 * ordinary modules. cookie points to this retained native allocation, NOT
 * module memory. Mask the claimed line first, then publish a notification.
 * No event API requiring unmasked IRQs and no EL0 callback runs here.
 * The module polls notifications, acknowledges its peripheral, then explicitly
 * reenables its line. Exported memory remains retained on close/Panic.
 * last_context is an observation address, not a retained exception frame. */
typedef struct {
    uint32_t abi,bytes;
    uint64_t disable; /* retained address of rtos.irq.disable */
    int32_t interrupt;
    uint32_t reserved;
    _Atomic uint64_t notifications,last_context;
    _Atomic int32_t mask_result;
    uint32_t padding;
} FxNativeIrqMailbox;
_Static_assert(sizeof(FxNativeIrqMailbox)==48u,"native IRQ mailbox size");
int64_t fx_native_irq_mailbox(uintptr_t cookie,uintptr_t saved_context);
/* register(desc), register-affinity(desc,one_cpu_bit), unregister(desc)
 * enable/disable(irq), status(irq,uint32_t *), limit(), priority-levels()
 * priority.get/trigger.get(irq,int32_t *), priority.set/trigger.set(irq,int32_t)
 * affinity.get(irq,uint8_t *), affinity.set(irq,one_cpu_bit)
 * pending.set/clear(irq), pending.is-set(irq)->0/1.
 * Other returns are original signed int32 status: 0 success, -17 argument,
 * -1005 occupied registration, -1004 missing registration for enable/disable.
 * Registration does not enable the line. limit() is an INCLUSIVE maximum.
 * IRQ<32 registration is CPU-local: serialize and use the same CPU throughout.
 * GICv3 multi-bit affinity returns success but leaves the old route unchanged.
 * unregister uses only desc->interrupt, NOT descriptor pointer identity;
 * it disables and removes that slot without joining in-flight callbacks.
 * Caller must own/serialize the native slot and quiesce all users before free.
 * Pending clear is a controller operation, not a peripheral source ACK.
 * These bindings do not automatically register/unregister anything at boot,
 * Activate, owner close or Panic, and do not write internal Flash. */
#endif
