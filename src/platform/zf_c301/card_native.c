#if !defined(FX_ZF_EXPERIMENTAL_CARD) || FX_ZF_EXPERIMENTAL_CARD != 1
#error "Zf card adapter requires checked experimental helpers"
#endif
#include "card.h"
#include "fx_zf_card_profile.h"
static const FxZfCardNative native={
    (int32_t (*)(const char *,uint32_t,uint32_t))(uintptr_t)FX_ZF_CARD_OPEN,
    (uint32_t (*)(int32_t,void *,uint32_t,uint32_t *))(uintptr_t)FX_ZF_CARD_READ,
    (uint32_t (*)(int32_t))(uintptr_t)FX_ZF_CARD_CLOSE};
FxResult fx_zf_card_native_files(FxTextFiles *files,FxZfCardScope *s) {
    return fx_zf_card_files(files,s,&native);
}
const FxZfCardNative *fx_zf_card_native_ops(void) { return &native; }
