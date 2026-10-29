#ifndef FENESTRA_NATIVE_MENU_H
#define FENESTRA_NATIVE_MENU_H
#include "fenestra_platform.h"
#define FX_NATIVE_ROWS 18u
#define FX_NATIVE_EVENTS 18u
typedef struct {uint32_t word[11];} FxNativeRow;
typedef struct {uint64_t event,callback,condition,next;} FxNativeEvent;
typedef struct {
    uint32_t flags,reserved;uintptr_t header,rows,events;
    uint16_t count,geometry[15];
} FxNativeDescriptor;
typedef struct {
    uint32_t identity,reserved;uintptr_t previous,next,surface,content,callbacks;
    uint16_t count,selected,scroll,other;uint64_t unknown[3];uintptr_t descriptor,tail;
} FxNativePage;
/* Type-8 confirmation/reader buttons are children of the surface, while
 * ordinary list rows belong to content. This is Nikon's descriptor layout. */
static inline int fx_native_page_modal(const FxNativePage *p) {
    return p && p->descriptor && (((const FxNativeDescriptor *)p->descriptor)->flags&255u)==8u;
}
static inline void *fx_native_page_rows(const FxNativePage *p) {
    return (void *)(fx_native_page_modal(p)?p->surface:p->content);
}
/* Original C110/C532 use the low halfword at +0x58 as the first physical
 * descriptor row, separately from the selected offset at +0x32. */
static inline uint32_t fx_native_page_row_index(const FxNativePage *p) {
    return p->selected+(uint16_t)p->tail;
}
typedef struct {uint32_t image,width,height,error;uintptr_t surface;} FxNativeBackdrop;
typedef struct FxNativeMenu {
    FxNativePage page;FxNativeDescriptor descriptor;uint8_t header[340];
    FxNativeRow rows[FX_NATIVE_ROWS];uintptr_t events[FX_NATIVE_ROWS],callbacks[5];
    FxNativeEvent handlers[FX_NATIVE_ROWS][FX_NATIVE_EVENTS];
    FxNativeBackdrop backdrop;
    struct FxNativeRowProgram *programs[FX_NATIVE_ROWS];
} FxNativeMenu;
/* Core-owned immutable replacement for a native row's event records. Older
 * records remain allocated, but cease to be accepted after a row is rebound. */
typedef struct FxNativeRowProgram {
    FxNativeEvent events[FX_NATIVE_EVENTS];
    FxNativeMenu *menu;uint32_t row,control;
} FxNativeRowProgram;
typedef struct {
    const FxNativePage *page;const FxNativeRow *row;const FxNativeEvent *navigation;
    uint32_t terminal,back_event,select_events[4];uint64_t stay,back;
} FxNativeMenuProfile;
/* Link a row into an existing stock page. Preserve its original Back, Menu,
 * scrolling and conditional routes; only the four enter actions are replaced.
 * Never use a child page's already-rewritten handlers as the source. */
FxResult fx_native_menu_link(FxNativeEvent out[FX_NATIVE_EVENTS],const FxNativeEvent *source,
                             uint32_t terminal,const uint32_t select_events[4],
                             uintptr_t action,uint64_t next);
typedef struct {
    uint32_t text,row,submenu,value;uintptr_t action;uint64_t next;
} FxNativeEntry;
/* Original layout only. Business callbacks/events are never inherited from
 * a stock confirmation such as Save changes or Format card. */
typedef struct {
    const FxNativeDescriptor *descriptor;const FxNativeRow *rows;
    uint32_t header_bytes,row_count;
} FxNativeDialogStyle;
/* Initialize an unpublished copied page. previous is the native surface
 * ancestry, NOT the logical Back destination. A parentless list is treated
 * as a root by Nikon's touch router, which then dereferences its next page.
 * Both core and module pages must use this same initialization path. */
void fx_native_page_initialize(FxNativePage *,const FxNativePage *,uint32_t identity,
                               uintptr_t descriptor,uintptr_t callbacks[5],int child);
/* Common metadata constructor; profiles supply independently verified original
 * records and event IDs. Does not publish roots, allocate, call GUI or run IO. */
FxResult fx_native_menu_build(FxNativeMenu *,const FxNativeMenuProfile *,uint32_t page,uint32_t title,
                              const FxNativeEntry *,uint32_t count,const uintptr_t callbacks[5]);
FxResult fx_native_menu_build_style(FxNativeMenu *,const FxNativeMenuProfile *,uint32_t page,uint32_t title,
                              const FxNativeEntry *,uint32_t count,const uintptr_t callbacks[5],
                              const FxNativeDialogStyle *);
/* Expand only our unpublished copied modal records. Native coordinates are
 * supplied by the profile; stock descriptors and business handlers stay intact. */
FxResult fx_native_menu_reader_layout(FxNativeMenu *,uint16_t width,uint16_t height);
/* Original leaf surface/content pair, under the caller's GUI exclusion.
 * The content clip comes from Nikon's current target after surface dispatch. */
typedef struct {uintptr_t dirty,dispatch,device,rectangle,empty,lookup;uint32_t modal_id;uintptr_t restore;} FxNativeMenuDrawProfile;
FxResult fx_native_menu_draw(const FxNativeMenuDrawProfile *,const FxNativePage *);
#endif
