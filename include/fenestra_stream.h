#ifndef FENESTRA_STREAM_H
#define FENESTRA_STREAM_H
#include "fenestra.h"
#include <stdatomic.h>

/* Shared ordinary WB RAM, at the same address in its published CPU views.
 * SPSC ownership is explicit. begin/commit/cancel hold a producer/consumer
 * claim, never a stock lock. DMA completion/cache maintenance precede commit.
 * The owner pins ALL storage until every CPU/device user has stopped. */
#define FX_STREAM_ABI 1u
enum { FX_CLOCK_MONOTONIC_NS = 1u, FX_CLOCK_ARCH_COUNTER, FX_CLOCK_SAMPLE_INDEX };
#define FX_STREAM_HEADER 128u
#define FX_STREAM_ALIGNMENT 64u
typedef struct {
    uint32_t abi, bytes, slots, stride, payload, flags, clock_id, clock_hz;
    _Alignas(64) _Atomic uint64_t written;
    _Atomic uint64_t read, dropped;
    _Atomic uint32_t producer, consumer, closed, fault;
    uint64_t epoch, producer_nonce, consumer_nonce;
} FxStream;
typedef struct {
    uint64_t sequence, timestamp;
    uint32_t bytes, flags;
    uint64_t reserved;
} FxStreamFrame;
typedef struct {
    void *data;
    uint32_t capacity, bytes, flags, reserved;
    uint64_t position, epoch, timestamp, nonce;
} FxStreamSpan;
_Static_assert(sizeof(FxStream) == FX_STREAM_HEADER, "shared stream header ABI");
_Static_assert(sizeof(FxStreamFrame) == 32u, "shared stream frame ABI");
FxResult fx_stream_layout(uint32_t slots, uint32_t payload, uint32_t *bytes, uint32_t *stride);
FxResult fx_stream_init(void *, uint32_t bytes, uint32_t slots, uint32_t payload, uint64_t epoch);
FxResult fx_stream_produce(FxStream *, FxStreamSpan *);
FxResult fx_stream_commit(FxStream *, const FxStreamSpan *, uint32_t bytes, uint32_t flags,
                          uint64_t timestamp);
FxResult fx_stream_cancel(FxStream *, const FxStreamSpan *);
FxResult fx_stream_consume(FxStream *, FxStreamSpan *);
FxResult fx_stream_release(FxStream *, const FxStreamSpan *);
FxResult fx_stream_set_clock(FxStream *, uint32_t clock_id, uint32_t frequency);
void fx_stream_close(FxStream *);
int fx_stream_quiesced(const FxStream *);
#endif
