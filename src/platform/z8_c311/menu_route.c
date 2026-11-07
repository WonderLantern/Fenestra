/* Z8 routed id allocation table (T5). The audited free page-id window
 * [0x61c,0x65d] (nearest stock page-id immediates 0x61b/0x65e at
 * open/navigation call sites; four-source T4b audit re-derived in the T5
 * Step 0 mirror check: template census 0x44d28110 ends at 0x4c2, no
 * row-table next target inside the window, no call-site immediate inside
 * the window) holds 66 ids: 4 own pages (0x63c-0x63f), one 18-id manager
 * window, one apps list id and one 18-id module-UI window, leaving >=5
 * spare ids at both ends. Borrowed row ids come from the 99-row stock
 * array at 0x43cf133c (ids 0x45cf..0x4631; family byte-verified in the
 * mirror): the manager keeps the T2 base 0x4600 (controls/pickers
 * 0x4600..0x4617, back/notice 0x4628/0x4629), apps pages take
 * 0x461a..0x4623 + 0x462a..0x462e and the single module-UI slot takes
 * 0x45cf..0x45e6 + 0x45f7/0x45f8 -- all disjoint and clear of the
 * language/region family 0x46e8..0x46f5 and the entry rows 0x2d1c/0x2d1d.
 * Co-open constraint: the manager chain ([663, core, manager...]) and the
 * apps chain ([663, wonder, apps, module UI...]) are navigation-disjoint,
 * and the apps page is co-open only with its module UI (disjoint bases);
 * a second module UI exhausts the table and fails FX_CAPABILITY
 * explicitly (the row pool cannot host a second base). */
#include "menu_data.h"
#include "menu_route.h"

/* Audited window bounds and margins (T4b audit, re-derived T5 Step 0). */
#define FX_Z8_ROUTE_WINDOW_FIRST 0x61cu
#define FX_Z8_ROUTE_WINDOW_LAST 0x65du
#define FX_Z8_ROUTE_MARGIN 5u
#define FX_Z8_ROUTE_GATE 0x68cu
/* Borrowed row families (mirror-pinned): the 99-row manager-family array
 * and the 14-row language/region array, plus the 663 entry-row borrows. */
#define FX_Z8_ROUTE_ROWS_FIRST 0x45cfu
#define FX_Z8_ROUTE_ROWS_LAST 0x4631u
/* Apps rows per list page: offsets 0..9 (app rows) and 0x10..0x14
 * (previous/next/back/unavailable/retry) inside the shared family. */
#define FX_Z8_ROUTE_APP_ROWS 10u
#define FX_Z8_ROUTE_ROWS_LANGUAGE_FIRST 0x46e8u
#define FX_Z8_ROUTE_ROWS_LANGUAGE_LAST 0x46f5u

const FxZ8RouteTable fx_z8_route = {
    .fenestra_page = FX_Z8_PAGE_FENESTRA,
    .wonder_page = FX_Z8_PAGE_WONDER,
    .auto_page = FX_Z8_PAGE_AUTO,
    .guard_page = FX_Z8_PAGE_GUARD,
    .manager_base = 0x624u,      /* window 0x624..0x635 (18 ids) */
    .apps_list_base = 0x636u,    /* apps list, ordinal 1 (root = wonder) */
    .module_page_first = 0x640u, /* dynamic module-UI window */
    .module_page_limit = 0x652u, /* one FX_NATIVE_UI_PAGES window */
    .manager_row_base = 0x4600u,
    .apps_row_base = 0x461au,
    .module_row_count = 1u,
    .module_row_base = {0x45cfu},
};

typedef struct { uint32_t first, count; } Span;
static int span_inside(const Span *s, uint32_t first, uint32_t last) {
    return s->count && s->first >= first && s->first + s->count - 1u <= last;
}
static int spans_disjoint(const Span *a, const Span *b) {
    return a->first + a->count <= b->first || b->first + b->count <= a->first;
}

FxResult fx_z8_route_validate(const FxZ8RouteTable *r) {
    if (!r || !r->module_row_count || r->module_row_count > 1u)
        return FX_ARGUMENT;
    uint32_t usable_first = FX_Z8_ROUTE_WINDOW_FIRST + FX_Z8_ROUTE_MARGIN,
             usable_last = FX_Z8_ROUTE_WINDOW_LAST - FX_Z8_ROUTE_MARGIN;
    Span pages[6] = {
        {r->fenestra_page, 1u}, {r->wonder_page, 1u}, {r->auto_page, 1u},
        {r->guard_page, 1u},    {r->apps_list_base, 1u},
        {r->manager_base, FX_NATIVE_UI_PAGES},
    };
    Span windows = {r->module_page_first, r->module_page_limit - r->module_page_first};
    for (uint32_t i = 0u; i < 6u; ++i) {
        if (!span_inside(&pages[i], usable_first, usable_last) ||
            pages[i].first + pages[i].count - 1u >= FX_Z8_ROUTE_GATE)
            return FX_CAPABILITY;
        for (uint32_t j = i + 1u; j < 6u; ++j)
            if (!spans_disjoint(&pages[i], &pages[j]))
                return FX_CAPABILITY;
        if (!spans_disjoint(&pages[i], &windows))
            return FX_CAPABILITY;
    }
    /* A module UI needs at least its primary page plus one window slot. */
    if (windows.count < 2u || !span_inside(&windows, usable_first, usable_last) ||
        windows.first + windows.count - 1u >= FX_Z8_ROUTE_GATE)
        return FX_CAPABILITY;
    /* Row spans per family (offsets are not contiguous: UI pages use
     * 0..23 and 40/41, apps pages 0..9 and 0x10..0x14). */
    Span rows[6] = {
        {r->manager_row_base, FX_UI_CONTROLS + FX_UI_OPTIONS},
        {r->manager_row_base + 40u, 2u},
        {r->apps_row_base, FX_Z8_ROUTE_APP_ROWS},
        {r->apps_row_base + 0x10u, 5u},
        {r->module_row_base[0], FX_UI_CONTROLS + FX_UI_OPTIONS},
        {r->module_row_base[0] + 40u, 2u},
    };
    for (uint32_t i = 0u; i < 6u; ++i) {
        if (!span_inside(&rows[i], FX_Z8_ROUTE_ROWS_FIRST, FX_Z8_ROUTE_ROWS_LAST))
            return FX_CAPABILITY;
        for (uint32_t j = i + 1u; j < 6u; ++j)
            if (!spans_disjoint(&rows[i], &rows[j]))
                return FX_CAPABILITY;
    }
    return FX_OK;
}

FxResult fx_z8_route_module_row_base(const FxZ8RouteTable *r, uint32_t load_index,
                                     uint32_t *row_base) {
    if (!r || !row_base)
        return FX_ARGUMENT;
    if (load_index >= r->module_row_count)
        return FX_CAPABILITY;
    *row_base = r->module_row_base[load_index];
    return FX_OK;
}
