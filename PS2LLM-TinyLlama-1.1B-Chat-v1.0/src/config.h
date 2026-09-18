#ifndef CONFIG_H
#define CONFIG_H

/* PS2 32 MB Memory Budget Partitioning for SmolLM2-1.7B */
#define MEM_RESERVED_SYSTEM    (6 * 1024 * 1024)   /* Kernel, IOP, GS Framebuffers, Keyboard */
#define MEM_ACTIVE_TENSOR_BUF  (17 * 1024 * 1024)  /* Largest tensor: 2048 * 8192 int8 = 16.7 MB */
#define MEM_KV_CACHE_BUF       (9 * 1024 * 1024)   /* Bounded dynamic context */

#define CONFIG_MODEL_PATH_MAX 128
#define CONFIG_PARTITION_MAX 32

typedef enum {
    STORAGE_AUTO = 0,
    STORAGE_HDD,
    STORAGE_USB,
    STORAGE_CDROM
} StorageDevice;

typedef struct {
    StorageDevice device;
    char hdd_partition[CONFIG_PARTITION_MAX];
    char model_path[CONFIG_MODEL_PATH_MAX];
} PS2LLMConfig;

int config_load(PS2LLMConfig *out);

#endif