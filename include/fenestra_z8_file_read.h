#ifndef FENESTRA_Z8_FILE_READ_H
#define FENESTRA_Z8_FILE_READ_H
#include <stdint.h>
/* Synchronous cached-RAM adapter. May return a short read (at most 32 KiB).
 * The caller owns the descriptor and applies its normal EOF/close policy. */
uint32_t fx_z8_file_read(int32_t fd,void *data,uint32_t bytes,uint32_t *done);
char fx_z8_card_drive(void);
#endif
