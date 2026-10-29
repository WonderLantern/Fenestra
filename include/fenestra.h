#ifndef FENESTRA_H
#define FENESTRA_H

#include <stddef.h>
#include <stdint.h>

#define FX_ABI 1u
#define FX_MAX_LEASES 16u
#define FX_CARD_LIMIT (1024u * 1024u)
#define FX_READ_CALL_LIMIT 64u

typedef enum {
    FX_OK, FX_ARGUMENT, FX_STATE, FX_CAPABILITY, FX_BUDGET, FX_BUSY,
    FX_STALE, FX_TIMEOUT, FX_IO, FX_BOUNDS, FX_INTEGRITY, FX_CLOSE, FX_EMPTY,
    FX_INTERRUPTED
} FxResult;

typedef enum {
    FX_BYPASS, FX_VALIDATING, FX_PREPARED, FX_ACTIVE,
    FX_QUIESCING, FX_DISABLED, FX_PANIC
} FxPhase;

typedef struct {
    FxResult reason;
    FxPhase phase;
    uint32_t generation;
    uint64_t detail;
} FxFault;

typedef struct { uint64_t id; uint32_t generation; } FxLease;

/* All calls must be serialized by one owner or a validated platform lock.
 * This struct is a portable accounting model, not a camera exception handler.
 * No function allocates, frees, executes packages, or changes firmware state. */
typedef struct {
    uint32_t abi, generation, attempted, outstanding, drain_confirmed;
    FxPhase phase;
    uint64_t verified_capabilities, ram_budget, reserved_bytes;
    uint64_t last_time, deadline, watchdog_interval, next_lease;
    FxLease leases[FX_MAX_LEASES];
    FxFault first_fault;
    uint32_t fault_count;
    uint32_t execution_idle; /* No module invocation/lease is in flight. */
} FxRuntime;

void fx_init(FxRuntime *, uint64_t verified_capabilities, uint64_t ram_budget);
FxResult fx_begin(FxRuntime *, uint64_t required_capabilities,
                  uint64_t reserved_bytes, uint64_t now, uint64_t timeout);
FxResult fx_prepared(FxRuntime *, uint64_t now);
FxResult fx_publish(FxRuntime *, uint64_t now);
FxResult fx_acquire(FxRuntime *, uint32_t generation, uint64_t now, FxLease *);
FxResult fx_release(FxRuntime *, FxLease, uint64_t now);
FxResult fx_disable(FxRuntime *, uint64_t now, uint64_t timeout);
FxResult fx_confirm_quiesced(FxRuntime *, uint32_t generation, uint64_t now);
FxResult fx_heartbeat(FxRuntime *, uint64_t now);
/* Trusted execution-boundary bookkeeping, not a module-facing bypass.
 * Idle excludes original task sleep/native service time from the module
 * heartbeat. Pending EL0/IRQ continuations retain their active deadline. */
FxResult fx_execution_idle(FxRuntime *, uint64_t now);
FxResult fx_execution_resume(FxRuntime *, uint64_t now);
FxResult fx_poll(FxRuntime *, uint64_t now);
FxResult fx_panic(FxRuntime *, FxResult reason, uint64_t detail);
FxResult fx_reject(FxRuntime *, FxResult reason, uint64_t detail);
int fx_release_allowed(const FxRuntime *);

typedef struct {
    void *context;
    int (*open)(void *, const char *, uint64_t *size);
    int (*read)(void *, uint8_t *, uint32_t requested, uint32_t *actual);
    int (*close)(void *);
} FxCardIO;

/* Internal SHA-256 implementation; expected digests come from independent policy. */
void fx_sha256(const void *, size_t bytes, uint8_t digest[32]);

typedef struct {
    FxResult primary, cleanup;
    uint32_t bytes, read_calls, verified;
} FxCardReceipt;

/* Read-only data staging. The output never becomes executable memory. */
FxResult fx_card_read(const FxCardIO *, const char *path, uint32_t expected_bytes,
                      const uint8_t expected_sha256[32], uint8_t *buffer,
                      uint32_t capacity, FxCardReceipt *);

enum {
    FX_FULL_READOUT = 1u << 0, FX_FRAME_LEASE = 1u << 1,
    FX_MODE_TRANSACTION = 1u << 2, FX_ENCODER_3_2 = 1u << 3,
    FX_STORAGE_HANDOFF = 1u << 4, FX_RESTORE_MODE = 1u << 5,
    FX_THERMAL_LIMITS = 1u << 6, FX_BUFFER_OWNERSHIP = 1u << 7,
    FX_VIDEO_REQUIRED = 0xffu
};

typedef struct {
    uint32_t verified, full_width, full_height;
    uint64_t ram_budget, max_raw_bandwidth;
} FxVideoCaps;

typedef struct {
    uint32_t width, height, bits, stride, fps_num, fps_den, buffers;
} FxVideoRequest;

typedef struct {
    uint32_t missing_capabilities;
    uint64_t frame_bytes, buffer_bytes, raw_bytes_per_second;
} FxVideoPlan;

/* Eligibility/budget calculation only; no sensor, codec, DMA or register I/O. */
FxResult fx_video_plan(const FxVideoCaps *, const FxVideoRequest *, FxVideoPlan *);

#endif
