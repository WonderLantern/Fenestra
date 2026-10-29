#ifndef FENESTRA_FAULT_TEST_H
#define FENESTRA_FAULT_TEST_H
#include "fenestra_demo.h"
/* Explicit diagnostic suffix. Unarmed test components execute the unchanged
 * demo implementation; only the separate Failsafe test action arms a bug. */
typedef struct {
    FxDemoServices demo;
    uint32_t armed,reserved;
} FxFaultServices;
#endif
