#if !defined(FX_ZF_EXPERIMENTAL_MENU_SETUP) || FX_ZF_EXPERIMENTAL_MENU_SETUP != 1
#error "Zf menu setup is experimental and is not enabled by a production builder"
#endif

#include "menu_setup.h"
#include "fenestra_memory.h"
#include "fenestra_native_namespace.h"

static int overlaps(uintptr_t a, size_t bytes, uintptr_t b, size_t extent)
{
    if (bytes > UINTPTR_MAX - a || extent > UINTPTR_MAX - b) return 1;
    return a < b + extent && b < a + bytes;
}

FxResult fx_zf_setup_prepare(FxZfSetupCopy *out, const FxNativeDescriptor *stock,
                             const FxNativeEvent *navigation, uintptr_t action)
{
    if (!out || !stock || !navigation || !action) return FX_ARGUMENT;
    if ((uintptr_t)out % _Alignof(FxZfSetupCopy) || (uintptr_t)stock % _Alignof(FxNativeDescriptor) ||
        (uintptr_t)navigation % _Alignof(FxNativeEvent) || (action & 3u)) return FX_ARGUMENT;
    if (out->descriptor.rows || out->descriptor.events) return FX_BUSY;
    if (stock->count != FX_ZF_SETUP_STOCK_ROWS || !stock->header || !stock->rows || !stock->events)
        return FX_INTEGRITY;
    if (stock->rows % _Alignof(FxNativeRow) || stock->events % _Alignof(uintptr_t)) return FX_ARGUMENT;
    uintptr_t self = (uintptr_t)out;
    if (overlaps((uintptr_t)stock, sizeof(*stock), self, sizeof(*out)) ||
        overlaps(stock->header, FX_ZF_SETUP_HEADER_BYTES, self, sizeof(*out)) ||
        overlaps(stock->rows, FX_ZF_SETUP_STOCK_ROWS * sizeof(FxNativeRow), self, sizeof(*out)) ||
        overlaps(stock->events, FX_ZF_SETUP_STOCK_ROWS * sizeof(uintptr_t), self, sizeof(*out)) ||
        overlaps((uintptr_t)navigation, FX_NATIVE_EVENTS * sizeof(FxNativeEvent), self, sizeof(*out)) ||
        overlaps(action, 1u, self, sizeof(*out)))
        return FX_ARGUMENT;
    const FxNativeRow *rows = (const FxNativeRow *)stock->rows;
    if (rows[0].word[0] != 618u || rows[44].word[0] != 866u) return FX_INTEGRITY;
    const uintptr_t *stock_events = (const uintptr_t *)stock->events;
    for (uint32_t i = 0; i < FX_ZF_SETUP_STOCK_ROWS; ++i)
        if (stock_events[i] && overlaps(stock_events[i], sizeof(FxNativeEvent), self, sizeof(*out)))
            return FX_ARGUMENT;

    static const uint32_t select[4] = {0x2afu, 0x2b4u, 0x2c8u, 0x2d7u};
    uint32_t seen = 0u, back = 0u, terminal = 0u;
    for (uint32_t i = 0; i < FX_NATIVE_EVENTS; ++i) {
        uint32_t event = (uint32_t)navigation[i].event & 65535u;
        if ((navigation[i].callback && overlaps(navigation[i].callback, 1u, self, sizeof(*out))) ||
            (navigation[i].condition && overlaps(navigation[i].condition, 1u, self, sizeof(*out))))
            return FX_ARGUMENT;
        if (event == 0x3dcu) { terminal = 1u; break; }
        if (event == 0x2aeu) ++back;
        for (uint32_t k = 0; k < 4u; ++k)
            if (event == select[k]) {
                uint32_t bit = 1u << k;
                if (seen & bit) return FX_INTEGRITY;
                seen |= bit;
            }
    }
    if (!terminal || back != 1u || seen != 15u) return FX_INTEGRITY;

    FxNativeEvent entry[FX_ZF_SETUP_EXTRA_ROWS][FX_NATIVE_EVENTS];
    memset(entry, 0, sizeof(entry));
    FxResult result = fx_native_menu_link(entry[0], navigation, 0x3dcu, select,
                                          action, FX_NATIVE_FENESTRA_PAGE);
    if (result != FX_OK) return result;
    result = fx_native_menu_link(entry[1], navigation, 0x3dcu, select,
                                 action, FX_NATIVE_WONDER_PAGE);
    if (result != FX_OK) return result;

    memcpy(out->rows + FX_ZF_SETUP_EXTRA_ROWS, rows, FX_ZF_SETUP_STOCK_ROWS * sizeof(*rows));
    memcpy(out->events + FX_ZF_SETUP_EXTRA_ROWS, (const void *)stock->events,
           FX_ZF_SETUP_STOCK_ROWS * sizeof(uintptr_t));
    memcpy(out->entry_events, entry, sizeof(entry));
    for (uint32_t i = 0; i < FX_ZF_SETUP_EXTRA_ROWS; ++i) {
        out->rows[i] = rows[44];
        out->rows[i].word[0] = FX_ZF_STOCK_TEXT_COUNT + i;
        out->rows[i].word[2] = 65535u;
        out->rows[i].word[6] = 0x7ff0u + i;
        out->events[i] = (uintptr_t)out->entry_events[i];
    }
    out->descriptor = *stock;
    out->descriptor.rows = (uintptr_t)out->rows;
    out->descriptor.events = (uintptr_t)out->events;
    out->descriptor.count = FX_ZF_SETUP_TOTAL_ROWS;
    return FX_OK;
}
