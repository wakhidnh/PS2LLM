#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "config.h"
#include "storage.h"
#include "storage_internal.h"

static const char *DEFAULT_INI_CONTENT =
    "[storage]\n"
    "device=auto\n"
    "hdd_partition=__system\n"
    "model_path=MODEL.P2L\n";

static void set_defaults(PS2LLMConfig *cfg) {
    cfg->device = STORAGE_AUTO;
    strncpy(cfg->hdd_partition, "__system", CONFIG_PARTITION_MAX - 1);
    cfg->hdd_partition[CONFIG_PARTITION_MAX - 1] = '\0';
    strncpy(cfg->model_path, "MODEL.P2L", CONFIG_MODEL_PATH_MAX - 1);
    cfg->model_path[CONFIG_MODEL_PATH_MAX - 1] = '\0';
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                        s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
    return s;
}

static void parse_ini_text(const char *text, PS2LLMConfig *cfg) {
    char line[256];
    int li = 0;

    for (const char *p = text; ; p++) {
        if (*p == '\n' || *p == '\0') {
            line[li] = '\0';

            char *l = line;
            while (*l == ' ' || *l == '\t') l++;

            if (l[0] != ';' && l[0] != '#' && l[0] != '[' && l[0] != '\0') {
                char *eq = strchr(l, '=');
                if (eq) {
                    *eq = '\0';
                    char *key = trim(l);
                    char *val = trim(eq + 1);

                    if (strcmp(key, "device") == 0) {
                        if (strcmp(val, "hdd") == 0) cfg->device = STORAGE_HDD;
                        else if (strcmp(val, "usb") == 0) cfg->device = STORAGE_USB;
                        else if (strcmp(val, "cdrom") == 0) cfg->device = STORAGE_CDROM;
                    } else if (strcmp(key, "hdd_partition") == 0) {
                        strncpy(cfg->hdd_partition, val, CONFIG_PARTITION_MAX - 1);
                        cfg->hdd_partition[CONFIG_PARTITION_MAX - 1] = '\0';
                    } else if (strcmp(key, "model_path") == 0) {
                        strncpy(cfg->model_path, val, CONFIG_MODEL_PATH_MAX - 1);
                        cfg->model_path[CONFIG_MODEL_PATH_MAX - 1] = '\0';
                    }
                }
            }

            li = 0;
            if (*p == '\0') break;
        } else if (li < (int)sizeof(line) - 1) {
            line[li++] = *p;
        }
    }
}

static int try_read_config_from(const char *path, PS2LLMConfig *cfg) {
    void *blob = NULL;
    size_t size = 0;
    if (storage_try_read_whole_file(path, &blob, &size) != 0) return -1;

    char *text = malloc(size + 1);
    if (!text) { 
        free(blob); 
        return -1; 
    }
    memcpy(text, blob, size);
    text[size] = '\0';
    free(blob);

    parse_ini_text(text, cfg);
    free(text);
    return 0;
}

static int try_create_default_ini(const char *target_path) {
    /* Avoid writing to optical media */
    if (strncmp(target_path, "cdrom", 5) == 0) return -1;

    int fd = open(target_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;

    size_t len = strlen(DEFAULT_INI_CONTENT);
    int written = write(fd, DEFAULT_INI_CONTENT, len);
    close(fd);

    return (written == (int)len) ? 0 : -1;
}

int config_load(PS2LLMConfig *out) {
    set_defaults(out);
    storage_ensure_sif_ready();

    /* 1. Check USB Mass Storage (mass0:) */
    if (storage_try_init_usb() == 0) {
        if (try_read_config_from("mass0:/CONFIG.INI", out) == 0) {
            return 0;
        }
        /* Missing: attempt to create default on USB */
        if (try_create_default_ini("mass0:/CONFIG.INI") == 0) {
            return 0;
        }
    }

    /* 2. Check Internal HDD (pfs0:) */
    if (storage_try_init_hdd() == 0) {
        if (try_read_config_from("pfs0:/CONFIG.INI", out) == 0) {
            return 0;
        }
        /* Missing: attempt to create default on HDD */
        if (try_create_default_ini("pfs0:/CONFIG.INI") == 0) {
            return 0;
        }
    }

    /* 3. Check Optical Disc (cdrom0:) */
    /*
    if (storage_try_init_cdrom() == 0) {
        if (try_read_config_from("cdrom0:\\CONFIG.INI;1", out) == 0) {
            return 0;
        }
    }
    */

    /* 4. Default fallback: keep compiled defaults without hanging */
    return 0;
}