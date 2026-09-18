#ifndef BPE_H
#define BPE_H

#include <stddef.h>
#include "vocab.h"

int bpe_init_from_fd(int fd);
int bpe_encode(const char *text, int *out_tokens, int max_tokens);
int bpe_decode_token(int token_id, char *out_str, size_t out_max, int is_first);

#endif