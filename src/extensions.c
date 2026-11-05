#include "fenestra_extensions.h"
#include "fenestra_memory.h"
#include "fenestra_native_context.h"
#include "fenestra_time.h"
typedef struct Provider Provider;
struct Provider {
    Provider *next;
    FxPlatformOwner *owner;
    uint64_t token, features;
    uint32_t version;
    char name[64];
    void *context;
    FxProviderCall call;
    _Atomic uint32_t active, calls;
};
typedef struct Stream Stream;
struct Stream {
    Stream *next;
    FxPlatformOwner *owner;
    FxStreamInfo info;
};
typedef struct Timer Timer;
struct Timer {
    Timer *next;
    FxTimerInfo info;
    uint32_t released;
};
typedef struct Client Client;
struct Client {
    Client *next;
    FxExtensionDomain *domain;
    FxPlatformOwner *owner;
    Timer *timers;
    uint64_t now, next_log, dropped;
    FxLogRecord *logs;
    uint32_t log_capacity, timers_armed, closed;
    _Atomic uint32_t lock;
};
struct FxExtensionDomain {
    FxIndexMemory memory;
    uint32_t budget, used;
    uint64_t next;
    Provider *providers;
    Stream *streams;
    _Atomic(Client *) clients;
    _Atomic uint64_t revision;
    _Atomic uint32_t lock;
};
static FxResult client(FxExtensionDomain *, FxPlatformOwner *, Client **);
static void changed(FxExtensionDomain *d) {
    atomic_fetch_add_explicit(&d->revision, 1u, memory_order_release);
    for (Client *c = atomic_load_explicit(&d->clients, memory_order_acquire); c; c = c->next)
        fx_platform_wake(c->owner);
}
static int equal(const char *a, const char *b) {
    uint32_t i = 0u;
    while (i < 64u && a[i] && a[i] == b[i])
        ++i;
    return i < 64u && a[i] == b[i];
}
static int name_valid(const char *s) {
    if (!s || !s[0])
        return 0;
    for (uint32_t i = 0u; i < 64u; ++i) {
        if (!s[i])
            return 1;
        if ((uint8_t)s[i] < 33u || (uint8_t)s[i] > 126u)
            return 0;
    }
    return 0;
}
static int lock(_Atomic uint32_t *p) {
    uint32_t zero = 0u;
    return atomic_compare_exchange_strong_explicit(p, &zero, 1u, memory_order_acquire, memory_order_relaxed);
}
static void unlock(_Atomic uint32_t *p) { atomic_store_explicit(p, 0u, memory_order_release); }
static void *allocate(FxExtensionDomain *d, uint32_t bytes) {
    if (bytes > d->budget - d->used)
        return 0;
    void *p = d->memory.allocate(d->memory.context, bytes);
    if (p) {
        memset(p, 0, bytes);
        d->used += bytes;
    }
    return p;
}
FxExtensionDomain *fx_extensions_create(FxIndexMemory memory, uint32_t budget) {
    if (!memory.allocate || !memory.release || budget < sizeof(FxExtensionDomain))
        return 0;
    FxExtensionDomain *d = memory.allocate(memory.context, (uint32_t)sizeof(*d));
    if (!d)
        return 0;
    memset(d, 0, sizeof(*d));
    d->memory = memory;
    d->budget = budget;
    d->used = (uint32_t)sizeof(*d);
    return d;
}
static void provider_info(Provider *p, FxProviderInfo *out) {
    *out = (FxProviderInfo){.token = p->token,
                            .features = p->features,
                            .version = p->version,
                            .owner = p->owner->owner,
                            .generation = p->owner->generation,
                            .active = atomic_load(&p->active),
                            .calls = atomic_load(&p->calls)};
    memcpy(out->name, p->name, 64u);
}
FxResult fx_provider_publish(FxExtensionDomain *d, FxPlatformOwner *o, const FxProviderDeclaration *p,
                             uint64_t *token) {
    if (!d || !o || !p || !token || !p->call || !p->version || !name_valid(p->name))
        return FX_ARGUMENT;
    if (o->phase != FX_OWNER_RUNNING)
        return FX_STATE;
    Client *c;
    FxResult r = client(d, o, &c);
    if (r != FX_OK)
        return r;
    if (!lock(&d->lock))
        return FX_BUSY;
    r = o->phase == FX_OWNER_RUNNING ? FX_OK : FX_STATE;
    for (Provider *q = d->providers; q; q = q->next)
        if (atomic_load(&q->active) && equal(q->name, p->name)) {
            r = FX_BUSY;
            break;
        }
    if (d->next == UINT64_MAX)
        r = FX_BUDGET;
    if (r == FX_OK) {
        Provider *q = allocate(d, (uint32_t)sizeof(*q));
        if (!q)
            r = FX_BUDGET;
        else {
            q->owner = o;
            q->token = ++d->next;
            q->version = p->version;
            q->features = p->features;
            q->context = p->context;
            q->call = p->call;
            uint32_t i = 0u;
            while (p->name[i]) {
                q->name[i] = p->name[i];
                ++i;
            }
            q->next = d->providers;
            d->providers = q;
            atomic_store_explicit(&q->active, 1u, memory_order_release);
            *token = q->token;
        }
    }
    unlock(&d->lock);
    if (r == FX_OK)
        changed(d);
    return r;
}
FxResult fx_provider_lookup(FxExtensionDomain *d, const FxProviderLookup *request, FxProviderInfo *out) {
    if (!d || !request || !out || !name_valid(request->name) || !request->minimum ||
        request->maximum < request->minimum)
        return FX_ARGUMENT;
    if (!lock(&d->lock))
        return FX_BUSY;
    FxResult result = FX_EMPTY;
    for (Provider *p = d->providers; p; p = p->next)
        if (p->owner->phase == FX_OWNER_RUNNING && atomic_load_explicit(&p->active, memory_order_acquire) &&
            equal(p->name, request->name) && p->version >= request->minimum &&
            p->version <= request->maximum && (p->features & request->features) == request->features) {
            provider_info(p, out);
            result = FX_OK;
            break;
        }
    unlock(&d->lock);
    return result;
}
FxResult fx_provider_remove(FxExtensionDomain *d, FxPlatformOwner *o, uint64_t token) {
    if (!d || !o || !token)
        return FX_ARGUMENT;
    if (!lock(&d->lock))
        return FX_BUSY;
    FxResult result = FX_STALE;
    for (Provider *p = d->providers; p; p = p->next)
        if (p->token == token && p->owner == o) {
            atomic_store_explicit(&p->active, 0u, memory_order_release);
            result = atomic_load_explicit(&p->calls, memory_order_acquire) ? FX_BUSY : FX_OK;
            break;
        }
    unlock(&d->lock);
    if (result == FX_OK || result == FX_BUSY)
        changed(d);
    return result;
}
FxResult fx_provider_call(FxExtensionDomain *d, FxPlatformOwner *o, uint64_t token, uint64_t operation,
                          const void *data, uint32_t bytes, FxServiceReply *reply) {
    if (!d || !o || !reply || (bytes && !data))
        return FX_ARGUMENT;
    if (!lock(&d->lock))
        return FX_BUSY;
    Provider *p = d->providers;
    while (p && p->token != token)
        p = p->next;
    if (!p ||
        (!atomic_load_explicit(&p->active, memory_order_acquire) || p->owner->phase != FX_OWNER_RUNNING)) {
        unlock(&d->lock);
        return FX_STALE;
    }
    atomic_fetch_add_explicit(&p->calls, 1u, memory_order_acq_rel);
    unlock(&d->lock);
    FxProviderClient caller = {1u, sizeof(caller), o->owner, o->generation};
    reply->bytes = 0u;
    FxResult result = p->call(p->context, &caller, operation, data, bytes, reply);
    atomic_fetch_sub_explicit(&p->calls, 1u, memory_order_release);
    if (reply->bytes > FX_SERVICE_BYTES) {
        reply->bytes = 0u;
        return FX_BOUNDS;
    }
    return result;
}
static FxResult client(FxExtensionDomain *d, FxPlatformOwner *o, Client **out) {
    Client *c = (void *)atomic_load_explicit(&o->extensions, memory_order_acquire);
    if (c) {
        *out = c;
        return FX_OK;
    }
    if (!lock(&d->lock))
        return FX_BUSY;
    c = (void *)atomic_load_explicit(&o->extensions, memory_order_acquire);
    if (!c) {
        c = allocate(d, (uint32_t)sizeof(*c));
        if (c) {
            c->owner = o;
            c->domain = d;
            c->next_log = 1u;
            c->next = atomic_load_explicit(&d->clients, memory_order_relaxed);
            atomic_store_explicit(&d->clients, c, memory_order_release);
            atomic_store_explicit(&o->extensions, (uintptr_t)c, memory_order_release);
        }
    }
    unlock(&d->lock);
    *out = c;
    return c ? FX_OK : FX_BUDGET;
}
static FxResult stream_register(Client *c, const FxServiceRequest *q, FxServiceReply *a) {
    if (q->bytes != sizeof(FxStreamRegistration))
        return FX_ARGUMENT;
    FxStreamRegistration p;
    memcpy(&p, q->data, sizeof(p));
    FxStream header;
    if (!name_valid(p.name) || p.reserved || (p.address & 63u) || p.bytes < sizeof(FxStream))
        return FX_ARGUMENT;
    /* Validate the ENTIRE shared range as module-owned writable data before
     * retaining a pointer. No per-frame copying and no executable text alias. */
    FxResult r = fx_platform_module_range(c->owner, p.address, p.bytes, 1u);
    if (r != FX_OK)
        return r;
    r = fx_platform_module_transfer(c->owner, p.address, &header, sizeof(header), 0u);
    if (r != FX_OK)
        return r;
    uint32_t bytes, stride;
    if (header.abi != FX_STREAM_ABI || header.epoch != p.epoch || !header.clock_id || !header.clock_hz ||
        fx_stream_layout(header.slots, header.payload, &bytes, &stride) != FX_OK || bytes != p.bytes ||
        header.bytes != bytes || header.stride != stride)
        return FX_INTEGRITY;
    FxExtensionDomain *d = c->domain;
    if (!lock(&d->lock))
        return FX_BUSY;
    r = FX_OK;
    for (Stream *s = d->streams; s; s = s->next)
        if (s->owner == c->owner && s->info.active && equal(s->info.name, p.name)) {
            r = FX_BUSY;
            break;
        }
    if (d->next == UINT64_MAX)
        r = FX_BUDGET;
    if (r == FX_OK) {
        Stream *s = allocate(d, (uint32_t)sizeof(*s));
        if (!s)
            r = FX_BUDGET;
        else {
            s->owner = c->owner;
            s->info = (FxStreamInfo){.token = ++d->next,
                                     .address = p.address,
                                     .epoch = p.epoch,
                                     .bytes = bytes,
                                     .slots = header.slots,
                                     .payload = header.payload,
                                     .owner = c->owner->owner,
                                     .generation = c->owner->generation,
                                     .active = 1u,
                                     .clock_id = header.clock_id,
                                     .clock_hz = header.clock_hz};
            memcpy(s->info.name, p.name, 64u);
            s->next = d->streams;
            d->streams = s;
            memcpy(a->data, &s->info, sizeof(s->info));
            a->bytes = sizeof(s->info);
        }
    }
    unlock(&d->lock);
    return r;
}
FxResult fx_extensions_stream(FxExtensionDomain *d, uint64_t token, FxStreamInfo *out) {
    if (!d || !out || !token)
        return FX_ARGUMENT;
    if (!lock(&d->lock))
        return FX_BUSY;
    FxResult r = FX_STALE;
    for (Stream *s = d->streams; s; s = s->next)
        if (s->info.token == token && s->info.active) {
            *out = s->info;
            r = FX_OK;
            break;
        }
    unlock(&d->lock);
    return r;
}
static FxResult streams(Client *c, const FxServiceRequest *q, FxServiceReply *a) {
    if (q->arg[2] == FX_STREAM_REGISTER)
        return stream_register(c, q, a);
    if (q->bytes)
        return FX_ARGUMENT;
    if (q->arg[2] == FX_STREAM_QUERY) {
        a->value[0] = FX_STREAM_ABI;
        a->value[1] = FX_STREAM_HEADER;
        a->value[2] = FX_STREAM_ALIGNMENT;
        return FX_OK;
    }
    if (q->arg[2] == FX_STREAM_LOOKUP) {
        FxStreamInfo info;
        FxResult r = fx_extensions_stream(c->domain, q->arg[3], &info);
        if (r == FX_OK) {
            memcpy(a->data, &info, sizeof(info));
            a->bytes = sizeof(info);
        }
        return r;
    }
    if (q->arg[2] != FX_STREAM_REMOVE)
        return FX_ARGUMENT;
    FxExtensionDomain *d = c->domain;
    if (!lock(&d->lock))
        return FX_BUSY;
    FxResult r = FX_STALE;
    for (Stream *s = d->streams; s; s = s->next)
        if (s->info.token == q->arg[3] && s->owner == c->owner) {
            s->info.active = 0u;
            fx_stream_close((void *)(uintptr_t)s->info.address);
            r = FX_OK;
            break;
        }
    unlock(&d->lock);
    return r;
}
static FxResult logs(Client *c, const FxServiceRequest *q, FxServiceReply *a) {
    if (!c->logs) {
        if (!lock(&c->domain->lock))
            return FX_BUSY;
        c->log_capacity = 32u;
        c->logs = allocate(c->domain, c->log_capacity * (uint32_t)sizeof(FxLogRecord));
        unlock(&c->domain->lock);
        if (!c->logs)
            return FX_BUDGET;
    }
    uint64_t oldest = c->next_log > c->log_capacity ? c->next_log - c->log_capacity : 1u;
    if (q->arg[2] == FX_LOG_QUERY) {
        if (q->bytes)
            return FX_ARGUMENT;
        FxLogInfo info = {oldest, c->next_log, c->dropped, c->log_capacity, sizeof(FxLogRecord)};
        memcpy(a->data, &info, sizeof(info));
        a->bytes = sizeof(info);
        return FX_OK;
    }
    if (q->arg[2] == FX_LOG_WRITE) {
        if (q->bytes != sizeof(FxLogWrite))
            return FX_ARGUMENT;
        FxLogWrite p;
        memcpy(&p, q->data, sizeof(p));
        if (p.bytes > FX_LOG_TEXT || p.level > FX_LOG_ERROR)
            return FX_ARGUMENT;
        if (c->next_log == UINT64_MAX)
            return FX_BUDGET;
        FxLogRecord *r = &c->logs[(c->next_log - 1u) % c->log_capacity];
        *r = (FxLogRecord){.sequence = c->next_log++,
                           .time_ms = c->owner->module_time_ms,
                           .owner = c->owner->owner,
                           .generation = c->owner->generation,
                           .level = p.level,
                           .bytes = p.bytes};
        memcpy(r->text, p.text, p.bytes);
        if (r->sequence > c->log_capacity)
            ++c->dropped;
        a->value[0] = r->sequence;
        return FX_OK;
    }
    if (q->arg[2] == FX_LOG_READ) {
        if (q->bytes)
            return FX_ARGUMENT;
        uint64_t at = q->arg[3] ? q->arg[3] : oldest;
        a->value[0] = oldest;
        a->value[1] = c->next_log;
        if (at < oldest)
            return FX_STALE;
        if (at >= c->next_log)
            return FX_EMPTY;
        memcpy(a->data, &c->logs[(at - 1u) % c->log_capacity], sizeof(FxLogRecord));
        a->bytes = sizeof(FxLogRecord);
        return FX_OK;
    }
    return FX_ARGUMENT;
}
static FxResult times(Client *c, const FxServiceRequest *q, FxServiceReply *a) {
    if (q->arg[2] == FX_TIME_QUERY) {
        if (q->bytes)
            return FX_ARGUMENT;
        a->value[0] = c->owner->module_time_ms;
        a->value[1] = 1000u;
        const FxNamedBinding *clock = fx_platform_find(c->owner, "core.context");
        if (clock && c->owner->platform->io.call) {
            uint64_t args[8] = {FX_CONTEXT_COUNTER}, counter = 0u, hz = 0u;
            FxPlatformIO io = c->owner->platform->io;
            if (io.call(io.context, clock, args, &counter) == FX_OK) {
                args[0] = FX_CONTEXT_FREQUENCY;
                if (io.call(io.context, clock, args, &hz) == FX_OK && hz) {
                    a->value[2] = counter;
                    a->value[3] = hz;
                    if (hz <= UINT32_MAX)
                        (void)fx_time_scale(counter, (uint32_t)hz, 1000u, &a->value[0]);
                }
            }
        }
        return FX_OK;
    }
    if (q->arg[2] == FX_TIME_CREATE) {
        if (q->bytes || q->arg[3])
            return FX_ARGUMENT;
        if (!lock(&c->domain->lock))
            return FX_BUSY;
        Timer *t = c->timers;
        while (t && !t->released)
            t = t->next;
        if (c->domain->next == UINT64_MAX)
            t = 0;
        else if (!t) {
            t = allocate(c->domain, (uint32_t)sizeof(*t));
            if (t) {
                t->next = c->timers;
                c->timers = t;
            }
        }
        if (t) {
            t->released = 0u;
            t->info = (FxTimerInfo){.token = ++c->domain->next};
            a->value[0] = t->info.token;
        }
        unlock(&c->domain->lock);
        return t ? FX_OK : FX_BUDGET;
    }
    FxTimerRequest p = {.token = q->arg[3]};
    if (q->arg[2] == FX_TIME_ARM) {
        if (q->bytes != sizeof(p))
            return FX_ARGUMENT;
        memcpy(&p, q->data, sizeof(p));
    } else if (q->bytes)
        return FX_ARGUMENT;
    Timer *t = c->timers;
    while (t && t->info.token != p.token)
        t = t->next;
    if (!t || t->released)
        return FX_STALE;
    if (q->arg[2] == FX_TIME_ARM) {
        if (!p.deadline_ms)
            return FX_ARGUMENT;
        if (!t->info.armed && !c->timers_armed++)
            atomic_fetch_add(&c->owner->tick_subscriptions, 1u);
        t->info.deadline_ms = p.deadline_ms;
        t->info.period_ms = p.period_ms;
        t->info.expirations = 0u;
        t->info.armed = 1u;
    } else if (q->arg[2] == FX_TIME_CANCEL || q->arg[2] == FX_TIME_RELEASE) {
        if (t->info.armed && !--c->timers_armed)
            atomic_fetch_sub(&c->owner->tick_subscriptions, 1u);
        t->info.armed = 0u;
        if (q->arg[2] == FX_TIME_RELEASE)
            t->released = 1u;
    } else if (q->arg[2] != FX_TIME_POLL)
        return FX_ARGUMENT;
    memcpy(a->data, &t->info, sizeof(t->info));
    a->bytes = sizeof(t->info);
    return FX_OK;
}
void fx_extensions_pulse(FxPlatformOwner *o, uint64_t now) {
    if (!o)
        return;
    Client *c = (void *)atomic_load_explicit(&o->extensions, memory_order_acquire);
    o->module_time_ms = now;
    if (!c || !lock(&c->lock))
        return;
    c->now = now;
    uint32_t wake = 0u;
    for (Timer *t = c->timers; t; t = t->next)
        if (t->info.armed && now >= t->info.deadline_ms) {
            uint64_t n = t->info.period_ms ? (now - t->info.deadline_ms) / t->info.period_ms + 1u : 1u;
            if (n > UINT64_MAX - t->info.expirations)
                t->info.expirations = UINT64_MAX;
            else
                t->info.expirations += n;
            if (!t->info.period_ms || n > (UINT64_MAX - t->info.deadline_ms) / t->info.period_ms) {
                t->info.armed = 0u;
                if (!--c->timers_armed)
                    atomic_fetch_sub(&o->tick_subscriptions, 1u);
            } else
                t->info.deadline_ms += n * t->info.period_ms;
            wake = 1u;
        }
    unlock(&c->lock);
    if (wake)
        fx_platform_wake(o);
}
FxResult fx_extensions_stop(FxPlatformOwner *o) {
    Client *c = (void *)atomic_load_explicit(&o->extensions, memory_order_acquire);
    if (!c)
        return FX_OK;
    if (!lock(&c->lock))
        return FX_BUSY;
    c->closed = 1u;
    if (c->timers_armed) {
        c->timers_armed = 0u;
        atomic_fetch_sub(&o->tick_subscriptions, 1u);
    }
    for (Timer *t = c->timers; t; t = t->next)
        t->info.armed = 0u;
    unlock(&c->lock);
    FxExtensionDomain *d = c->domain;
    if (!lock(&d->lock))
        return FX_BUSY;
    uint32_t busy = 0u;
    for (Provider *p = d->providers; p; p = p->next)
        if (p->owner == o) {
            atomic_store_explicit(&p->active, 0u, memory_order_release);
            if (atomic_load(&p->calls))
                busy = 1u;
        }
    for (Stream *s = d->streams; s; s = s->next)
        if (s->owner == o) {
            s->info.active = 0u;
            fx_stream_close((void *)(uintptr_t)s->info.address);
        }
    unlock(&d->lock);
    return busy ? FX_BUSY : FX_OK;
}
FxResult fx_extensions_resource(void *context, uint32_t operation, const FxNamedBinding *b,
                                FxPlatformOwner *o, uint64_t token, uint64_t *cookie,
                                const FxServiceRequest *q, FxServiceReply *a) {
    (void)token;
    FxExtensionDomain *d = context ? context : fx_native_extensions(o);
    if (!d)
        return FX_BUDGET;
    if (operation == FX_RESOURCE_ATTACH) {
        Client *c;
        FxResult result = client(d, o, &c);
        if (result != FX_OK)
            return result;
        *cookie = (uintptr_t)c;
        return FX_OK;
    }
    if (operation == FX_RESOURCE_DETACH) {
        *cookie = 0u;
        return FX_OK;
    }
    if (operation == FX_RESOURCE_PANIC)
        return fx_extensions_stop(o);
    Client *c = (void *)(uintptr_t)*cookie;
    if (operation != FX_RESOURCE_CALL || !c || c->owner != o || !q || !a)
        return FX_ARGUMENT;
    if (!lock(&c->lock))
        return FX_BUSY;
    FxResult r = FX_STATE;
    if (!c->closed) {
        if (equal(b->name, "core.self")) {
            if (q->arg[2] || q->bytes)
                r = FX_ARGUMENT;
            else {
                FxModuleSelfInfo info = {.abi = 1u,
                                         .bytes = sizeof(info),
                                         .owner = o->owner,
                                         .generation = o->generation,
                                         .purpose = o->kind,
                                         .entry_abi = o->module_entry_abi,
                                         .image_bytes = o->module_bytes,
                                         .capabilities = o->capabilities,
                                         .image = (uintptr_t)o->module_image};
                memcpy(info.target, o->platform->module_identity, 32u);
                memcpy(a->data, &info, sizeof(info));
                a->bytes = sizeof(info);
                r = FX_OK;
            }
        } else if (equal(b->name, "core.streams"))
            r = streams(c, q, a);
        else if (equal(b->name, "core.logs"))
            r = logs(c, q, a);
        else if (equal(b->name, "core.time"))
            r = times(c, q, a);
        else if (q->arg[2] == FX_PROVIDER_LOOKUP && q->bytes == sizeof(FxProviderLookup)) {
            FxProviderLookup lookup;
            FxProviderInfo info;
            memcpy(&lookup, q->data, sizeof(lookup));
            r = fx_provider_lookup(d, &lookup, &info);
            if (r == FX_OK) {
                memcpy(a->data, &info, sizeof(info));
                a->bytes = sizeof(info);
            }
        } else if (q->arg[2] == FX_PROVIDER_REMOVE && !q->bytes) {
            r = fx_provider_remove(d, o, q->arg[3]);
        } else if (q->arg[2] == FX_PROVIDER_ENUM && !q->bytes) {
            if (!lock(&d->lock))
                r = FX_BUSY;
            else {
                Provider *selected = 0;
                for (Provider *p = d->providers; p; p = p->next)
                    if (p->token > q->arg[3] && p->owner->phase == FX_OWNER_RUNNING &&
                        atomic_load(&p->active) && (!selected || p->token < selected->token))
                        selected = p;
                r = selected ? FX_OK : FX_EMPTY;
                if (selected) {
                    FxProviderInfo info;
                    provider_info(selected, &info);
                    memcpy(a->data, &info, sizeof(info));
                    a->bytes = sizeof(info);
                    a->value[0] = selected->token;
                }
                unlock(&d->lock);
            }
        } else if (q->arg[2] == FX_PROVIDER_CALL && q->bytes >= sizeof(FxProviderRequest)) {
            FxProviderRequest p;
            memcpy(&p, q->data, sizeof(p));
            /* Never hold client/domain locks while calling extension code. */
            unlock(&c->lock);
            return fx_provider_call(d, o, p.token, p.operation, q->data + sizeof(p),
                                    q->bytes - (uint32_t)sizeof(p), a);
        } else if (q->arg[2] == FX_PROVIDER_QUERY && !q->bytes) {
            if (!lock(&d->lock))
                r = FX_BUSY;
            else {
                a->value[0] = FX_PROVIDER_ABI;
                a->value[1] = d->budget;
                a->value[2] = d->used;
                a->value[3] = atomic_load(&d->revision);
                r = FX_OK;
                unlock(&d->lock);
            }
        } else
            r = FX_ARGUMENT;
    }
    unlock(&c->lock);
    return r;
}
