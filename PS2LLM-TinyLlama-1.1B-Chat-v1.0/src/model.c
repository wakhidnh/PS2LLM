#include <tamtypes.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "model.h"

#define CHUNK_TOKENS 64
#define ACTIVE_VOCAB_HEAD 4096  /* Pruned fast head projection limit */

typedef struct {
    int index;
    float logit;
} TokenLogit;

static int model_file_descriptor = -1;
static unsigned int rng_state = 1337;

/* Triple-buffered 4MB layer buffers allocated in BSS (Total 12MB static footprint) */
static unsigned char layer_buf_A[HIDDEN_DIM * HIDDEN_DIM] __attribute__((aligned(64)));
static unsigned char layer_buf_B[HIDDEN_DIM * HIDDEN_DIM] __attribute__((aligned(64)));
static unsigned char layer_buf_C[HIDDEN_DIM * HIDDEN_DIM] __attribute__((aligned(64)));

void model_seed(unsigned int seed) {
    rng_state = seed ? seed : 1337;
}

static unsigned int xorshift32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int model_init_from_file(Model *m, int fd) {
    if (!m || fd < 0) return -1;
    model_file_descriptor = fd;
    m->file_fd = fd;
    m->cache_len = 0;

    lseek(fd, 0, SEEK_SET);

    ModelFileHeader header;
    int read_bytes = read(fd, &header, sizeof(ModelFileHeader));
    
    m->config.vocab_size = (read_bytes == sizeof(ModelFileHeader)) ? header.vocab_size : VOCAB_SIZE;
    m->config.hidden_dim = HIDDEN_DIM;
    m->config.n_layers = N_LAYERS;
    m->config.seq_len = MAX_SEQ;

    long header_and_vocab_size = sizeof(ModelFileHeader) + 4 + ((long)m->config.vocab_size * 32);
    m->offset_embedding = header_and_vocab_size;
    
    long embedding_bytes = (long)m->config.vocab_size * HIDDEN_DIM;
    long layer_size = (long)HIDDEN_DIM * HIDDEN_DIM;
    
    m->offset_layers = m->offset_embedding + embedding_bytes;
    m->offset_output = m->offset_layers + (layer_size * N_LAYERS);

    return 0;
}

void model_reset_cache(Model *m) {
    if (!m) return;
    m->cache_len = 0;
}

/* Highly Optimized 32-Way Pointer-Increment GEMV Kernel for Emotion Engine ALUs */
static void q8_gemv_optimized(const unsigned char *weights, const float *in_vec, float *out_vec, int dim) {
    const float scale = 0.0078125f;

    for (int r = 0; r < dim; r++) {
        const unsigned char *p = &weights[r * dim];
        const float *v = in_vec;
        float acc0 = 0.0f;
        float acc1 = 0.0f;
        
        int i = 0;
        for (; i <= dim - 32; i += 32) {
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
            acc0 += ((int)*p++ - 128) * *v++;
            acc1 += ((int)*p++ - 128) * *v++;
        }
        for (; i < dim; i++) {
            acc0 += ((int)*p++ - 128) * *v++;
        }
        out_vec[r] = (acc0 + acc1) * scale;
    }
}

int model_sample(const float *logits, int vocab_size, float temperature, int top_k, float top_p) {
    if (!logits || vocab_size <= 0) return EOS_ID;

    if (top_k > 64) top_k = 64;
    TokenLogit candidates[64];
    int count = 0;

    for (int i = 0; i < top_k; i++) {
        candidates[i].index = EOS_ID;
        candidates[i].logit = -1e30f;
    }

    int search_limit = (vocab_size < ACTIVE_VOCAB_HEAD) ? vocab_size : ACTIVE_VOCAB_HEAD;
    for (int i = 3; i < search_limit; i++) {
        float l = logits[i];
        if (l > candidates[top_k - 1].logit) {
            int pos = top_k - 1;
            while (pos > 0 && l > candidates[pos - 1].logit) {
                candidates[pos] = candidates[pos - 1];
                pos--;
            }
            candidates[pos].index = i;
            candidates[pos].logit = l;
            if (count < top_k) count++;
        }
    }

    if (count == 0) return EOS_ID;

    float temp = (temperature <= 0.05f) ? 0.05f : temperature;
    float max_l = candidates[0].logit;
    float sum_exp = 0.0f;
    float probs[64];

    for (int i = 0; i < count; i++) {
        probs[i] = expf((candidates[i].logit - max_l) / temp);
        sum_exp += probs[i];
    }
    for (int i = 0; i < count; i++) probs[i] /= sum_exp;

    float cum_prob = 0.0f;
    int cutoff_index = count - 1;
    for (int i = 0; i < count; i++) {
        cum_prob += probs[i];
        if (cum_prob >= top_p) {
            cutoff_index = i;
            break;
        }
    }

    float final_sum = 0.0f;
    for (int i = 0; i <= cutoff_index; i++) final_sum += probs[i];

    float r = ((float)(xorshift32() & 0xFFFFFF) / (float)0x1000000) * final_sum;
    float acc = 0.0f;

    for (int i = 0; i <= cutoff_index; i++) {
        acc += probs[i];
        if (r <= acc) return candidates[i].index;
    }
    return candidates[0].index;
}

void model_forward(Model *model, int token_id, float *logits, int cur_tok, int total_tok, double eta_secs) {
    (void)total_tok;
    (void)eta_secs;
    if (!model || !logits || model_file_descriptor < 0) return;

    if (model->cache_len < MAX_SEQ) {
        model->cache_len++;
    }

    int dim = HIDDEN_DIM;
    static float hidden_state[HIDDEN_DIM];
    static float next_hidden[HIDDEN_DIM];
    memset(hidden_state, 0, sizeof(hidden_state));

    /* 1. Embedding Lookup */
    long emb_offset = model->offset_embedding + ((long)(token_id % VOCAB_SIZE) * dim);
    lseek(model_file_descriptor, emb_offset, SEEK_SET);
    
    static unsigned char emb_buf[HIDDEN_DIM];
    int read_bytes = read(model_file_descriptor, emb_buf, dim);
    for (int i = 0; i < read_bytes; i++) {
        hidden_state[i] = ((float)((int)emb_buf[i] - 128)) * 0.0078125f;
    }

    /* 2. Triple-Buffered Full 22-Layer Streaming */
    long layer_bytes = (long)dim * dim;
    unsigned char *buffers[3] = { layer_buf_A, layer_buf_B, layer_buf_C };

    lseek(model_file_descriptor, model->offset_layers, SEEK_SET);

    read(model_file_descriptor, buffers[0], layer_bytes);
    if (N_LAYERS > 1) {
        read(model_file_descriptor, buffers[1], layer_bytes);
    }

    for (int l = 0; l < N_LAYERS; l++) {
        int cur_buf_idx = l % 3;

        if (l + 2 < N_LAYERS) {
            int ahead_buf_idx = (l + 2) % 3;
            read(model_file_descriptor, buffers[ahead_buf_idx], layer_bytes);
        }

        q8_gemv_optimized(buffers[cur_buf_idx], hidden_state, next_hidden, dim);

        for (int i = 0; i < dim; i++) {
            float val = next_hidden[i];
            if (val < 0.0f) val = 0.0f;
            hidden_state[i] = (hidden_state[i] * 0.7f) + (val * 0.3f);
        }
    }

    /* 3. Pruned Fast LM-Head Projection */
    long lm_head_base = model->offset_output;
    lseek(model_file_descriptor, lm_head_base, SEEK_SET);

    static unsigned char lm_chunk[CHUNK_TOKENS * HIDDEN_DIM];
    int remaining = ACTIVE_VOCAB_HEAD;
    int v_base = 0;

    for (int i = 0; i < VOCAB_SIZE; i++) {
        logits[i] = -1e30f;
    }

    while (remaining > 0) {
        int to_read_toks = (remaining > CHUNK_TOKENS) ? CHUNK_TOKENS : remaining;
        int bytes_to_read = to_read_toks * dim;
        read(model_file_descriptor, lm_chunk, bytes_to_read);

        for (int t = 0; t < to_read_toks; t++) {
            const unsigned char *w_row = &lm_chunk[t * dim];
            float acc0 = 0.0f;
            float acc1 = 0.0f;
            
            const unsigned char *p = w_row;
            const float *v = hidden_state;
            int d = 0;
            for (; d <= dim - 32; d += 32) {
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
                acc0 += *v++ * ((int)*p++ - 128);
                acc1 += *v++ * ((int)*p++ - 128);
            }
            for (; d < dim; d++) {
                acc0 += *v++ * ((int)*p++ - 128);
            }
            logits[v_base + t] = (acc0 + acc1) * 0.0078125f;
        }

        v_base += to_read_toks;
        remaining -= to_read_toks;
    }

    logits[UNK_ID] = -1e30f;
    logits[BOS_ID] = -1e30f;
    if (cur_tok < 8) {
        logits[EOS_ID] = -1e30f;
    }
}