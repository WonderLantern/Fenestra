#ifndef FENESTRA_API_H
#define FENESTRA_API_H
#include <stdint.h>
typedef struct {
    uint32_t abi, bytes;
    void *context;
    int (*log)(void *, const char *);
} FxModuleServices;
typedef int (*FxModuleEntry)(const FxModuleServices *);
#endif
