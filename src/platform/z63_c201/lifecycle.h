#ifndef FX_Z63_C201_LIFECYCLE_H
#define FX_Z63_C201_LIFECYCLE_H
#include <stdint.h>
#include <stdatomic.h>
enum {
    FX_Z63_STARTED=1u, FX_Z63_CARD=2u, FX_Z63_STOP=4u,
    FX_Z63_START_PENDING=0x100u, FX_Z63_CARD_PENDING=0x200u,
    FX_Z63_STOP_PENDING=0x400u, FX_Z63_PENDING=0x700u
};
/* Retained owner storage, initialized before publication. Notifications coalesce;
 * they do not certify native completion. STOP is terminal until cold boot. */
typedef struct { _Atomic uint32_t word; } FxZ63Lifecycle;
void fx_z63_lifecycle_record(FxZ63Lifecycle *,uint32_t event);
uint32_t fx_z63_lifecycle_take(FxZ63Lifecycle *);
int fx_z63_lifecycle_accepts_work(const FxZ63Lifecycle *);
#endif
