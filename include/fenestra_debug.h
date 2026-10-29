#ifndef FENESTRA_DEBUG_H
#define FENESTRA_DEBUG_H
#include "fenestra_loader.h"

#define FX_DEBUG_HEADER 32u
#define FX_DEBUG_PAYLOAD 256u
#define FX_DEBUG_FRAME (FX_DEBUG_HEADER + FX_DEBUG_PAYLOAD)
#define FX_DEBUG_LOG_COUNT 16u
#define FX_DEBUG_LOG_BYTES 64u
#define FX_DEBUG_FRAGMENT_TIMEOUT 1000u /* monotonic milliseconds */
enum { FX_DBG_HELLO = 1, FX_DBG_STATUS, FX_DBG_LOGS, FX_DBG_MODULE };

/* One serialized owner. No USB I/O, allocation, or loader activation here.
 * Frames are copied, bounded and little endian; bytes may arrive fragmented.
 * Nonces identify sessions, not user authentication. Local UI gates enable. */
typedef struct {
    const FxLoader *loader;
    uint32_t enabled, connected, greeted, last_id;
    uint64_t session, last_session, fragment_started, last_time;
    uint32_t rx_bytes, tx_bytes, tx_offset, previous_bytes, previous_reply_bytes;
    uint8_t rx[FX_DEBUG_FRAME], tx[FX_DEBUG_FRAME];
    uint8_t previous[FX_DEBUG_FRAME], previous_reply[FX_DEBUG_FRAME];
    uint8_t logs[FX_DEBUG_LOG_COUNT][FX_DEBUG_LOG_BYTES];
    uint32_t log_head, log_count, log_sequence, dropped_logs, bad_frames;
} FxDebug;

void fx_debug_init(FxDebug *, const FxLoader *);
void fx_debug_enable(FxDebug *, int enabled);
FxResult fx_debug_connect(FxDebug *, uint64_t unique_session, uint64_t now_ms);
void fx_debug_disconnect(FxDebug *);
FxResult fx_debug_feed(FxDebug *, const uint8_t *, uint32_t bytes,
                       uint64_t now_ms, uint32_t *consumed);
FxResult fx_debug_read(FxDebug *, uint8_t *, uint32_t capacity, uint32_t *bytes);
FxResult fx_debug_tick(FxDebug *, uint64_t now_ms);
void fx_debug_log(FxDebug *, uint32_t level, uint64_t now_ms, const char *);
uint32_t fx_debug_crc(const uint8_t *, uint32_t bytes);
#endif
