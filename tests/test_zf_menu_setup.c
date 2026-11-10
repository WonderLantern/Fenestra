#include "menu_setup.h"
#include "fenestra_native_namespace.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static int is_select(uint32_t event)
{
    return event == 0x2afu || event == 0x2b4u || event == 0x2c8u || event == 0x2d7u;
}

int main(void)
{
    static const uint32_t ids[15] = {0x10,0x11,0x12,0x13,0x2ae,0x2af,0x2b4,0x14,
                                     0x15,0x2c8,0x16,0x2d7,0x17,0x18,0x3dc};
    FxNativeRow rows[47] = {0}, rows_before[47];
    uintptr_t events[47], events_before[47];
    FxNativeEvent nav[FX_NATIVE_EVENTS] = {0}, nav_before[FX_NATIVE_EVENTS];
    uint8_t header[FX_ZF_SETUP_HEADER_BYTES] = {0};
    FxNativeDescriptor stock = {.header=(uintptr_t)header,.rows=(uintptr_t)rows,
                                .events=(uintptr_t)events,.count=47};
    FxNativeDescriptor stock_before;
    FxZfSetupCopy out = {0}, empty = {0}, empty_before;
    memcpy(&stock_before, &stock, sizeof(stock));
    memcpy(&empty_before, &empty, sizeof(empty));
    rows[0].word[0] = 618;
    rows[44].word[0] = 866;
    rows[44].word[4] = 0x41;
    for (unsigned i = 0; i < 47; ++i) events[i] = (uintptr_t)(0x1000u + i);
    for (unsigned i = 0; i < 15; ++i)
        nav[i] = (FxNativeEvent){ids[i],0x2000u+i,0x3000u+i,0x4000u+i};
    memcpy(rows_before, rows, sizeof(rows));
    memcpy(events_before, events, sizeof(events));
    memcpy(nav_before, nav, sizeof(nav));

    assert(fx_zf_setup_prepare(&out,&stock,nav,0x1234u) == FX_OK);
    assert(out.descriptor.count == 49 && out.descriptor.rows == (uintptr_t)out.rows);
    assert(out.descriptor.events == (uintptr_t)out.events && out.descriptor.header == stock.header);
    assert(!memcmp(out.rows+2,rows,sizeof(rows)) && !memcmp(out.events+2,events,sizeof(events)));
    assert(out.rows[0].word[0] == 2898 && out.rows[1].word[0] == 2899);
    assert(out.rows[0].word[2] == 65535 && out.rows[0].word[6] == 0x7ff0);
    assert(out.rows[1].word[2] == 65535 && out.rows[1].word[6] == 0x7ff1);
    for (unsigned row = 0; row < 2; ++row) {
        assert(out.events[row] == (uintptr_t)out.entry_events[row]);
        for (unsigned i = 0; i < 15; ++i) {
            FxNativeEvent e = out.entry_events[row][i];
            assert(e.event == nav[i].event);
            if (is_select(ids[i])) {
                assert(e.callback == 0x1234 && !e.condition);
                assert(e.next == (row ? FX_NATIVE_WONDER_PAGE : FX_NATIVE_FENESTRA_PAGE));
            } else {
                assert(e.callback == nav[i].callback && e.condition == nav[i].condition && e.next == nav[i].next);
            }
        }
    }
    assert(!memcmp(&stock,&stock_before,sizeof(stock)) && !memcmp(rows,rows_before,sizeof(rows)));
    assert(!memcmp(events,events_before,sizeof(events)) && !memcmp(nav,nav_before,sizeof(nav)));
    assert(fx_zf_setup_prepare(&out,&stock,nav,0x1234u) == FX_BUSY);

    stock.count = 45;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_INTEGRITY);
    assert(!empty.descriptor.rows && !empty.descriptor.events);
    stock.count = 47;
    rows[44].word[0] = 906;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_INTEGRITY);
    rows[44].word[0] = 866;
    nav[5].event = 0x2b4;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_INTEGRITY);
    nav[5].event = 0x2af;
    nav[14].event = 0;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_INTEGRITY);
    nav[14].event = 0x3dc;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0) == FX_ARGUMENT);
    assert(fx_zf_setup_prepare((FxZfSetupCopy *)((uint8_t *)&empty+1),&stock,nav,0x1234u) == FX_ARGUMENT);
    assert(fx_zf_setup_prepare(&empty,(FxNativeDescriptor *)((uint8_t *)&stock+1),nav,0x1234u) == FX_ARGUMENT);
    assert(fx_zf_setup_prepare(&empty,&stock,(FxNativeEvent *)((uint8_t *)nav+1),0x1234u) == FX_ARGUMENT);
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1235u) == FX_ARGUMENT);
    stock.rows = (uintptr_t)rows + 1u;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_ARGUMENT);
    stock.rows = (uintptr_t)rows;
    stock.events = (uintptr_t)events + 1u;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_ARGUMENT);
    stock.events = (uintptr_t)events;
    stock.header = (uintptr_t)empty.rows;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_ARGUMENT);
    stock.header = (uintptr_t)header;
    events[0] = (uintptr_t)empty.rows;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_ARGUMENT);
    events[0] = events_before[0];
    nav[0].callback = (uintptr_t)empty.rows;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_ARGUMENT);
    nav[0].callback = nav_before[0].callback;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,(uintptr_t)empty.rows) == FX_ARGUMENT);
    stock.rows = (uintptr_t)empty.rows;
    assert(fx_zf_setup_prepare(&empty,&stock,nav,0x1234u) == FX_ARGUMENT);
    assert(!memcmp(&empty,&empty_before,sizeof(empty)));
    return 0;
}
