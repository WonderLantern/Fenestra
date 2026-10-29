#ifndef FENESTRA_NATIVE_NAMESPACE_H
#define FENESTRA_NATIVE_NAMESPACE_H
/* Nikon page IDs include menus, dialogs and shooting pages in one namespace.
 * Every supported immutable profile checks the entire native range at build.
 */
#define FX_NATIVE_FENESTRA_PAGE 0x7000u
#define FX_NATIVE_WONDER_PAGE 0x7001u
#define FX_NATIVE_MODULE_PAGE_BASE 0x7100u
#define FX_NATIVE_MANAGER_PAGE_BASE 0x704au
#define FX_NATIVE_STARTUP_AUTO_PAGE 0x705cu
#define FX_NATIVE_STARTUP_GUARD_PAGE 0x705du
#define FX_NATIVE_APPLICATION_PAGE_BASE 0x7080u
#define FX_NATIVE_UI_OWNERS 512u
#define FX_NATIVE_EXTENSION_PAGE_LIMIT 0x9500u
#endif
