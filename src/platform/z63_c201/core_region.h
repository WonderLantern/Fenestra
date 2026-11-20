#ifndef FX_Z63_CORE_REGION_H
#define FX_Z63_CORE_REGION_H
#include <stdint.h>
/* Resident core link range. It must stay outside every text window that
 * original startup copies to DRAM and remaps: package bytes after a window's
 * copy end do not survive boot. The builder rejects a profile that disagrees
 * with these values or with the original relocation routine. */
#define FX_Z63_CORE_BEGIN UINT64_C(0x401a0000)
#define FX_Z63_CORE_END UINT64_C(0x40200000)
#endif
