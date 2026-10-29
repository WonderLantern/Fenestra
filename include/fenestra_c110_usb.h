#ifndef FENESTRA_C110_USB_H
#define FENESTRA_C110_USB_H
#include "fenestra_native_linux.h"
typedef FxLinuxBridge FxC110Usb;
FxC110Usb *fx_c110_menu_usb(void);
void fx_c110_usb_tick(uint64_t,uint32_t,int);
void fx_c110_usb_revoke(void);
uint32_t fx_c110_usb_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
int fx_c110_usb_quiesced(void);
uint32_t fx_c110_usb_ready(void);
#endif
