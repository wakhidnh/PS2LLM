#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

void storage_set_base_path(const char *argv0);
int storage_load_model_blob(void **out_blob, size_t *out_size);
int storage_open_model_file(void);
int storage_try_init_hdd(void);
int storage_try_init_usb(void);
int storage_try_init_cdrom(void);

/* Added helpers required by config.c and model.c */
void storage_ensure_sif_ready(void);
int storage_try_read_whole_file(const char *path, void **out_buf, size_t *out_size);
int storage_read_block(int fd, void *buffer, size_t size);

#endif