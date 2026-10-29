#ifndef FENESTRA_C110_DEMO_H
#define FENESTRA_C110_DEMO_H
#include "fenestra_settings.h"
#include "fenestra_text.h"
#include "fenestra_platform.h"
typedef struct FxC110Demo FxC110Demo;
enum { FX_C110_PLAY=1, FX_C110_STOP=2, FX_C110_DISABLE=3, FX_C110_WRITE_TEXT=4,
       FX_C110_TEST_FAULT=5, FX_C110_RETRY_CARD=6 };
FxResult fx_c110_demo_start(FxSettings *,FxC110Demo **,void *,void *,uint32_t);
FxResult fx_c110_demo_request(FxC110Demo *,uint32_t);
int fx_c110_demo_shutdown(FxC110Demo *);
uint32_t fx_c110_demo_phase(const FxC110Demo *);
void fx_c110_demo_page_closed(FxC110Demo *);
uint32_t fx_c110_demo_text_state(const FxC110Demo *);
/* Called only by the owning native worker between module invocations. */
uint32_t fx_c110_demo_debug(FxC110Demo *,uint16_t,const uint8_t *,uint32_t,
                            uint8_t *,uint32_t *);
uint32_t fx_c110_module_menu(FxC110Demo *,uint32_t,char [32],uint32_t *,int *);
FxResult fx_c110_module_action(FxC110Demo *,uint32_t);
FxPlatformOwner *fx_c110_module_platform(FxC110Demo *,uint32_t);
int fx_c110_modules_ready(FxC110Demo *);
uint32_t fx_c110_module_loaded(FxC110Demo *,const char *id);
uint32_t fx_c110_module_count(const FxC110Demo *);
uint32_t fx_c110_module_capacity(const FxC110Demo *);
const char *fx_c110_module_identity(const FxC110Demo *,uint32_t);
void fx_c110_demo_observer_fault(FxC110Demo *,FxResult);
#endif
