#ifndef FX_NEF_METADATA_H
#define FX_NEF_METADATA_H
#include "fenestra.h"
/* Module utility for an exclusively owned, serialized little-endian Nikon
 * metadata header. Recognizes Nikon TIFF directories, never fixed offsets or
 * old rectangle bytes. Preflights every matching tag before modifying any.
 * Updates MakerNote0045 and NEFInfo v1/0100 crop+CRC together. The checksum
 * callback returns seeded CRC16 (low16), with a CPU-readable input address.
 * Does not touch image data, acquire ownership, translate or synchronize DMA. */
typedef uint32_t (*FxNefChecksum)(uint32_t bytes,uint64_t address,uint32_t seed);
FxResult fx_nef_crop_write(void *header,uint32_t bytes,const uint16_t crop[4],FxNefChecksum);
#endif
