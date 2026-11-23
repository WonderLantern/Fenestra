#ifndef FENESTRA_CARD_PAYLOAD_H
#define FENESTRA_CARD_PAYLOAD_H

#include "fenestra_text.h"

#define FX_CARD_PAYLOAD_ABI 1u
#define FX_CARD_PAYLOAD_MAGIC UINT32_C(0x31424e46) /* FNB1, little endian */
#define FX_CARD_PAYLOAD_PATH "A:\\FENESTRA\\BOOT.BIN"
#define FX_CARD_PAYLOAD_HEADER_BYTES 16u
#define FX_CARD_PAYLOAD_ADDRESS 0x401a0000
#define FX_CARD_PAYLOAD_CAPACITY 0x5d000

typedef enum {
    FX_CARD_MOUNT_PENDING, FX_CARD_MOUNT_READY, FX_CARD_MOUNT_FAILED
} FxCardMountState;

typedef struct {
    void *context;
    FxCardMountState (*mount_state)(void *);
    uint64_t (*clock_ms)(void *);
    FxResult (*wait_ms)(void *,uint32_t);
    void *(*allocate)(void *,uint32_t);
    void (*release)(void *,void *);
    uintptr_t reserved;
    FxTextFiles files;
    uint32_t missing_file_error;
} FxCardBootIO;

/* Borrowed for the duration of entry. The I/O table provides read access.
 * Any additional copy must fit in the arena and start on a page boundary. */
typedef struct {
    uint32_t abi,bytes;
    const FxCardBootIO *io;
    const char *target;
    const uint8_t *application;
    uint32_t image_bytes,capacity;
    FxResult (*copy)(uint32_t offset,const void *data,uint32_t bytes);
} FxCardPayloadBoot;

typedef void (*FxCardPayloadEntry)(const FxCardPayloadBoot *);

typedef struct {
    uint32_t magic,header_bytes,image_bytes,entry_offset;
} FxCardPayloadHeader;

_Static_assert(sizeof(FxCardPayloadHeader)==FX_CARD_PAYLOAD_HEADER_BYTES,"Card payload header ABI");
FxResult fx_card_payload_parse(const void *,uint32_t file_bytes,uint32_t capacity,FxCardPayloadHeader *);

/* The callback borrows the complete, closed file only during its invocation. */
FxResult fx_card_boot_read(const FxCardBootIO *,const char *path,uint32_t minimum,uint32_t maximum,
    uint32_t mount_timeout_ms,uint32_t load_timeout_ms,
    FxResult (*accept)(void *,const void *,uint32_t),void *context);

#endif
