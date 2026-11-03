/* Expected-negative full-system control: never return from EL0. */
#include "fenestra_api.h"
int fenestra_module_entry(const FxModuleServices *api) {
    (void)api;
    for(;;)__asm__ volatile("nop":::"memory");
}
