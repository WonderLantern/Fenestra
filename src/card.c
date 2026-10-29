#include "fenestra.h"
#include "fenestra_memory.h"

FxResult fx_card_read(const FxCardIO *io, const char *path, uint32_t expected_bytes,
                      const uint8_t expected_sha256[32], uint8_t *buffer,
                      uint32_t capacity, FxCardReceipt *out) {
    uint64_t size = 0u;
    FxResult result = FX_OK;
    uint32_t loaded = 0u;
    uint8_t actual_digest[32];
    if (out == NULL) return FX_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (io == NULL || path == NULL || expected_sha256 == NULL || buffer == NULL ||
        io->open == NULL || io->read == NULL || io->close == NULL || expected_bytes == 0u ||
        expected_bytes > FX_CARD_LIMIT || expected_bytes > capacity) {
        out->primary = FX_ARGUMENT;
        return FX_ARGUMENT;
    }
    /* open failure must mean no handle acquired; otherwise this ABI is unsuitable. */
    if (io->open(io->context, path, &size) != 0) {
        memset(buffer, 0, expected_bytes);
        out->primary = FX_IO;
        return FX_IO;
    }
    if (size != expected_bytes) result = FX_BOUNDS;
    while (result == FX_OK && loaded < expected_bytes) {
        uint32_t actual = 0u;
        uint32_t requested = expected_bytes - loaded;
        if (out->read_calls == FX_READ_CALL_LIMIT) { result = FX_IO; break; }
        if (requested > 65536u) requested = 65536u;
        ++out->read_calls;
        if (io->read(io->context, buffer + loaded, requested, &actual) != 0) {
            result = FX_IO;
        } else if (actual == 0u || actual > requested) {
            result = FX_BOUNDS;
        } else {
            loaded += actual;
        }
    }
    if (io->close(io->context) != 0) {
        out->cleanup = FX_CLOSE;
        if (result == FX_OK) result = FX_CLOSE;
    }
    if (result == FX_OK) {
        fx_sha256(buffer, loaded, actual_digest);
        if (memcmp(actual_digest, expected_sha256, sizeof(actual_digest)) != 0)
            result = FX_INTEGRITY;
    }
    out->primary = result;
    if (result == FX_OK) {
        out->bytes = loaded;
        out->verified = 1u;
    } else {
        memset(buffer, 0, expected_bytes);
    }
    return result;
}
