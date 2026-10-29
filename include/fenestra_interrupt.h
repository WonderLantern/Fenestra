#ifndef FENESTRA_INTERRUPT_H
#define FENESTRA_INTERRUPT_H
#include <stdint.h>
/* Fenestra-owned synchronous software interrupt, valid only while a module
 * runs under the private EL1 vector table. This is a terminal Panic request;
 * it does not resume the component or claim an IRQ/watchdog facility. */
#define FX_INTERRUPT_PANIC_SVC 0x464eu
#define FX_INTERRUPT_MODULE_BUG 0x4d4f4431u
static inline __attribute__((noreturn)) void fx_interrupt_panic(uint32_t reason) {
    register uint64_t argument __asm__("x0")=reason;
    __asm__ volatile("svc #0x464e" : "+r"(argument) :: "memory");
    __builtin_trap(); /* A Panic interrupt must never return to this module. */
}
#endif
