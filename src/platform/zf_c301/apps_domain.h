#ifndef FX_ZF_C301_APPS_DOMAIN_H
#define FX_ZF_C301_APPS_DOMAIN_H
#include "fenestra_native_apps.h"
typedef struct { _Atomic(FxNativeApps *) published; } FxZfAppsDomain;
/* Sole original GUI initializer, before page callbacks can reach apps. apps
 * and its initialized profile/callback sources must be retained to reboot.
 * This publishes a controller, not pages or a module activation result. */
FxResult fx_zf_apps_publish(FxZfAppsDomain *,FxNativeApps *);
FxNativeApps *fx_zf_apps_get(const FxZfAppsDomain *);
FxZfAppsDomain *fx_zf_owner_apps_domain(void);
#endif
