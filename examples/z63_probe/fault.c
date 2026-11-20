/* Emulator negative control: an EL0 null write must fault and restore native
 * context. This is not a camera feature module or a hardware test recipe. */
#include "fenestra_services.h"
int fenestra_module_entry(const FxModuleServices *base) {
    if(!base)return 1;
    *(volatile uint32_t *)(uintptr_t)0u=1u;
    return 0;
}
