#include "fenestra_memory.h"
#include "fenestra_stream.h"
FxResult fx_stream_layout(uint32_t slots, uint32_t payload, uint32_t *bytes, uint32_t *stride) {
    if (!bytes || !stride || !slots || (slots & (slots - 1u)) || !payload)
        return FX_ARGUMENT;
    if (payload > UINT32_MAX - 95u)
        return FX_BOUNDS;
    uint32_t step = (payload + 95u) & ~63u;
    if (slots > (UINT32_MAX - FX_STREAM_HEADER) / step)
        return FX_BOUNDS;
    *stride = step;
    *bytes = FX_STREAM_HEADER + slots * step;
    return FX_OK;
}
static int valid(const FxStream *s) {
    uint32_t bytes, stride;
    return s && !((uintptr_t)s & 63u) && s->abi == FX_STREAM_ABI && s->epoch &&
           fx_stream_layout(s->slots, s->payload, &bytes, &stride) == FX_OK && s->bytes == bytes &&
           s->stride == stride;
}
FxResult fx_stream_init(void *memory, uint32_t capacity, uint32_t slots, uint32_t payload, uint64_t epoch) {
    uint32_t bytes, stride;
    FxResult r = fx_stream_layout(slots, payload, &bytes, &stride);
    if (r != FX_OK)
        return r;
    if (!memory || ((uintptr_t)memory & 63u) || !epoch || capacity < bytes)
        return FX_ARGUMENT;
    /* Init is before publication, or after EXTERNALLY proven full quiescence. */
    memset(memory, 0, bytes);
    FxStream *s = memory;
    s->abi = FX_STREAM_ABI;
    s->bytes = bytes;
    s->slots = slots;
    s->stride = stride;
    s->payload = payload;
    s->epoch = epoch;
    s->clock_id = FX_CLOCK_MONOTONIC_NS;
    s->clock_hz = 1000000000u;
    atomic_init(&s->written, 0u);
    atomic_init(&s->read, 0u);
    atomic_init(&s->dropped, 0u);
    atomic_init(&s->producer, 0u);
    atomic_init(&s->consumer, 0u);
    atomic_init(&s->closed, 0u);
    atomic_init(&s->fault, 0u);
    return FX_OK;
}
static FxStreamFrame *frame(FxStream *s, uint64_t position) {
    return (void *)((uint8_t *)s + FX_STREAM_HEADER + (position & (s->slots - 1u)) * s->stride);
}
static FxResult fail(FxStream *s, FxResult r) {
    atomic_store_explicit(&s->fault, (uint32_t)r, memory_order_release);
    return r;
}
FxResult fx_stream_produce(FxStream *s, FxStreamSpan *out) {
    if (!valid(s) || !out)
        return FX_ARGUMENT;
    if (atomic_load_explicit(&s->closed, memory_order_acquire))
        return FX_STATE;
    uint32_t zero = 0u;
    if (!atomic_compare_exchange_strong_explicit(&s->producer, &zero, 1u, memory_order_acquire,
                                                 memory_order_relaxed))
        return FX_BUSY;
    uint64_t w = atomic_load_explicit(&s->written, memory_order_relaxed),
             r = atomic_load_explicit(&s->read, memory_order_acquire);
    FxResult result = FX_OK;
    if (atomic_load_explicit(&s->closed, memory_order_acquire))
        result = FX_STATE;
    else if (w < r || w - r > s->slots)
        result = fail(s, FX_INTEGRITY);
    else if (w == UINT64_MAX || s->producer_nonce == UINT64_MAX)
        result = fail(s, FX_BUDGET);
    else if (w - r == s->slots) {
        atomic_fetch_add_explicit(&s->dropped, 1u, memory_order_relaxed);
        result = FX_BUSY;
    }
    if (result != FX_OK) {
        atomic_store_explicit(&s->producer, 0u, memory_order_release);
        return result;
    }
    *out = (FxStreamSpan){.data = frame(s, w) + 1,
                          .capacity = s->payload,
                          .position = w,
                          .epoch = s->epoch,
                          .nonce = ++s->producer_nonce};
    return FX_OK;
}
static int matches(FxStream *s, const FxStreamSpan *p, uint32_t producer) {
    return valid(s) && p && p->epoch == s->epoch && p->data == frame(s, p->position) + 1 &&
           p->capacity == s->payload &&
           atomic_load_explicit(producer ? &s->producer : &s->consumer, memory_order_acquire) == 1u &&
           p->nonce == (producer ? s->producer_nonce : s->consumer_nonce) &&
           p->position == atomic_load_explicit(producer ? &s->written : &s->read, memory_order_relaxed);
}
FxResult fx_stream_commit(FxStream *s, const FxStreamSpan *p, uint32_t bytes, uint32_t flags,
                          uint64_t timestamp) {
    if (!matches(s, p, 1u))
        return FX_STALE;
    if (bytes > s->payload)
        return FX_BOUNDS; /* Claim remains available for cancel/retry. */
    if (atomic_load_explicit(&s->closed, memory_order_acquire))
        return fx_stream_cancel(s, p) == FX_OK ? FX_STATE : FX_STALE;
    FxStreamFrame *f = frame(s, p->position);
    *f = (FxStreamFrame){p->position, timestamp, bytes, flags, 0u};
    atomic_store_explicit(&s->written, p->position + 1u, memory_order_release);
    atomic_store_explicit(&s->producer, 0u, memory_order_release);
    return FX_OK;
}
FxResult fx_stream_cancel(FxStream *s, const FxStreamSpan *p) {
    if (!matches(s, p, 1u))
        return FX_STALE;
    atomic_store_explicit(&s->producer, 0u, memory_order_release);
    return FX_OK;
}
FxResult fx_stream_consume(FxStream *s, FxStreamSpan *out) {
    if (!valid(s) || !out)
        return FX_ARGUMENT;
    uint32_t zero = 0u;
    if (!atomic_compare_exchange_strong_explicit(&s->consumer, &zero, 1u, memory_order_acquire,
                                                 memory_order_relaxed))
        return FX_BUSY;
    uint64_t r = atomic_load_explicit(&s->read, memory_order_relaxed),
             w = atomic_load_explicit(&s->written, memory_order_acquire);
    FxResult result = FX_OK;
    if (w < r || w - r > s->slots)
        result = fail(s, FX_INTEGRITY);
    else if (w == r)
        result = FX_EMPTY;
    else if (s->consumer_nonce == UINT64_MAX)
        result = fail(s, FX_BUDGET);
    else if (frame(s, r)->sequence != r || frame(s, r)->bytes > s->payload)
        result = fail(s, FX_INTEGRITY);
    if (result != FX_OK) {
        atomic_store_explicit(&s->consumer, 0u, memory_order_release);
        return result;
    }
    FxStreamFrame *f = frame(s, r);
    *out = (FxStreamSpan){.data = f + 1,
                          .capacity = s->payload,
                          .bytes = f->bytes,
                          .flags = f->flags,
                          .position = r,
                          .epoch = s->epoch,
                          .timestamp = f->timestamp,
                          .nonce = ++s->consumer_nonce};
    return FX_OK;
}
FxResult fx_stream_release(FxStream *s, const FxStreamSpan *p) {
    if (!matches(s, p, 0u))
        return FX_STALE;
    atomic_store_explicit(&s->read, p->position + 1u, memory_order_release);
    atomic_store_explicit(&s->consumer, 0u, memory_order_release);
    return FX_OK;
}
FxResult fx_stream_set_clock(FxStream *s, uint32_t clock_id, uint32_t frequency) {
    if (!valid(s) || !clock_id || !frequency)
        return FX_ARGUMENT;
    /* Before publication only; an empty stream is not proof of no readers. */
    if (atomic_load(&s->written) || atomic_load(&s->read) || atomic_load(&s->producer) ||
        atomic_load(&s->consumer) || atomic_load(&s->closed))
        return FX_STATE;
    s->clock_id = clock_id;
    s->clock_hz = frequency;
    return FX_OK;
}
void fx_stream_close(FxStream *s) {
    if (valid(s))
        atomic_store_explicit(&s->closed, 1u, memory_order_release);
}
int fx_stream_quiesced(const FxStream *s) {
    return valid(s) && !atomic_load_explicit(&s->producer, memory_order_acquire) &&
           !atomic_load_explicit(&s->consumer, memory_order_acquire);
}
