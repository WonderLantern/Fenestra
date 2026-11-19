#include "fenestra_audio.h"
#include "fenestra_extensions.h"
#include "fenestra_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static FxExtensionDomain *domain;
static void *allocated[1024];
static uint32_t allocation_count;
static void *allocate(void *context, uint32_t bytes) {
    (void)context;
    assert(allocation_count < 1024u);
    void *p = malloc(bytes);
    if (p)
        allocated[allocation_count++] = p;
    return p;
}
static void release(void *context, void *p) {
    (void)context;
    free(p);
}
FxExtensionDomain *fx_native_extensions(FxPlatformOwner *o) {
    (void)o;
    return domain;
}
static uint32_t calls, wakes;
static FxPlatformOwner *retiring_owner;
static uint64_t retiring_token;
static void wake(void *p) {
    (void)p;
    ++wakes;
}
static FxResult provider(void *p, const FxProviderClient *caller, uint64_t operation, const void *data,
                         uint32_t bytes, FxServiceReply *out) {
    (void)p;
    (void)data;
    assert(caller->owner == 2u && caller->generation == 1u && !operation && !bytes);
    ++calls;
    if (retiring_owner) {
        assert(fx_provider_remove(domain, retiring_owner, retiring_token) == FX_BUSY);
        retiring_owner = 0;
    }
    out->value[0] = 99u;
    return FX_OK;
}
static FxResult service(FxPlatformOwner *o, const char *name, uint32_t operation, uint64_t arg,
                        const void *data, uint32_t bytes, FxServiceReply *out) {
    FxNamedBinding b = {.name = name};
    uint64_t cookie = 0u;
    assert(fx_extensions_resource(domain, FX_RESOURCE_ATTACH, &b, o, 1u, &cookie, 0, 0) == FX_OK);
    FxServiceRequest q = {.bytes = bytes, .arg = {0u, 0u, operation, arg}};
    if (bytes)
        memcpy(q.data, data, bytes);
    memset(out, 0, sizeof(*out));
    return fx_extensions_resource(domain, FX_RESOURCE_CALL, &b, o, 1u, &cookie, &q, out);
}
static FxResult stream_lookup(void *context, uint64_t token, FxStreamInfo *info) {
    return fx_extensions_stream(context, token, info);
}
static FxStream *audio_stream;
static uint32_t opening, starting, stopping, closing;
static FxResult describe(void *p, FxAudioInfo *out) {
    (void)p;
    out->formats = 1u << FX_AUDIO_S16;
    out->directions = FX_AUDIO_CAPTURE;
    out->rate_min = 48000u;
    out->rate_max = 48000u;
    out->channels_max = 2u;
    out->period_min = 16u;
    out->period_max = 16u;
    out->flags = FX_AUDIO_SYNTHETIC;
    return FX_OK;
}
static FxResult audio_open(void *p, const FxAudioOpen *o, FxStream *s) {
    (void)p;
    (void)o;
    audio_stream = s;
    return opening++ ? FX_OK : FX_BUSY;
}
static FxResult audio_start(void *p) {
    (void)p;
    if (!starting++)
        return FX_BUSY;
    FxStreamSpan span;
    assert(fx_stream_produce(audio_stream, &span) == FX_OK);
    for (uint32_t i = 0u; i < 32u; ++i)
        ((int16_t *)span.data)[i] = (int16_t)(i * 7u);
    return fx_stream_commit(audio_stream, &span, 64u, 0u, 5678u);
}
static FxResult audio_stop(void *p) {
    (void)p;
    return stopping++ ? FX_OK : FX_BUSY;
}
static FxResult audio_close(void *p) {
    (void)p;
    return closing++ ? FX_OK : FX_BUSY;
}
int main(void) {
    uint64_t dummy = 0u;
    FxNamedBinding binding = {.name = "test",
                              .kind = FX_BIND_TABLE,
                              .abi = FX_CALL_BYTES,
                              .version = 1u,
                              .bytes = 8u,
                              .address = (uintptr_t)&dummy};
    FxPlatform p = {.abi = 1u, .count = 1u, .bindings = &binding};
    FxPlatformOwner a = {0}, b = {0};
    assert(fx_platform_init(&a, &p, 1u, 1u, FX_SDK_CAPS) == FX_OK);
    assert(fx_platform_init(&b, &p, 2u, 1u, FX_SDK_CAPS) == FX_OK);
    b.wake_context = 0;
    atomic_store(&b.wake_callback, (uintptr_t)wake);
    domain = fx_extensions_create((FxIndexMemory){0, allocate, release}, 1024u * 1024u);
    assert(domain);
    FxServiceReply out;
    assert(service(&b, "core.providers", FX_PROVIDER_QUERY, 0u, 0, 0u, &out) == FX_OK);
    FxProviderDeclaration declaration = {"test.provider", 3u, 5u, 0, provider};
    uint64_t token;
    assert(fx_provider_publish(domain, &a, &declaration, &token) == FX_OK && wakes == 1u);
    FxProviderLookup lookup = {.name = "test.provider", .minimum = 2u, .maximum = 4u, .features = 1u};
    FxProviderInfo info;
    assert(fx_provider_lookup(domain, &lookup, &info) == FX_OK && info.token == token && info.version == 3u);
    lookup.features = 8u;
    assert(fx_provider_lookup(domain, &lookup, &info) == FX_EMPTY);
    assert(fx_provider_call(domain, &b, token, 0u, 0, 0u, &out) == FX_OK && calls == 1u &&
           out.value[0] == 99u);
    assert(fx_provider_remove(domain, &b, token) == FX_STALE);
    assert(fx_provider_remove(domain, &a, token) == FX_OK);
    assert(fx_provider_call(domain, &b, token, 0u, 0, 0u, &out) == FX_STALE);
    assert(fx_provider_publish(domain, &a, &declaration, &token) == FX_OK);
    assert(service(&b, "core.providers", FX_PROVIDER_ENUM, 0u, 0, 0u, &out) == FX_OK);
    retiring_owner = &a;
    retiring_token = token;
    assert(fx_provider_call(domain, &b, token, 0u, 0, 0u, &out) == FX_OK);
    assert(fx_provider_call(domain, &b, token, 0u, 0, 0u, &out) == FX_STALE);
    assert(fx_provider_remove(domain, &a, token) == FX_OK);
    assert(fx_provider_publish(domain, &a, &declaration, &token) == FX_OK);
    for (uint32_t i = 0u; i < 40u; ++i) {
        FxLogWrite log = {.level = FX_LOG_INFO, .bytes = 1u, .text = {(char)('a' + i % 26u)}};
        assert(service(&b, "core.logs", FX_LOG_WRITE, 0u, &log, sizeof(log), &out) == FX_OK);
    }
    assert(service(&b, "core.logs", FX_LOG_QUERY, 0u, 0, 0u, &out) == FX_OK);
    FxLogInfo loginfo;
    memcpy(&loginfo, out.data, sizeof(loginfo));
    assert(loginfo.oldest == 9u && loginfo.next == 41u && loginfo.dropped == 8u);
    assert(service(&b, "core.logs", FX_LOG_READ, 1u, 0, 0u, &out) == FX_STALE);
    assert(service(&b, "core.time", FX_TIME_CREATE, 0u, 0, 0u, &out) == FX_OK);
    uint64_t timer = out.value[0];
    FxTimerRequest arm = {timer, 100u, 20u};
    assert(service(&b, "core.time", FX_TIME_ARM, 0u, &arm, sizeof(arm), &out) == FX_OK);
    fx_extensions_pulse(&b, 151u);
    assert(service(&b, "core.time", FX_TIME_POLL, timer, 0, 0u, &out) == FX_OK);
    FxTimerInfo time;
    memcpy(&time, out.data, sizeof(time));
    assert(time.expirations == 3u && time.deadline_ms == 160u);
    assert(service(&b, "core.time", FX_TIME_RELEASE, timer, 0, 0u, &out) == FX_OK);
    assert(!atomic_load(&b.tick_subscriptions));
    assert(service(&b, "core.time", FX_TIME_POLL, timer, 0, 0u, &out) == FX_STALE);
    uint32_t before_reuse = allocation_count;
    assert(service(&b, "core.time", FX_TIME_CREATE, 0u, 0, 0u, &out) == FX_OK);
    assert(out.value[0] != timer && allocation_count == before_reuse);
    uint32_t size, stride;
    assert(fx_stream_layout(2u, 64u, &size, &stride) == FX_OK);
    void *storage = malloc(size + 63u);
    assert(storage);
    FxStream *stream = (void *)(((uintptr_t)storage + 63u) & ~(uintptr_t)63u);
    assert(fx_stream_init(stream, size, 2u, 64u, 7u) == FX_OK);
    assert(fx_stream_set_clock(stream, FX_CLOCK_SAMPLE_INDEX, 48000u) == FX_OK);
    b.module_image = (void *)stream;
    b.module_bytes = size;
    b.module_segment_count = 1u;
    b.module_segments[0] = (FxSegment){.memory_bytes = size, .flags = FX_READ | FX_WRITE};
    FxStreamRegistration registration = {
        .address = (uintptr_t)stream, .epoch = 7u, .bytes = size, .name = "audio.test"};
    assert(service(&b, "core.streams", FX_STREAM_REGISTER, 0u, &registration, sizeof(registration), &out) ==
           FX_OK);
    FxStreamInfo streaminfo;
    memcpy(&streaminfo, out.data, sizeof(streaminfo));
    assert(streaminfo.clock_id == FX_CLOCK_SAMPLE_INDEX && streaminfo.clock_hz == 48000u);
    assert(service(&b, "core.self", 0u, 0u, 0, 0u, &out) == FX_OK);
    FxModuleSelfInfo self;
    memcpy(&self, out.data, sizeof(self));
    assert(self.abi == 1u && self.bytes == sizeof(self) && self.owner == 2u && self.generation == 1u &&
           self.image == (uintptr_t)stream && self.image_bytes == size);
    FxAudioDevice device = {.driver = {.query = describe,
                                       .open = audio_open,
                                       .start = audio_start,
                                       .stop = audio_stop,
                                       .close = audio_close},
                            .lookup_context = domain,
                            .stream_lookup = stream_lookup};
    FxProviderClient caller = {1u, sizeof(caller), 2u, 1u};
    FxAudioOpen open = {streaminfo.token, FX_AUDIO_CAPTURE, 0u, {48000u, 2u, FX_AUDIO_S16, 16u}};
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_OPEN, &open, sizeof(open), &out) == FX_BUSY);
    assert(device.state == FX_AUDIO_OPENING && device.owner == caller.owner);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_OPEN, &open, sizeof(open), &out) == FX_OK);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_START, 0, 0u, &out) == FX_BUSY);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_CLOSE, 0, 0u, &out) == FX_STATE);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_START, 0, 0u, &out) == FX_OK);
    FxStreamSpan span;
    assert(fx_stream_consume(stream, &span) == FX_OK && span.timestamp == 5678u);
    for (uint32_t i = 0u; i < 32u; ++i)
        assert(((int16_t *)span.data)[i] == (int16_t)(i * 7u));
    assert(fx_stream_release(stream, &span) == FX_OK);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_CLOSE, 0, 0u, &out) == FX_STATE);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_STOP, 0, 0u, &out) == FX_BUSY);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_CLOSE, 0, 0u, &out) == FX_STATE);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_STOP, 0, 0u, &out) == FX_OK);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_CLOSE, 0, 0u, &out) == FX_BUSY);
    assert(device.owner == caller.owner && device.stream == stream);
    assert(fx_audio_provider(&device, &caller, FX_AUDIO_CLOSE, 0, 0u, &out) == FX_OK);
    assert(!device.owner && !device.stream && device.state == FX_AUDIO_IDLE);
    assert(fx_extensions_stop(&a) == FX_OK);
    assert(fx_provider_call(domain, &b, token, 0u, 0, 0u, &out) == FX_STALE);
    assert(fx_extensions_stop(&b) == FX_OK);
    assert(atomic_load(&stream->closed));
    assert(!atomic_load(&b.tick_subscriptions));
    free(storage);
    for (uint32_t i = 0u; i < allocation_count; ++i)
        free(allocated[i]);
    puts("extension runtime: services, versioning, removal, logs, timers and PCM data passed");
    return 0;
}
