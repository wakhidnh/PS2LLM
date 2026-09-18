#include <tamtypes.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>

#include "irx_loader.h"
#include "irx_data.h"

/* Lookup table mapping string names from storage.c and keyboard.c to embedded IRX data */
static const EmbeddedIrx embedded_irx_table[] = {
    { "usbd",     IRX_USBD_DATA,    sizeof(IRX_USBD_DATA) },
    { "ps2kbd",   IRX_PS2KBD_DATA,  sizeof(IRX_PS2KBD_DATA) },
    { "ps2dev9",  IRX_DEV9_DATA,    sizeof(IRX_DEV9_DATA) },
    { "ps2atad",  IRX_PS2ATAD_DATA, sizeof(IRX_PS2ATAD_DATA) },
    { "ps2hdd",   IRX_PS2HDD_DATA,  sizeof(IRX_PS2HDD_DATA) },
    { "ps2fs",    IRX_PS2FS_DATA,   sizeof(IRX_PS2FS_DATA) },
    { NULL, NULL, 0 }
};

static const EmbeddedIrx *find_irx(const char *name) {
    if (!name) return NULL;
    for (int i = 0; embedded_irx_table[i].name != NULL; i++) {
        if (strcmp(embedded_irx_table[i].name, name) == 0) {
            return &embedded_irx_table[i];
        }
    }
    return NULL;
}

int load_embedded_module_with_args(const char *name, int argc, const char *args) {
    const EmbeddedIrx *irx = find_irx(name);
    if (!irx || !irx->data || irx->size == 0) {
        return -1;
    }

    int ret = 0;
    int arg_len = (args && argc > 0) ? (strlen(args) + 1) : 0;

    int mod_res = SifExecModuleBuffer((void *)irx->data, irx->size, arg_len, args, &ret);
    if (mod_res < 0 || ret == 1) {
        return -1;
    }
    return 0;
}

int load_embedded_module(const char *name) {
    /* If ps2atad is requested without arguments, automatically apply UDMA Mode 4 */
    if (name && strcmp(name, "ps2atad") == 0) {
        const char atad_arg[] = "-m 4";
        if (load_embedded_module_with_args("ps2atad", 1, atad_arg) == 0) {
            return 0;
        }
    }
    return load_embedded_module_with_args(name, 0, NULL);
}

int irx_loader_load_hdd_modules(void) {
    /* 1. DEV9 hardware bus interface */
    if (load_embedded_module("ps2dev9") != 0) {
        return -1;
    }

    /* 2. ATAD Driver - Force Ultra-DMA Mode 4 ("-m 4") */
    const char atad_arg[] = "-m 4";
    if (load_embedded_module_with_args("ps2atad", 1, atad_arg) != 0) {
        if (load_embedded_module("ps2atad") != 0) {
            return -1;
        }
    }

    /* 3. HDD Management (APA Partition driver) */
    const char hdd_arg[] = "-o 4 -n 20";
    if (load_embedded_module_with_args("ps2hdd", 1, hdd_arg) != 0) {
        return -1;
    }

    /* 4. PFS FileSystem Driver */
    const char pfs_arg[] = "-m 4 -o 4 -n 40";
    if (load_embedded_module_with_args("ps2fs", 1, pfs_arg) != 0) {
        return -1;
    }

    return 0;
}

int irx_loader_load_usb_modules(void) {
    if (load_embedded_module("usbd") != 0) {
        return -1;
    }

    if (load_embedded_module("ps2kbd") != 0) {
        return -1;
    }

    return 0;
}