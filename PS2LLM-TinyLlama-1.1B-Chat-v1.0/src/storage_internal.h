#ifndef STORAGE_INTERNAL_H
#define STORAGE_INTERNAL_H

#include <stddef.h>

/* Shared between storage.c and config.c so there's exactly one copy
 * of the IRX-loading logic (the genuinely hardware-uncertain part -
 * see storage.c's top comment) rather than two copies that could
 * silently drift out of sync. */

void storage_ensure_sif_ready(void);
int storage_try_init_hdd(void);
int storage_try_init_usb(void);
int storage_try_init_cdrom(void);
int storage_try_read_whole_file(const char *path, void **out_blob, size_t *out_size);

#endif
