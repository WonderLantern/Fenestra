#ifndef FX_Z9_C532_MENU_DATA_H
#define FX_Z9_C532_MENU_DATA_H
#include "fenestra.h"
#define FX_Z9_MENU_ROWS 16u
#define FX_Z9_MENU_EVENTS 18u
/* Original ordinary menu navigation, including MENU/application return.
 * The old 42ea8928 modal table disables those contacts by condition. */
#define FX_Z9_MENU_NAVIGATION UINT64_C(0x43014bb8)
typedef struct {uint32_t word[11];} FxZ9MenuRow;
typedef struct {uint64_t event,callback,condition,next;} FxZ9MenuEvent;
typedef struct {
    uint32_t flags,reserved;uintptr_t header,rows,events;
    uint16_t count,geometry[15];
} FxZ9MenuDescriptor;
typedef struct {
    uint32_t identity,reserved;uintptr_t previous,next,surface,content,callbacks;
    uint16_t count,selected,scroll,other;uint64_t unknown[3];uintptr_t descriptor,tail;
} FxZ9MenuPage;
typedef struct {
    uint32_t text_id,row_id,submenu,reserved;uintptr_t action;uint64_t next;
} FxZ9MenuEntry;
typedef struct {
    FxZ9MenuPage page;FxZ9MenuDescriptor descriptor;uint8_t header[272];
    FxZ9MenuRow rows[FX_Z9_MENU_ROWS];uintptr_t events[FX_Z9_MENU_ROWS],callbacks[5];
    FxZ9MenuEvent handlers[FX_Z9_MENU_ROWS][FX_Z9_MENU_EVENTS];
} FxZ9OwnedMenu;
/* Constructs unpublished RAM metadata using the exact original C532 Setup
 * and navigation records. No allocation, root publication, label fabrication,
 * task start or activation here. Actions are trusted EL1 platform callbacks;
 * module code never supplies these pointers. Text/attributes must be bound
 * and runtime actions qualified before the menu owner publishes a page. */
FxResult fx_z9_menu_construct(FxZ9OwnedMenu *,const FxZ9MenuPage *setup,
                             uint32_t page_id,uint32_t title_id,
                             const FxZ9MenuEntry *,uint32_t count,
                             const uintptr_t callbacks[5]);
#endif
