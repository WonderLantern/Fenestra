#include "fenestra.h"
#include "fenestra_memory.h"

static void record_fault(FxRuntime *r, FxResult reason, uint64_t detail) {
    if (r->fault_count == 0u) {
        r->first_fault.reason = reason;
        r->first_fault.phase = r->phase;
        r->first_fault.generation = r->generation;
        r->first_fault.detail = detail;
    }
    if (r->fault_count != UINT32_MAX) ++r->fault_count;
}

static int valid(const FxRuntime *r) { return r != NULL && r->abi == FX_ABI; }

static int deadline(uint64_t now, uint64_t duration, uint64_t *out) {
    if (duration == 0u || duration > UINT64_MAX - now) return 0;
    *out = now + duration;
    return 1;
}

void fx_init(FxRuntime *r, uint64_t caps, uint64_t budget) {
    if (r == NULL) return;
    memset(r, 0, sizeof(*r));
    r->abi = FX_ABI;
    r->phase = FX_BYPASS;
    r->verified_capabilities = caps;
    r->ram_budget = budget;
}

FxResult fx_panic(FxRuntime *r, FxResult reason, uint64_t detail) {
    if (!valid(r) || reason <= FX_OK || reason > FX_EMPTY) return FX_ARGUMENT;
    record_fault(r, reason, detail);
    r->phase = FX_PANIC;
    /* Reservations and active leases deliberately survive panic. */
    return reason;
}

FxResult fx_reject(FxRuntime *r, FxResult reason, uint64_t detail) {
    if (!valid(r) || reason <= FX_OK || reason > FX_EMPTY) return FX_ARGUMENT;
    if (r->phase != FX_VALIDATING || r->outstanding != 0u) return FX_STATE;
    /* Caller must have closed I/O and retained no external references. */
    record_fault(r, reason, detail);
    r->reserved_bytes = 0u;
    r->phase = FX_DISABLED;
    return reason;
}

FxResult fx_begin(FxRuntime *r, uint64_t required, uint64_t bytes,
                  uint64_t now, uint64_t timeout) {
    uint64_t end;
    if (!valid(r)) return FX_ARGUMENT;
    if (r->attempted || r->phase != FX_BYPASS) return FX_STATE;
    r->attempted = 1u;
    r->last_time = now;
    if (!deadline(now, timeout, &end) || bytes == 0u) {
        record_fault(r, FX_ARGUMENT, timeout);
        r->phase = FX_DISABLED;
        return FX_ARGUMENT;
    }
    if ((required & ~r->verified_capabilities) != 0u) {
        record_fault(r, FX_CAPABILITY, required & ~r->verified_capabilities);
        r->phase = FX_DISABLED;
        return FX_CAPABILITY;
    }
    if (bytes > r->ram_budget) {
        record_fault(r, FX_BUDGET, bytes);
        r->phase = FX_DISABLED;
        return FX_BUDGET;
    }
    r->generation = 1u; /* One generation/attempt per initialized instance. */
    r->reserved_bytes = bytes;
    r->deadline = end;
    r->watchdog_interval = timeout;
    r->phase = FX_VALIDATING;
    return FX_OK;
}

FxResult fx_poll(FxRuntime *r, uint64_t now) {
    if (!valid(r)) return FX_ARGUMENT;
    if (r->phase == FX_PANIC) return FX_STATE;
    if (r->phase == FX_BYPASS || r->phase == FX_DISABLED) return FX_OK;
    if (now < r->last_time) return fx_panic(r, FX_INTEGRITY, now);
    r->last_time = now;
    if (!(r->phase == FX_ACTIVE && r->execution_idle) && now >= r->deadline)
        return fx_panic(r, FX_TIMEOUT, now);
    return FX_OK;
}

FxResult fx_prepared(FxRuntime *r, uint64_t now) {
    FxResult result;
    if (!valid(r)) return FX_ARGUMENT;
    if (r->phase != FX_VALIDATING) return FX_STATE;
    result = fx_poll(r, now);
    if (result != FX_OK) return result;
    r->phase = FX_PREPARED;
    return FX_OK;
}

FxResult fx_publish(FxRuntime *r, uint64_t now) {
    uint64_t end;
    FxResult result;
    if (!valid(r)) return FX_ARGUMENT;
    if (r->phase != FX_PREPARED) return FX_STATE;
    result = fx_poll(r, now);
    if (result != FX_OK) return result;
    if (!deadline(now, r->watchdog_interval, &end)) return fx_panic(r, FX_BOUNDS, now);
    r->phase = FX_ACTIVE;
    r->deadline = end;
    return FX_OK;
}

FxResult fx_acquire(FxRuntime *r, uint32_t generation, uint64_t now, FxLease *out) {
    uint32_t i;
    FxResult result;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (!valid(r) || out == NULL) return FX_ARGUMENT;
    if (generation != r->generation) return FX_STALE;
    if (r->phase != FX_ACTIVE || r->execution_idle) return FX_STATE;
    result = fx_poll(r, now);
    if (result != FX_OK) return result;
    if (r->outstanding >= FX_MAX_LEASES) return FX_BUSY;
    if (r->next_lease == UINT64_MAX) return fx_panic(r, FX_BOUNDS, r->next_lease);
    for (i = 0u; i < FX_MAX_LEASES; ++i) {
        if (r->leases[i].id == 0u) {
            r->leases[i].id = ++r->next_lease;
            r->leases[i].generation = generation;
            ++r->outstanding;
            *out = r->leases[i];
            return FX_OK;
        }
    }
    return fx_panic(r, FX_INTEGRITY, r->outstanding);
}

static void finish_drain(FxRuntime *r) {
    if (r->phase == FX_QUIESCING && r->drain_confirmed && r->outstanding == 0u) {
        r->reserved_bytes = 0u;
        r->phase = FX_DISABLED;
    }
}

FxResult fx_release(FxRuntime *r, FxLease lease, uint64_t now) {
    uint32_t i;
    FxResult result;
    if (!valid(r)) return FX_ARGUMENT;
    if (lease.id == 0u || lease.generation != r->generation) return FX_STALE;
    if (r->phase != FX_PANIC) {
        result = fx_poll(r, now);
        if (result != FX_OK) return result;
    }
    for (i = 0u; i < FX_MAX_LEASES; ++i) {
        if (r->leases[i].id == lease.id && r->leases[i].generation == lease.generation) {
            if (r->outstanding == 0u) return fx_panic(r, FX_INTEGRITY, lease.id);
            memset(&r->leases[i], 0, sizeof(r->leases[i]));
            --r->outstanding;
            finish_drain(r);
            return FX_OK;
        }
    }
    return FX_STALE;
}

FxResult fx_disable(FxRuntime *r, uint64_t now, uint64_t timeout) {
    uint64_t end;
    FxResult result;
    if (!valid(r)) return FX_ARGUMENT;
    if (r->phase != FX_ACTIVE && r->phase != FX_PREPARED && r->phase != FX_VALIDATING)
        return FX_STATE;
    if (!deadline(now, timeout, &end)) return FX_ARGUMENT;
    result = fx_poll(r, now);
    if (result != FX_OK) return result;
    r->drain_confirmed = 0u;
    r->phase = FX_QUIESCING;
    r->execution_idle = 0u;
    r->deadline = end;
    return FX_OK;
}

FxResult fx_confirm_quiesced(FxRuntime *r, uint32_t generation, uint64_t now) {
    FxResult result;
    if (!valid(r)) return FX_ARGUMENT;
    if (generation != r->generation) return FX_STALE;
    if (r->phase != FX_QUIESCING) return FX_STATE;
    result = fx_poll(r, now);
    if (result != FX_OK) return result;
    r->drain_confirmed = 1u;
    finish_drain(r);
    return FX_OK;
}

FxResult fx_heartbeat(FxRuntime *r, uint64_t now) {
    uint64_t end;
    FxResult result;
    if (!valid(r)) return FX_ARGUMENT;
    if (r->phase != FX_ACTIVE) return FX_STATE;
    result = fx_poll(r, now); /* A late heartbeat cannot revive a timed-out instance. */
    if (result != FX_OK) return result;
    if (r->execution_idle) return FX_OK;
    if (!deadline(now, r->watchdog_interval, &end)) return fx_panic(r, FX_BOUNDS, now);
    r->deadline = end;
    return FX_OK;
}

FxResult fx_execution_idle(FxRuntime *r, uint64_t now) {
    if (!valid(r)) return FX_ARGUMENT;
    if (r->phase != FX_ACTIVE || r->execution_idle) return FX_STATE;
    if (r->outstanding) return FX_BUSY;
    FxResult result = fx_poll(r, now);
    if (result != FX_OK) return result;
    r->execution_idle = 1u;
    return FX_OK;
}

FxResult fx_execution_resume(FxRuntime *r, uint64_t now) {
    uint64_t end;
    if (!valid(r)) return FX_ARGUMENT;
    if (r->phase != FX_ACTIVE) return FX_STATE;
    FxResult result = fx_poll(r, now);
    if (result != FX_OK || !r->execution_idle) return result;
    if (r->outstanding) return fx_panic(r, FX_INTEGRITY, r->outstanding);
    if (!deadline(now, r->watchdog_interval, &end)) return fx_panic(r, FX_BOUNDS, now);
    r->deadline = end;
    r->execution_idle = 0u;
    return FX_OK;
}

int fx_release_allowed(const FxRuntime *r) {
    return valid(r) && (r->phase == FX_BYPASS || r->phase == FX_DISABLED) &&
           r->outstanding == 0u && r->reserved_bytes == 0u;
}
