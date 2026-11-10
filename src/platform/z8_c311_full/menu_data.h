/* Z8 port derived from upstream src/platform/z9_c532/menu_data.h.
 * Native values are supplied only by independently recovered Z8 bindings.
 * Upstream LICENSE and EXCEPTIONS apply. */
#include "z8_full_bindings.h"
#ifndef FX_Z8_C311_MENU_DATA_H
#define FX_Z8_C311_MENU_DATA_H
#include "fenestra.h"
#define FX_Z8_MENU_ROWS 16u
#define FX_Z8_MENU_EVENTS 18u
/* Original ordinary menu navigation, including MENU/application return.
 * The old 42ea8928 modal table disables those contacts by condition. */
#define FX_Z8_MENU_NAVIGATION FX_Z8_ADDR_43014bb8
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
    FxZ8MenuPage page;FxZ8MenuDescriptor descriptor;uint8_t header[272];
    FxZ8MenuRow rows[FX_Z8_MENU_ROWS];uintptr_t events[FX_Z8_MENU_ROWS],callbacks[5];
    FxZ8MenuEvent handlers[FX_Z8_MENU_ROWS][FX_Z8_MENU_EVENTS];
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
