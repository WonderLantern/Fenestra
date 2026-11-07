/* Z8 routed id allocation table (T5; two module row slots and the paged
 * apps list added in T12). The audited free page-id window [0x61c,0x65d]
 * (nearest stock page-id immediates 0x61b/0x65e at open/navigation call
 * sites; four-source T4b audit re-derived in the T5 Step 0 mirror check:
 * template census 0x44d28110 ends at 0x4c2, no row-table next target
 * inside the window, no call-site immediate inside the window) holds 66
 * ids: 4 own pages (0x63c-0x63f), one 18-id manager window, the apps
 * root plus FX_Z8_APPS_PAGES-1 list pages (0x636..0x63b) and one 18-id
 * module-UI window, leaving >=5 spare ids at both ends. Borrowed row ids
 * come from the 99-row stock array at 0x43cf133c (ids 0x45cf..0x4631;
 * family byte-verified in the mirror): the manager keeps the T2 offsets
 * (controls/pickers base..+23, back/notice +40/+41) at base 0x4603,
 * apps pages take 0x461d..0x4626 + 0x462d..0x4631 and the two module-UI
 * slots take 0x45cf..0x45e6 + back/notice 0x45e7/0x45e8 and
 * 0x45e9..0x4600 + back/notice 0x4601/0x4602 (module slots pack the
 * back/notice rows immediately after the control rows, so two 24+2
 * families fit below the manager; the shared UI row code takes the
 * module navigation offset from this table) -- all disjoint and clear
 * of the language/region family 0x46e8..0x46f5 and the entry rows
 * 0x2d1c/0x2d1d. Co-open constraint: the manager chain ([663, core,
 * manager...]) and the apps chain ([663, wonder, apps, module UI...])
 * are navigation-disjoint, and the apps page is co-open only with its
 * module UI (disjoint bases); beyond two module UIs the table exhausts
 * and fails FX_CAPABILITY explicitly (the row pool cannot host a third
 * base). */
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
    .apps_list_base = 0x636u,    /* list pages, ordinal 1 (root = wonder) */
    .module_page_first = 0x640u, /* dynamic module-UI window */
    .module_page_limit = 0x652u, /* one FX_NATIVE_UI_PAGES window */
    .manager_row_base = 0x4603u,
    .manager_nav_row_offset = 40u, /* T2 manager layout: back/notice +40/+41 */
    .apps_row_base = 0x461du,
    .module_nav_row_offset = 24u,  /* packed slots: back/notice +24/+25 */
    .module_row_count = 2u,
    .module_row_base = {0x45cfu, 0x45e9u},
};

typedef struct { uint32_t first, count; } Span;
static int span_inside(const Span *s, uint32_t first, uint32_t last) {
    return s->count && s->first >= first && s->first + s->count - 1u <= last;
}
static int spans_disjoint(const Span *a, const Span *b) {
    return a->first + a->count <= b->first || b->first + b->count <= a->first;
}

FxResult fx_z8_route_validate(const FxZ8RouteTable *r) {
    if (!r || !r->module_row_count || r->module_row_count > 2u)
        return FX_ARGUMENT;
    uint32_t usable_first = FX_Z8_ROUTE_WINDOW_FIRST + FX_Z8_ROUTE_MARGIN,
             usable_last = FX_Z8_ROUTE_WINDOW_LAST - FX_Z8_ROUTE_MARGIN;
    /* The apps root is the wonder page (span 1); list pages run from
     * apps_list_base for FX_Z8_APPS_PAGES-1 ids. */
    Span pages[6] = {
        {r->fenestra_page, 1u}, {r->wonder_page, 1u}, {r->auto_page, 1u},
        {r->guard_page, 1u},    {r->apps_list_base, FX_Z8_APPS_PAGES - 1u},
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
     * 0..23 plus the family's back/notice pair, apps pages 0..9 and
     * 0x10..0x14; the manager keeps the T2 +40 layout while the packed
     * module slots carry back/notice right after the control rows). */
    Span rows[8] = {
        {r->manager_row_base, FX_UI_CONTROLS + FX_UI_OPTIONS},
        {r->manager_row_base + r->manager_nav_row_offset, 2u},
        {r->apps_row_base, FX_Z8_ROUTE_APP_ROWS},
        {r->apps_row_base + 0x10u, 5u},
        {r->module_row_base[0], FX_UI_CONTROLS + FX_UI_OPTIONS},
        {r->module_row_base[0] + r->module_nav_row_offset, 2u},
    };
    for (uint32_t slot = 1u; slot < r->module_row_count && slot < 2u; ++slot) {
        rows[4 + 2u * slot] = (Span){r->module_row_base[slot], FX_UI_CONTROLS + FX_UI_OPTIONS};
        rows[5 + 2u * slot] = (Span){r->module_row_base[slot] + r->module_nav_row_offset, 2u};
    }
    for (uint32_t i = 0u; i < 4u + 2u * r->module_row_count; ++i) {
        if (!span_inside(&rows[i], FX_Z8_ROUTE_ROWS_FIRST, FX_Z8_ROUTE_ROWS_LAST))
            return FX_CAPABILITY;
        for (uint32_t j = i + 1u; j < 4u + 2u * r->module_row_count; ++j)
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
