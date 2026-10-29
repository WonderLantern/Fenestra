#ifndef FENESTRA_C110_RTOS_H
#define FENESTRA_C110_RTOS_H
#include "fenestra_native_rtos.h"
typedef FxRtos FxC110Rtos;
FxC110Rtos *fx_c110_menu_rtos(void);
void fx_c110_rtos_tick(uint64_t,uint32_t,int);
void fx_c110_rtos_revoke(void);
int fx_c110_rtos_quiesced(void);
int fx_c110_rtos_read_normal(uint64_t,uint32_t,uint8_t *,uint64_t *);
/* Legacy native helpers now share the same direct, unqualified byte reader.
 * native_span reports the requested address; it does not prove DMA identity. */
int fx_c110_rtos_native_span(uint64_t,uint32_t,uint64_t *,uint64_t *);
int fx_c110_rtos_read_native(uint64_t,uint32_t,uint8_t *,uint64_t *);
int fx_c110_rtos_read_video(uint64_t,uint32_t,uint8_t *,uint64_t *);
uint32_t fx_c110_rtos_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
/* Called only by the printf veneer. Native AAPCS64 va_list is copied by the
 * original bounded formatter; no module-controlled pointers are admitted. */
uint32_t fx_c110_rtos_vprintf(const char *,const void *,uintptr_t);
#endif
