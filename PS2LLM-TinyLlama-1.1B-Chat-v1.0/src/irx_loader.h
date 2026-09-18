#ifndef IRX_LOADER_H
#define IRX_LOADER_H

int load_embedded_module(const char *name);
int load_embedded_module_with_args(const char *name, int argc, const char *args);

int irx_loader_load_hdd_modules(void);
int irx_loader_load_usb_modules(void);

#endif