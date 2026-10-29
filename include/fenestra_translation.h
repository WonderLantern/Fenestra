#ifndef FENESTRA_TRANSLATION_H
#define FENESTRA_TRANSLATION_H
#include <stdint.h>
/* core.translation/v1, FUNCTION/U64_8: x0=virtual address, x1=access.
 * Returns raw PAR_EL1 from an AT probe in the current native translation
 * regime. Bit0 reports a translation fault, not a core service failure.
 * Unknown access returns UINT64_MAX. Remaining arguments are ignored.
 * This neither maps memory nor proves DMA/IOMMU visibility or buffer lifetime.
 * Incoming PAR_EL1 and DAIF are restored before returning. */
enum {FX_TRANSLATE_EL1_READ,FX_TRANSLATE_EL1_WRITE,
      FX_TRANSLATE_EL0_READ,FX_TRANSLATE_EL0_WRITE};
uint64_t fx_native_translation(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#endif
