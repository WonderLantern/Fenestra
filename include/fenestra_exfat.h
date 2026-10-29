#ifndef FENESTRA_EXFAT_H
#define FENESTRA_EXFAT_H
#include "fenestra.h"

#define FX_EXFAT_SECTOR_MAX 4096u
#define FX_EXFAT_IO_LIMIT 32768u
#define FX_EXFAT_DIRECTORY_LIMIT 4096u

typedef struct {
    void *context;
    uint32_t sector_bytes;
    uint64_t sector_count;
    int (*read_sector)(void *, uint64_t lba, uint8_t *, uint32_t bytes);
} FxBlockIO;

typedef struct {
    FxBlockIO io;
    uint64_t start, volume_sectors;
    uint32_t fat_offset, fat_sectors, heap_offset, clusters, root;
    uint32_t sectors_per_cluster, cluster_bytes, calls, mounted;
    uint8_t scratch[FX_EXFAT_SECTOR_MAX];
} FxExfat;

typedef struct {
    FxExfat *volume;
    uint64_t length, valid_length, position;
    uint32_t first, current, current_index, hops, contiguous, directory, opened;
} FxExfatFile;

/* Own read-only exFAT implementation. ASCII module paths, no filesystem writes.
 * The I/O primitive is a single logical-sector transfer, not a file/VFS API.
 * Select the partition start externally; MBR/GPT discovery is not implemented. */
FxResult fx_exfat_mount(FxExfat *, const FxBlockIO *, uint64_t partition_start);
/* Detect an exFAT volume in a raw image, MBR or CRC-checked primary GPT. */
FxResult fx_exfat_discover(FxExfat *, const FxBlockIO *);
FxResult fx_exfat_open(FxExfat *, const char *relative_path, FxExfatFile *);
FxResult fx_exfat_read(FxExfatFile *, uint8_t *, uint32_t requested, uint32_t *actual);
void fx_exfat_close(FxExfatFile *);
void fx_exfat_card_io(FxExfatFile *handle, FxCardIO *out);
#endif
