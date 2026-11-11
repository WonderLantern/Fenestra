#include "fenestra_audio.h"
#include "fenestra_memory.h"
uint32_t fx_audio_sample_bytes(uint32_t format) {
    return format == FX_AUDIO_S16                             ? 2u
           : format == FX_AUDIO_S24                           ? 3u
           : format == FX_AUDIO_S32 || format == FX_AUDIO_F32 ? 4u
                                                              : 0u;
}
FxResult fx_audio_provider(void *context, const FxProviderClient *owner, uint64_t operation, const void *data,
                           uint32_t bytes, FxServiceReply *out) {
    FxAudioDevice *d = context;
    if (!d || !owner || !out || !d->driver.query || (bytes && !data))
        return FX_ARGUMENT;
    uint32_t zero = 0u;
    if (!atomic_compare_exchange_strong_explicit(&d->lock, &zero, 1u, memory_order_acquire,
                                                 memory_order_relaxed))
        return FX_BUSY;
    FxAudioInfo info = {.abi = FX_AUDIO_ABI, .bytes = sizeof(info)};
    FxResult r = d->driver.query(d->driver.context, &info);
    if (r != FX_OK)
        goto done;
    info.abi = FX_AUDIO_ABI;
    info.bytes = sizeof(info);
    info.operations =
        (1u << FX_AUDIO_QUERY) | (d->driver.open ? 1u << FX_AUDIO_OPEN : 0u) |
        (d->driver.start ? 1u << FX_AUDIO_START : 0u) | (d->driver.stop ? 1u << FX_AUDIO_STOP : 0u) |
        (d->driver.status ? 1u << FX_AUDIO_STATUS : 0u) | (d->driver.control ? 1u << FX_AUDIO_CONTROL : 0u) |
        (d->driver.close ? 1u << FX_AUDIO_CLOSE : 0u);
    if (operation == FX_AUDIO_QUERY) {
        if (bytes) {
            r = FX_ARGUMENT;
            goto done;
        }
        memcpy(out->data, &info, sizeof(info));
        out->bytes = sizeof(info);
        goto done;
    }
    if (operation > FX_AUDIO_CLOSE || !(info.operations & (1u << operation))) {
        r = FX_CAPABILITY;
        goto done;
    }
    if (d->owner && (d->owner != owner->owner || d->generation != owner->generation)) {
        r = FX_BUSY;
        goto done;
    }
    if (operation == FX_AUDIO_OPEN) {
        if (bytes != sizeof(FxAudioOpen) || (d->owner && d->state != FX_AUDIO_OPENING)) {
            r = FX_ARGUMENT;
            goto done;
        }
        FxAudioOpen p;
        memcpy(&p, data, sizeof(p));
        if (d->owner && memcmp(&d->request, &p, sizeof(p))) {
            r = FX_BUSY;
            goto done;
        }
        uint32_t sample = fx_audio_sample_bytes(p.format.format);
        if (p.reserved || !sample || !(info.formats & (1u << p.format.format)) || !p.direction ||
            (p.direction & (p.direction - 1u)) || !(info.directions & p.direction) || !p.format.channels ||
            p.format.channels > info.channels_max || p.format.rate < info.rate_min ||
            p.format.rate > info.rate_max || p.format.period_frames < info.period_min ||
            p.format.period_frames > info.period_max) {
            r = FX_ARGUMENT;
            goto done;
        }
        FxStreamInfo stream;
        if (!d->stream_lookup) {
            r = FX_CAPABILITY;
            goto done;
        }
        r = d->stream_lookup(d->lookup_context, p.stream, &stream);
        if (r != FX_OK)
            goto done;
        uint64_t n = (uint64_t)p.format.channels * p.format.period_frames * sample;
        if (stream.owner != owner->owner || stream.generation != owner->generation || n > stream.payload) {
            r = FX_BOUNDS;
            goto done;
        }
        /* Retain BEFORE entering a driver: even an error can leave an async
         * hardware reference. Only an acknowledged CLOSE releases this owner. */
        d->owner = owner->owner;
        d->generation = owner->generation;
        d->stream = (void *)(uintptr_t)stream.address;
        d->format = p.format;
        d->request = p;
        d->state = FX_AUDIO_OPENING;
        r = d->driver.open(d->driver.context, &p, d->stream);
        d->state = r == FX_OK ? FX_AUDIO_OPENED : r == FX_BUSY ? FX_AUDIO_OPENING : FX_AUDIO_FAILED;
    } else {
        if (d->owner != owner->owner || d->generation != owner->generation) {
            r = FX_STATE;
            goto done;
        }
        if (operation != FX_AUDIO_CONTROL && bytes) {
            r = FX_ARGUMENT;
            goto done;
        }
        if (operation == FX_AUDIO_START) {
            if (d->state != FX_AUDIO_OPENED && d->state != FX_AUDIO_STOPPED &&
                d->state != FX_AUDIO_STARTING) {
                r = FX_STATE;
                goto done;
            }
            d->state = FX_AUDIO_STARTING;
            r = d->driver.start(d->driver.context);
            d->state = r == FX_OK ? FX_AUDIO_RUNNING : r == FX_BUSY ? FX_AUDIO_STARTING : FX_AUDIO_FAILED;
        } else if (operation == FX_AUDIO_STOP) {
            if (d->state == FX_AUDIO_CLOSING) {
                r = FX_STATE;
                goto done;
            }
            d->state = FX_AUDIO_STOPPING;
            r = d->driver.stop(d->driver.context);
            if (r == FX_OK)
                d->state = FX_AUDIO_STOPPED;
        } else if (operation == FX_AUDIO_STATUS) {
            FxAudioStatus status = {.state = d->state, .format = d->format};
            r = d->driver.status(d->driver.context, &status);
            if (r == FX_OK) {
                status.state = d->state;
                status.format = d->format;
                memcpy(out->data, &status, sizeof(status));
                out->bytes = sizeof(status);
            }
        } else if (operation == FX_AUDIO_CLOSE) {
            if (d->state == FX_AUDIO_RUNNING || d->state == FX_AUDIO_STARTING ||
                d->state == FX_AUDIO_STOPPING) {
                r = FX_STATE;
                goto done;
            }
            d->state = FX_AUDIO_CLOSING;
            r = d->driver.close(d->driver.context);
            if (r == FX_OK) {
                d->owner = 0u;
                d->generation = 0u;
                d->stream = 0;
                d->state = FX_AUDIO_IDLE;
            }
        } else if (operation == FX_AUDIO_CONTROL) {
            if (bytes != sizeof(FxAudioControl)) {
                r = FX_ARGUMENT;
                goto done;
            }
            FxAudioControl control;
            memcpy(&control, data, sizeof(control));
            r = d->driver.control(d->driver.context, &control);
        }
    }
done:
    atomic_store_explicit(&d->lock, 0u, memory_order_release);
    return r;
}
