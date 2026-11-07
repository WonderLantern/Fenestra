/* Z8 C3.11: the task-family bindings are pinned against the C311 image.
 * create 0x40185064 validates the 56-byte record (entry +0x10, priority
 * +0x18, stack_bytes +0x20, stack +0x28, processor +0x30, affinity +0x34),
 * pops a 0x78-byte TCB from the free list 0xa4b7568, inserts it into the
 * ready list and returns slot+1; resume 0x401855e0 bounds the id against
 * the count at 0xa2aaab8 and wakes the TCB from the id table 0xa499e430;
 * delay 0x40187b00 links the current TCB into the wake list with wake
 * tick = now + ticks. The numbers equal the former Z9 references but are
 * re-derived from C311 bytes (16-byte entry-window sha256 pins in
 * tests/test_z8_task_pins.py). command/format below keep Z9 reference
 * values; their only consumers are the debug-only paths in src/native_rtos.c. */
#include "fenestra_native_rtos.h"
#include "ptp.h"
#define FN(a,t) ((t)(uintptr_t)(a))
extern FxRtos *fx_z8_menu_rtos(void);
const FxNativeRtosProfile fx_native_rtos_profile={
    fx_z8_menu_rtos,fx_z8_menu_ptp,
    FN(UINT64_C(0x40c01890),void (*)(const char *,char *)),
    FN(UINT64_C(0x4016a918),int32_t (*)(char *,uint64_t,const char *,const void *)),
    FN(UINT64_C(0x40185064),int32_t (*)(const FxRtosNativeTask *)), /* create, C311-pinned */
    FN(UINT64_C(0x401855e0),int32_t (*)(int32_t)),                /* resume, C311-pinned */
    FN(UINT64_C(0x40187b00),int32_t (*)(uint32_t))};               /* delay,  C311-pinned */
