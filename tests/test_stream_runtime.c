#include "fenestra_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef FX_TEST_PTHREAD
#include <pthread.h>
#endif
static FxStream *stream;
static void *storage;
static void create(uint32_t slots, uint32_t payload) {
    uint32_t bytes, stride;
    assert(fx_stream_layout(slots, payload, &bytes, &stride) == FX_OK);
    storage = malloc((size_t)bytes + 63u);
    assert(storage);
    stream = (void *)(((uintptr_t)storage + 63u) & ~(uintptr_t)63u);
    assert(fx_stream_init(stream, bytes, slots, payload, 42u) == FX_OK);
}
#ifdef FX_TEST_PTHREAD
static void *producer(void *context) {
    (void)context;
    for (uint64_t i = 0u; i < 100000u; ++i) {
        FxStreamSpan span;
        FxResult r;
        do {
            r = fx_stream_produce(stream, &span);
        } while (r == FX_BUSY);
        assert(r == FX_OK);
        uint64_t *p = span.data;
        for (uint32_t j = 0u; j < 8u; ++j)
            p[j] = i * 17u + j;
        assert(fx_stream_commit(stream, &span, 64u, 7u, i * 1000u) == FX_OK);
    }
    return 0;
}
static void *consumer(void *context) {
    (void)context;
    for (uint64_t i = 0u; i < 100000u; ++i) {
        FxStreamSpan span;
        FxResult r;
        do {
            r = fx_stream_consume(stream, &span);
        } while (r == FX_EMPTY || r == FX_BUSY);
        assert(r == FX_OK && span.bytes == 64u && span.timestamp == i * 1000u && span.flags == 7u);
        uint64_t *p = span.data;
        for (uint32_t j = 0u; j < 8u; ++j)
            assert(p[j] == i * 17u + j);
        assert(fx_stream_release(stream, &span) == FX_OK);
    }
    return 0;
}
#endif
int main(void) {
    uint32_t bytes, stride;
    assert(fx_stream_layout(3u, 10u, &bytes, &stride) == FX_ARGUMENT);
    assert(fx_stream_layout(0x80000000u, 128u, &bytes, &stride) == FX_BOUNDS);
    create(2u, 64u);
    FxStreamSpan a, b, c;
    assert(fx_stream_consume(stream, &a) == FX_EMPTY);
    assert(fx_stream_produce(stream, &a) == FX_OK);
    assert(fx_stream_cancel(stream, &a) == FX_OK);
    assert(fx_stream_produce(stream, &b) == FX_OK);
    assert(fx_stream_commit(stream, &a, 4u, 0u, 0u) == FX_STALE);
    memset(b.data, 0x35, 64u);
    assert(fx_stream_commit(stream, &b, 65u, 0u, 0u) == FX_BOUNDS);
    assert(fx_stream_commit(stream, &b, 64u, 9u, 123u) == FX_OK);
    assert(fx_stream_produce(stream, &a) == FX_OK);
    assert(fx_stream_commit(stream, &a, 0u, 0u, 124u) == FX_OK);
    assert(fx_stream_produce(stream, &c) == FX_BUSY);
    assert(atomic_load(&stream->dropped) == 1u);
    assert(fx_stream_consume(stream, &c) == FX_OK && c.timestamp == 123u && c.flags == 9u);
    for (uint32_t i = 0u; i < 64u; ++i)
        assert(((uint8_t *)c.data)[i] == 0x35u);
    assert(fx_stream_release(stream, &c) == FX_OK);
    assert(fx_stream_release(stream, &c) == FX_STALE);
    assert(fx_stream_produce(stream, &b) == FX_OK);
    fx_stream_close(stream);
    assert(!fx_stream_quiesced(stream));
    assert(fx_stream_commit(stream, &b, 1u, 0u, 0u) == FX_STATE);
    assert(fx_stream_consume(stream, &c) == FX_OK && !c.bytes);
    assert(fx_stream_release(stream, &c) == FX_OK);
    assert(fx_stream_consume(stream, &c) == FX_EMPTY);
    assert(fx_stream_quiesced(stream));
    free(storage);
#ifdef FX_TEST_PTHREAD
    create(64u, 64u);
    pthread_t p, q;
    assert(!pthread_create(&p, 0, producer, 0));
    assert(!pthread_create(&q, 0, consumer, 0));
    assert(!pthread_join(p, 0));
    assert(!pthread_join(q, 0));
    assert(atomic_load(&stream->read) == 100000u);
    free(storage);
#endif
    puts("stream runtime: ownership, wrap, cancellation, close and payload ordering passed");
    return 0;
}
