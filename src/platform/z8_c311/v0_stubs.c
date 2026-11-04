/* Z8-C311 v0 headless stubs. menu_restore/backdrop_restore live in
 * native_display.c (excluded until the Z8 GUI profile is pinned); the
 * navigate trampoline arrives with the displaced-instruction sites.
 * All are referenced only from dormant draw/navigation paths in the
 * uninitialised v0 configuration. */
#include "fenestra_native_display.h"
#include <stdint.h>
#include "z8_debug_identity.h"
/* The generated build identity enters the core image; verify_build_identity
 * independently checks every linked copy. */
const unsigned char *fx_z8_debug_identity_anchor(void){return fx_z8_debug_build;}
FxResult fx_native_menu_restore(const FxNativePage *page){(void)page;return FX_CAPABILITY;}
FxResult fx_native_backdrop_restore(const FxNativePage *page){(void)page;return FX_CAPABILITY;}
uint64_t fx_z8_navigate_original(uint32_t page){(void)page;return 0;}

/* v0 headless capability stubs: display, scene, Linux bridge, text-bank
 * growth and PTP are unavailable. Callers (panic pages, GUI refresh, the
 * Linux bridge) either never reach them in the headless configuration or
 * degrade safely. */
#include "fenestra_native_display.h"
#include "fenestra_native_font.h"
#include "fenestra_native_linux.h"
#include "fenestra_native_scene.h"
#include "ptp.h"
const FxNativeDisplayProfile fx_native_display_profile={0};
FxNativeView fx_native_current_view(void){FxNativeView v={0};return v;}
uint32_t fx_native_display_is_current(const FxNativeDisplay *d){(void)d;return 0u;}
FxResult fx_native_display_attach_global(FxNativeDisplay *d,const FxDemoScene *s){(void)d;(void)s;return FX_CAPABILITY;}
FxResult fx_native_display_attach(FxNativeDisplay *d,void *p,const FxDemoScene *s){(void)d;(void)p;(void)s;return FX_CAPABILITY;}
FxResult fx_native_display_present(FxNativeDisplay *d,const FxDemoScene *s){(void)d;(void)s;return FX_CAPABILITY;}
void fx_native_display_detach(FxNativeDisplay *d){(void)d;}
void fx_native_scene_poll(FxPlatformOwner *o){(void)o;}
int fx_native_scene_needs_poll(FxPlatformOwner *o){(void)o;return 0;}
void fx_linux_account(FxLinuxBridge *b,FxAllocationObserver o){(void)b;(void)o;}
void fx_linux_account_packets(FxLinuxBridge *b,FxIndexMemory m,FxAllocationObserver r,FxAllocationObserver d){(void)b;(void)m;(void)r;(void)d;}
int fx_linux_quiesced(void){return 1;}
void fx_linux_revoke(void){}
FxResult fx_native_text_bank_grow(FxIndexMemory m,FxNativeTextBank *b,uint32_t r,FxNativeTextBank **o){(void)m;(void)b;(void)r;(void)o;return FX_CAPABILITY;}
void fx_z8_ptp_poll(void){}
