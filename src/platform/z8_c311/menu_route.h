#ifndef FX_Z8_C311_MENU_ROUTE_H
#define FX_Z8_C311_MENU_ROUTE_H
#include "fenestra.h"
#include "fenestra_native_ui.h"

/* T5 routed page/row id allocation table. The C311 kind-2 navigation gate
 * drops every list target >= 0x68c before page.get runs, so every page the
 * menu can navigate to must carry an id below the gate inside the audited
 * free window. The table is static for the fixed families (own pages,
 * manager UI window, apps list) and bounded-dynamic for module UIs (row
 * bases are assigned by load order; page ids come from the module window
 * through FxNativeUiSpace). Exhaustion fails explicitly with
 * FX_CAPABILITY -- a silently unusable id would be a fake success. */
typedef struct {
    /* Own pages (T4b; see menu_data.h for the audit). */
    uint32_t fenestra_page, wonder_page, auto_page, guard_page;
    /* Manager UI: one FX_NATIVE_UI_PAGES window. The apps root reuses the
     * wonder id (ordinal 0); apps_list_base is ordinal 1. */
    uint32_t manager_base;
    uint32_t apps_list_base;
    /* Module-UI dynamic window: FxNativeUiSpace bounds (half-open). */
    uint32_t module_page_first, module_page_limit;
    /* Borrowed stock row-id bases inside the 99-row array at 0x43cf133c
     * (ids 0x45cf..0x4631). Manager and module UIs use offsets 0..23
     * (control rows, pickers) and 40/41 (back, notice); apps pages use
     * offsets 0..9 (app rows) and 0x10..0x14 (previous/next/back/
     * unavailable/retry). Unregistered ids abort the open-path row
     * rebuild (T12 P3), so every offset must stay in the family. */
    uint32_t manager_row_base;
    uint32_t apps_row_base;
    uint32_t module_row_count;
    uint32_t module_row_base[1];
} FxZ8RouteTable;

extern const FxZ8RouteTable fx_z8_route;

/* Whole-table invariants: gate bound, audited window with >=5 spare ids
 * at both ends, exact manager window size, non-empty module window, and
 * pairwise disjointness of every routed page span and row span. */
FxResult fx_z8_route_validate(const FxZ8RouteTable *route);

/* Bounded dynamic slot: the load-ordered module-UI row base. Returns
 * FX_CAPABILITY when the table is exhausted, never a fallback id. */
FxResult fx_z8_route_module_row_base(const FxZ8RouteTable *route, uint32_t load_index,
                                     uint32_t *row_base);
#endif
