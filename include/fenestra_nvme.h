#ifndef FENESTRA_NVME_H
#define FENESTRA_NVME_H
#include "fenestra_exfat.h"
#define FX_NVME_QUEUE_DEPTH 16u
typedef struct { uint32_t words[16]; } FxNvmeCommand;
typedef struct { volatile uint32_t words[4]; } FxNvmeCompletion;
typedef struct {
    FxNvmeCommand *submission;
    FxNvmeCompletion *completion;
    uint16_t tail, head, phase, id;
} FxNvmeQueue;
typedef struct {
    uintptr_t registers;
    uint32_t doorbell_stride, sector_bytes, ready, failed, commands;
    uint64_t sector_count;
    uint16_t next_command;
    FxNvmeQueue admin, io;
    _Alignas(4096) FxNvmeCommand admin_submission[FX_NVME_QUEUE_DEPTH];
    _Alignas(4096) FxNvmeCompletion admin_completion[FX_NVME_QUEUE_DEPTH];
    _Alignas(4096) FxNvmeCommand io_submission[FX_NVME_QUEUE_DEPTH];
    _Alignas(4096) FxNvmeCompletion io_completion[FX_NVME_QUEUE_DEPTH];
    _Alignas(4096) uint8_t transfer[4096];
} FxNvme;
/* Bare-metal, single-owner, identity DMA addresses, 4 KiB pages. Read-only NVM
 * commands; queues/bounce memory stay allocated for the controller lifetime.
 * PCIe BAR assignment and DMA address reachability must be established first. */
FxResult fx_nvme_init(FxNvme *, uintptr_t controller_registers, FxBlockIO *);
FxResult fx_nvme_stop(FxNvme *);
#endif
