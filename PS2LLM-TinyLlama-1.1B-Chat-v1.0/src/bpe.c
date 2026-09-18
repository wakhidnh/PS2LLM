#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "bpe.h"
#include "vocab.h"
#include "model.h"

#define TOKEN_SLOT_SIZE  32
#define BATCH_TOKENS     512
#define CACHE_SLOTS      256

static int g_model_fd = -1;
static off_t g_vocab_offset = 0;

typedef struct {
    int token_id;
    char text[TOKEN_SLOT_SIZE];
} TokenCacheEntry;

static TokenCacheEntry dcache[CACHE_SLOTS];
static int dcache_ready = 0;

int bpe_init_from_fd(int fd) {
    if (fd < 0) return -1;
    g_model_fd = fd;
    
    g_vocab_offset = (off_t)sizeof(ModelFileHeader) + (off_t)sizeof(int);

    for (int i = 0; i < CACHE_SLOTS; i++) {
        dcache[i].token_id = -1;
    }
    dcache_ready = 1;
    return 0;
}

int bpe_decode_token(int token_id, char *out_str, size_t out_max, int is_first) {
    if (g_model_fd < 0 || token_id < 0 || token_id >= VOCAB_SIZE || !out_str || out_max == 0) {
        if (out_str && out_max > 0) out_str[0] = '\0';
        return 0;
    }

    char slot[TOKEN_SLOT_SIZE];
    int cache_idx = token_id % CACHE_SLOTS;

    if (dcache_ready && dcache[cache_idx].token_id == token_id) {
        memcpy(slot, dcache[cache_idx].text, TOKEN_SLOT_SIZE);
    } else {
        off_t target = g_vocab_offset + ((off_t)token_id * TOKEN_SLOT_SIZE);
        lseek(g_model_fd, target, SEEK_SET);

        int n = read(g_model_fd, slot, TOKEN_SLOT_SIZE);
        if (n <= 0) {
            out_str[0] = '\0';
            return 0;
        }
        slot[TOKEN_SLOT_SIZE - 1] = '\0';

        // SANITIZER: Clean out any non-printable or weird high-bit bytes right after reading
        for (int i = 0; i < TOKEN_SLOT_SIZE; i++) {
            unsigned char c = (unsigned char)slot[i];
            if (c == 0) break;
            if (c < 32 || c > 126) {
                slot[i] = ' ';
            }
        }

        if (dcache_ready) {
            dcache[cache_idx].token_id = token_id;
            memcpy(dcache[cache_idx].text, slot, TOKEN_SLOT_SIZE);
        }
    }

    const char *tok = slot;
    if (is_first && (tok[0] == ' ' || (unsigned char)tok[0] == 0xC4 || (unsigned char)tok[0] == 0xE2)) {
        tok++;
    }

    strncpy(out_str, tok, out_max - 1);
    out_str[out_max - 1] = '\0';
    return (int)strlen(out_str);
}

int bpe_encode(const char *text, int *out_tokens, int max_tokens) {
    if (!text || max_tokens <= 0 || g_model_fd < 0) return 0;
    int n_tokens = 0;

    out_tokens[n_tokens++] = BOS_ID;

    char batch[BATCH_TOKENS * TOKEN_SLOT_SIZE];
    const char *p = text;

    while (*p && n_tokens < max_tokens - 1) {
        int best_id = -1;
        size_t best_len = 0;

        for (int b = 0; b < VOCAB_SIZE; b += BATCH_TOKENS) {
            int count = (b + BATCH_TOKENS > VOCAB_SIZE) ? (VOCAB_SIZE - b) : BATCH_TOKENS;
            off_t off = g_vocab_offset + ((off_t)b * TOKEN_SLOT_SIZE);
            lseek(g_model_fd, off, SEEK_SET);
            int n = read(g_model_fd, batch, count * TOKEN_SLOT_SIZE);
            if (n <= 0) break;

            for (int i = 0; i < count; i++) {
                char *candidate = batch + (i * TOKEN_SLOT_SIZE);
                candidate[TOKEN_SLOT_SIZE - 1] = '\0';
                size_t len = strlen(candidate);

                if (len > 0 && len > best_len && strncmp(p, candidate, len) == 0) {
                    best_len = len;
                    best_id = b + i;
                }
            }
        }

        if (best_id >= 0 && best_len > 0) {
            out_tokens[n_tokens++] = best_id;
            p += best_len;
        } else {
            out_tokens[n_tokens++] = (unsigned char)(*p);
            p++;
        }
    }

    return n_tokens;
}