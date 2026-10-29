#include "fenestra_debug.h"
#include "fenestra_memory.h"

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t)get32(p + 4) << 32; }
static void put32(uint8_t *p, uint32_t n) {
    unsigned i; for (i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8u * i));
}
static void put64(uint8_t *p, uint64_t n) { put32(p, (uint32_t)n); put32(p + 4, (uint32_t)(n >> 32)); }

uint32_t fx_debug_crc(const uint8_t *p, uint32_t bytes) {
    uint32_t crc = UINT32_MAX, i, bit;
    if (p == NULL) return 0;
    for (i = 0; i < bytes; ++i) {
        crc ^= i >= 28u && i < 32u ? 0u : p[i];
        for (bit = 0; bit < 8u; ++bit) crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}

void fx_debug_init(FxDebug *d, const FxLoader *loader) {
    if (d == NULL) return;
    memset(d, 0, sizeof(*d)); d->loader = loader;
}
void fx_debug_disconnect(FxDebug *d) {
    if (d == NULL) return;
    d->connected = d->greeted = d->last_id = 0;
    d->session = 0;
    d->rx_bytes = d->tx_bytes = d->tx_offset = d->previous_bytes = d->previous_reply_bytes = 0;
    memset(d->rx, 0, sizeof(d->rx)); memset(d->tx, 0, sizeof(d->tx));
    memset(d->previous, 0, sizeof(d->previous)); memset(d->previous_reply, 0, sizeof(d->previous_reply));
}
void fx_debug_enable(FxDebug *d, int enabled) {
    if (d == NULL) return;
    d->enabled = enabled != 0 ? 1u : 0u;
    if (!d->enabled) fx_debug_disconnect(d);
}
FxResult fx_debug_connect(FxDebug *d, uint64_t session, uint64_t now) {
    if (d == NULL || d->loader == NULL || session == 0) return FX_ARGUMENT;
    if (!d->enabled || d->loader->runtime.phase != FX_ACTIVE) return FX_STATE;
    if (d->connected) return FX_BUSY;
    if (session <= d->last_session) return FX_STALE;
    fx_debug_disconnect(d);
    d->last_session = d->session = session; d->last_time = now; d->connected = 1;
    return FX_OK;
}
static void increment(uint32_t *n) { if (*n != UINT32_MAX) ++*n; }
static FxResult bad(FxDebug *d, FxResult result) {
    increment(&d->bad_frames); d->rx_bytes = 0; return result;
}
FxResult fx_debug_tick(FxDebug *d, uint64_t now) {
    if (d == NULL) return FX_ARGUMENT;
    if (!d->connected) return FX_STATE;
    if (now < d->last_time) { fx_debug_disconnect(d); return FX_INTEGRITY; }
    d->last_time = now;
    if (d->rx_bytes && now - d->fragment_started >= FX_DEBUG_FRAGMENT_TIMEOUT)
        return bad(d, FX_TIMEOUT);
    return FX_OK;
}
void fx_debug_log(FxDebug *d, uint32_t level, uint64_t now, const char *message) {
    uint8_t *p; uint32_t slot, i;
    if (d == NULL || message == NULL) return;
    if (d->log_sequence == UINT32_MAX || d->log_count == FX_DEBUG_LOG_COUNT) {
        increment(&d->dropped_logs); return;
    }
    slot = (d->log_head + d->log_count) % FX_DEBUG_LOG_COUNT;
    p = d->logs[slot]; memset(p, 0, FX_DEBUG_LOG_BYTES);
    put64(p, now); put32(p + 8, ++d->log_sequence); put32(p + 12, level);
    for (i = 0; i < 47u && message[i] != 0; ++i) p[16u + i] = (uint8_t)message[i];
    ++d->log_count;
}
static void status(FxDebug *d, uint8_t *p) {
    const FxRuntime *r = &d->loader->runtime;
    put32(p, FX_ABI); put32(p + 4, (uint32_t)r->phase); put32(p + 8, r->generation);
    put32(p + 12, r->outstanding); put64(p + 16, r->reserved_bytes); put64(p + 24, r->ram_budget);
    put32(p + 32, (uint32_t)r->first_fault.reason); put32(p + 36, (uint32_t)r->first_fault.phase);
    put32(p + 40, r->first_fault.generation); put32(p + 44, r->fault_count);
    put64(p + 48, r->first_fault.detail); put32(p + 56, d->dropped_logs);
    put32(p + 60, d->bad_frames); put64(p + 64, r->verified_capabilities);
}
static FxResult dispatch(FxDebug *d) {
    uint32_t id = get32(d->rx + 8), payload = get32(d->rx + 12), bytes = 0, i, count;
    uint32_t opcode = (uint32_t)d->rx[6] | (uint32_t)d->rx[7] << 8;
    FxResult result = FX_OK;
    if (get32(d->rx + 28) != fx_debug_crc(d->rx, d->rx_bytes)) return bad(d, FX_INTEGRITY);
    if (d->rx_bytes == d->previous_bytes && memcmp(d->rx, d->previous, d->rx_bytes) == 0) {
        memcpy(d->tx, d->previous_reply, d->previous_reply_bytes);
        d->tx_bytes = d->previous_reply_bytes; d->tx_offset = 0; d->rx_bytes = 0; return FX_OK;
    }
    if (id == 0 || id <= d->last_id) return bad(d, FX_STALE);
    if ((!d->greeted && (opcode != FX_DBG_HELLO || get64(d->rx + 16) != 0)) ||
        (d->greeted && get64(d->rx + 16) != d->session)) return bad(d, FX_STALE);
    memset(d->tx, 0, sizeof(d->tx));
    if (payload != 0) result = FX_ARGUMENT; /* v1 commands have no arguments. */
    else if (opcode == FX_DBG_HELLO) {
        if (d->greeted) result = FX_STATE;
        else {
            d->greeted = 1; bytes = 64;
            put32(d->tx + 32, 1u); put32(d->tx + 36, FX_DEBUG_PAYLOAD);
            put32(d->tx + 40, FX_ABI); put32(d->tx + 44, 15u); /* four read-only commands */
            static const char project[] = "Fenestra / WonderLantern";
            memcpy(d->tx + 48, project, sizeof(project));
            memcpy(d->tx + 80, "ZR unbound", 10u);
        }
    } else if (opcode == FX_DBG_STATUS) { bytes = 72; status(d, d->tx + 32); }
    else if (opcode == FX_DBG_LOGS) {
        count = d->log_count > 3u ? 3u : d->log_count;
        put32(d->tx + 32, count); put32(d->tx + 36, d->dropped_logs); bytes = 8u + count * FX_DEBUG_LOG_BYTES;
        for (i = 0; i < count; ++i) {
            memcpy(d->tx + 40u + i * FX_DEBUG_LOG_BYTES, d->logs[d->log_head], FX_DEBUG_LOG_BYTES);
            d->log_head = (d->log_head + 1u) % FX_DEBUG_LOG_COUNT; --d->log_count;
        }
    } else if (opcode == FX_DBG_MODULE) {
        bytes = 96; put32(d->tx + 32, d->loader->module.prepared);
        put32(d->tx + 36, d->loader->module.bytes); put32(d->tx + 40, d->loader->module.entry_offset);
        put32(d->tx + 44, d->loader->card.verified);
        /* No raw addresses, memory read/write or native-call opcode. */
        put32(d->tx + 48, d->loader->card.bytes); put32(d->tx + 52, d->loader->card.read_calls);
        put32(d->tx + 56, (uint32_t)d->loader->card.primary);
        put32(d->tx + 60, (uint32_t)d->loader->card.cleanup);
        memcpy(d->tx + 64, d->loader->package_sha256, 32u);
        memcpy(d->tx + 96, d->loader->target_firmware_sha256, 32u);
    } else result = FX_CAPABILITY;
    memcpy(d->tx, d->rx, 12); put32(d->tx + 12, bytes); put64(d->tx + 16, d->session);
    put32(d->tx + 24, 0x80000000u | (uint32_t)result);
    d->tx_bytes = FX_DEBUG_HEADER + bytes; d->tx_offset = 0;
    put32(d->tx + 28, fx_debug_crc(d->tx, d->tx_bytes));
    d->last_id = id; d->previous_bytes = d->rx_bytes; d->previous_reply_bytes = d->tx_bytes;
    memcpy(d->previous, d->rx, d->rx_bytes); memcpy(d->previous_reply, d->tx, d->tx_bytes);
    d->rx_bytes = 0; return FX_OK;
}
FxResult fx_debug_feed(FxDebug *d, const uint8_t *input, uint32_t bytes, uint64_t now, uint32_t *consumed) {
    uint32_t required, copy; FxResult result;
    if (consumed != NULL) *consumed = 0;
    if (d == NULL || input == NULL || consumed == NULL || bytes == 0) return FX_ARGUMENT;
    if (!d->enabled || !d->connected) return FX_STATE;
    result = fx_debug_tick(d, now); if (result != FX_OK) return result;
    if (d->tx_bytes != 0) return FX_BUSY; /* retry after the IN response drains */
    if (d->rx_bytes == 0) d->fragment_started = now;
    while (*consumed < bytes) {
        required = d->rx_bytes < FX_DEBUG_HEADER ? FX_DEBUG_HEADER : FX_DEBUG_HEADER + get32(d->rx + 12);
        copy = required - d->rx_bytes;
        if (copy > bytes - *consumed) copy = bytes - *consumed;
        memcpy(d->rx + d->rx_bytes, input + *consumed, copy); d->rx_bytes += copy; *consumed += copy;
        if (d->rx_bytes < FX_DEBUG_HEADER) continue;
        if (memcmp(d->rx, "FXDB", 4) != 0 || d->rx[4] != 1 || d->rx[5] != 0 ||
            get32(d->rx + 24) != 0) return bad(d, FX_ARGUMENT);
        if (get32(d->rx + 12) > FX_DEBUG_PAYLOAD) return bad(d, FX_BOUNDS);
        if (d->rx_bytes == FX_DEBUG_HEADER + get32(d->rx + 12)) return dispatch(d);
    }
    return FX_OK;
}
FxResult fx_debug_read(FxDebug *d, uint8_t *out, uint32_t capacity, uint32_t *bytes) {
    uint32_t copy;
    if (bytes != NULL) *bytes = 0;
    if (d == NULL || out == NULL || bytes == NULL || capacity == 0) return FX_ARGUMENT;
    if (!d->connected || !d->enabled) return FX_STATE;
    if (d->tx_bytes == 0) return FX_EMPTY;
    copy = d->tx_bytes - d->tx_offset; if (copy > capacity) copy = capacity;
    memcpy(out, d->tx + d->tx_offset, copy); *bytes = copy; d->tx_offset += copy;
    if (d->tx_offset == d->tx_bytes) d->tx_bytes = d->tx_offset = 0;
    return FX_OK;
}
