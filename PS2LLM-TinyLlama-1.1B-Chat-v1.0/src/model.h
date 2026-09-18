#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "vocab.h"

#define VOCAB_SIZE         32000
#define ACTIVE_VOCAB_HEAD  4096    /* Evaluates top-4096 English vocabulary to save ~57MB read overhead */
#define HIDDEN_DIM         2048
#define N_LAYERS           22
#define N_HEADS            32
#define N_KV_HEADS         4
#define HEAD_DIM           (HIDDEN_DIM / N_HEADS)       /* 64 */
#define KV_DIM             (N_KV_HEADS * HEAD_DIM)      /* 256 */
#define INTERMEDIATE_DIM   5632
#define MAX_SEQ            96

#define EOS_ID             2
#define BOS_ID             1
#define UNK_ID             0

#define MODEL_MAGIC        0x4D4C3250u
#define MODEL_FORMAT_VERSION 7

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t hidden_dim;
    uint32_t n_layers;
    uint32_t n_heads;
    uint32_t n_kv_heads;
    uint32_t intermediate_dim;
    uint32_t vocab_size;
} ModelFileHeader;

typedef struct {
    int magic;
    int vocab_size;
    int hidden_dim;
    int intermediate_dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int seq_len;
} ModelConfig;

typedef struct {
    int file_fd;
    ModelConfig config;
    int cache_len;

    /* Scaled KV Cache: 22 * 96 * 256 * 4 bytes * 2 = ~4.32 MB in RDRAM */
    float kcache[N_LAYERS][MAX_SEQ][KV_DIM];
    float vcache[N_LAYERS][MAX_SEQ][KV_DIM];

    long offset_embedding;
    long offset_layers;
    long offset_output;
} Model;

int  model_init_from_file(Model *m, int file_fd);
void model_reset_cache(Model *m);
void model_forward(Model *model, int token_id, float *logits, int cur_tok, int total_tok, double eta_secs);
int  model_sample(const float *logits, int vocab_size, float temperature, int top_k, float top_p);
void model_seed(unsigned int seed);

#endif /* MODEL_H */