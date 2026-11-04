#ifndef FX_Z8_C311_MENU_DATA_H
#define FX_Z8_C311_MENU_DATA_H
#include "fenestra.h"
#define FX_Z8_MENU_ROWS 16u
/* Original ordinary menu navigation for the SETUP page family. Pinned
 * against the C311 image at 0x43bf0680: 12 records (not the Zf/Z9 single
 * 18-event table); callbacks 0x403b1b74..0x403b1bb0 + 0x4054eae4/0x4054ea5c,
 * conditions 0x403b1c5c/0x403b1c7c, next page id 0x68c. The old 0x43014bb8
 * value was a Z9 reference that reads as high-entropy data on C311. */
#define FX_Z8_MENU_NAVIGATION UINT64_C(0x43bf0680)
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
    FxZ8MenuEvent handlers[FX_Z8_MENU_ROWS][12];
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
