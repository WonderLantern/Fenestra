#ifndef FX_Z8_C311_MENU_DATA_H
#define FX_Z8_C311_MENU_DATA_H
#include "fenestra.h"
#include "fenestra_native_menu.h"
#define FX_Z8_MENU_ROWS 16u
/* Original ordinary menu navigation for the SETUP page family. Pinned
 * against the C311 image at 0x43bf0680: 13 records = 12 routes + the
 * {0x3bb,0,0,0x68c} terminator (not the Zf/Z9 single 18-event table);
 * route callbacks 0x403b1b74..0x403b1bb0 + 0x4054eae4/0x4054ea5c,
 * conditions 0x403b1c5c (always-false: stock-disabled) / 0x403b1c7c
 * (settings-gated), stay next page id 0x68c. The old 0x43014bb8 value
 * was a Z9 reference that reads as high-entropy data on C311. */
#define FX_Z8_MENU_NAVIGATION UINT64_C(0x43bf0680)
/* Routed own-page ids (T4b): the C311 kind-2 navigation gate
 * (FUN_403af53c: cmp w22,w20 @0x403af654 + b.ls @0x403af658) drops every
 * list target >= 0x68c BEFORE page.get runs, so navigable own pages live
 * in the audited free window [0x61c,0x65d] (nearest stock references:
 * page-id immediates 0x61b/0x65e at open/navigation call sites; audit =
 * descriptor-template census (ROM array 0x44d28110, stride 0x50, valid
 * templates end at id 0x4c2) + row-table next census (no target inside
 * the window) + call-site immediate scan + the live group-2 roots dump
 * (t4b-audit-r2: window slots are empty scaffolds, page_count 0)). The
 * T5 manager/apps/module windows extend the same window; the full routed
 * layout and its invariants live in menu_route.c (fx_z8_route). */
#define FX_Z8_PAGE_FENESTRA 0x63cu
#define FX_Z8_PAGE_WONDER 0x63du
#define FX_Z8_PAGE_AUTO 0x63eu
#define FX_Z8_PAGE_GUARD 0x63fu
/* Apps list pages (T12-③): ordinal 0 is the wonder root; ordinals 1..N
 * are list pages at apps_list_base.. The routed window's free segment
 * 0x636..0x63b hosts six list pages = 60 visible apps; a larger module
 * count still surfaces the shared apps budget notice. */
#define FX_Z8_APPS_PAGES 7u
typedef struct {uint32_t word[11];} FxZ8MenuRow;
typedef struct {uint64_t event,callback,condition,next;} FxZ8MenuEvent;
typedef struct {
    uint32_t flags,reserved;uintptr_t header,rows,events;
    uint16_t count,geometry[15];
} FxZ8MenuDescriptor;
typedef struct {
    uint32_t identity,reserved;uintptr_t previous,next,surface,content,callbacks;
    uint16_t count,selected,scroll,other;uint64_t unknown[3];uintptr_t descriptor,tail;
} FxZ8MenuPage;
typedef struct {
    uint32_t text_id,row_id,submenu,reserved;uintptr_t action;uint64_t next;
} FxZ8MenuEntry;
typedef struct {
    /* Layout-compatible prefix of the shared FxNativeMenu (asserted in
     * menu_construct.c) so fx_native_menu_build can construct in place.
     * The shared row/event slot counts are part of that layout; the C311
     * table only fills 13 of the 18 handler slots. */
    FxNativePage page;FxNativeDescriptor descriptor;uint64_t descriptor_tail[2];
    uint8_t header[340];
    FxNativeRow rows[FX_NATIVE_ROWS];uintptr_t events[FX_NATIVE_ROWS],callbacks[5];
    FxZ8MenuEvent handlers[FX_NATIVE_ROWS][FX_NATIVE_EVENTS];
} FxZ8OwnedMenu;
/* Constructs unpublished RAM metadata using the exact original C311 Setup
 * and navigation records. No allocation, root publication, label fabrication,
 * task start or activation here. Actions are trusted EL1 platform callbacks;
 * module code never supplies these pointers. Text/attributes must be bound
 * and runtime actions qualified before the menu owner publishes a page. */
FxResult fx_z8_menu_construct(FxZ8OwnedMenu *,const FxZ8MenuPage *setup,
                             uint32_t page_id,uint32_t title_id,
                             const FxZ8MenuEntry *,uint32_t count,
                             const uintptr_t callbacks[5]);
#endif
