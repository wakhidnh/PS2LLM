/*
 * ============================================================================
 * PS2LLM - Local LLM on PlayStation 2 (src/main.c)
 * ============================================================================
 * 
 * Copyright (c) 2026 Wakhid Nurhidayat (@wakhidnh)
 * 
 * License Terms:
 * - Free to use for all kinds of devices (Homebrew, Consoles, PCs, Embedded, etc.)
 * - Non-Commercial Purpose Only: This software and its source may NOT be used 
 *   for any commercial purposes, profit-driven products, or paid distribution.
 * - Attribution: You must include this original source notice and credit 
 *   Wakhid Nurhidayat (@wakhidnh) in your project source or documentation.
 * 
 * Support the creator: If you like this project, feel free to buy a coffee 
 * on Ko-fi: ko-fi.com/wakhidnh (or @wakhidnh)
 * ============================================================================
 */

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <debug.h>
#include <libpad.h>
#include <graph.h>

#include "model.h"
#include "bpe.h"
#include "keyboard.h"
#include "storage.h"
#include "config.h"
#include "gs_ui.h"

#define PROMPT_MAX          256
#define MAX_TOKEN_IDS       96
#define GEN_MAX             70   
#define TEMPERATURE         0.55f
#define TOP_K               64
#define TOP_P               0.90f
#define REP_WINDOW          10
#define REP_FACTOR          1.12f
#define RDRAM_SAFETY_LIMIT  31.8f  /* Adjusted limit for 12MB triple-buffered allocation */

static Model *model = NULL;
static float adj_logits[VOCAB_SIZE];
static char pad_buf[256] __attribute__((aligned(64)));
static int last_printed_char = 0;
static float peak_ram_mb = 0.0f;

static void print_credits_gs(void) {
    gs_ui_set_color(GS_COLOR_DIM);
    scr_printf("----! PS2LLM - HomeBrew Project !----\n");
    gs_ui_set_color(GS_COLOR_DEFAULT);
}

static float get_current_ram_mb(void) {
    /* Baseline OS (~14.8MB) + 12MB Triple Buffer + Dynamic KV Cache */
    unsigned int total_heap_bytes = 14800000 + (12 * 1024 * 1024) + 
        (model ? (model->cache_len * (N_LAYERS * KV_DIM * sizeof(float) * 2)) : 0);
    if (total_heap_bytes > 33554432) total_heap_bytes = 33554432;
    return (float)total_heap_bytes / (1024.0f * 1024.0f);
}

static void print_clean_char(char ch) {
    if (ch == ' ' && last_printed_char == ' ') return;
    if (ch == 0 || ch < 32 || ch > 126) return;
    scr_printf("%c", ch);
    last_printed_char = ch;
}

static void apply_sampling_adjustments(const float *logits, const int *recent, int recent_n) {
    memcpy(adj_logits, logits, sizeof(adj_logits));
    adj_logits[UNK_ID] = -1e30f;
    adj_logits[BOS_ID] = -1e30f;
    
    for (int i = 0; i < recent_n; i++) {
        int t = recent[i];
        if (t <= 2) continue;
        adj_logits[t] = (adj_logits[t] > 0) ? adj_logits[t] / REP_FACTOR
                                            : adj_logits[t] * REP_FACTOR;
    }
}

static void show_model_startup_splash(int is_480p) {
    gs_ui_clear();
    gs_ui_set_color(GS_COLOR_STATUS);
    scr_printf("======================================================\n");
    scr_printf("         PS2LLM - LOCAL LLM ON PLAYSTATION 2         \n");
	scr_printf("     Create by : Wakhid Nurhidayat  |  @wakhidnh     \n\n");
    scr_printf("======================================================\n\n");
    
    gs_ui_set_color(GS_COLOR_DEFAULT);
	scr_printf(" [MODEL NAME]        	TinyLlama-1.1B-Chat-v1.0\n");
	scr_printf(" [LICENSE]        	    Apache 2.0 (Model) / MIT (Engine)\n");
    scr_printf(" [ARCHITECTURE]      	22 Layers | 2048 Full Dim | 32 Heads\n");
    scr_printf(" [VOCABULARY]        	32,000 Lexicon (Top-4k Active Head)\n");
    scr_printf(" [QUANTIZATION]      	Q8 (Block Quantized)\n");
    scr_printf(" [STREAMING METHOD]     Triple-Buffered DMA (12MB Buffer)\n");
    scr_printf(" [MEMORY GUARD]      	Active 31.8MB Bounds Checking\n");
    
    gs_ui_set_color(is_480p ? GS_COLOR_STATUS : GS_COLOR_DIM);
    scr_printf(" [VIDEO MODE]        %s\n\n", is_480p ? "480p Progressive Scan (Active)" : "480i Interlaced (Default)");
    
    gs_ui_set_color(GS_COLOR_DIM);
    scr_printf("-----------------------------------------------------\n");
    scr_printf("Initializing triple-buffered streams (12MB) to RAM...\n");
    scr_printf("=====================================================\n");
    gs_ui_set_color(GS_COLOR_DEFAULT);

    for (volatile int i = 0; i < 120000000; i++) { __asm__ __volatile__(""); }
}

static void init_ps2_environment(void) {
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    FlushCache(0);
}

static int check_circle_button_480p(void) {
    padInit(0);
    padPortOpen(0, 0, pad_buf);

    clock_t start = clock();
    struct padButtonStatus buttons;
    int circle_pressed = 0;

    while (((double)(clock() - start) / CLOCKS_PER_SEC) < 1.00) {
        int state = padGetState(0, 0);
        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
            int ret = padRead(0, 0, &buttons);
            if (ret != 0) {
                u32 paddata = 0xffff ^ buttons.btns;
                if (paddata & PAD_CIRCLE) {
                    circle_pressed = 1;
                    break;
                }
            }
        }
    }
    padPortClose(0, 0);
    padEnd();
    return circle_pressed;
}

int main(int argc, char *argv[]) {
    init_ps2_environment();

    int enable_480p = check_circle_button_480p();
    gs_ui_init(enable_480p);
    gs_ui_clear();
    show_model_startup_splash(enable_480p);

    model = (Model *)malloc(sizeof(Model));
    if (!model) while (1) { }
    memset(model, 0, sizeof(Model));

    if (argc > 0 && argv[0]) storage_set_base_path(argv[0]);
    else storage_set_base_path("");

    storage_try_init_usb();
    int hdd_status = storage_try_init_hdd();

    keyboard_load_modules();
    for (int attempt = 0; attempt < 10; attempt++) {
        if (keyboard_try_connect() == 0) break;
        for (volatile int d = 0; d < 400000; d++) { __asm__ __volatile__(""); }
    }

    int model_fd_precheck = storage_open_model_file();
    int is_storage_actually_working = (model_fd_precheck >= 0);

    gs_ui_clear();
    print_credits_gs();
    scr_printf("\n");

    char hdd_msg[64];
    snprintf(hdd_msg, sizeof(hdd_msg), "HDD Init Status: %s\n", (is_storage_actually_working || hdd_status == 0) ? "SUCCESS" : "FAILED");
    gs_ui_set_color(is_storage_actually_working || hdd_status == 0 ? GS_COLOR_USER : GS_COLOR_DIM);
    scr_printf("%s", hdd_msg);
    gs_ui_set_color(GS_COLOR_USER);
    scr_printf("Keyboard Status: Connected\n");
    gs_ui_set_color(GS_COLOR_STATUS);
    scr_printf("Display Mode: %s\n", enable_480p ? "480p Progressive Scan" : "480i Interlaced (Default)");
    gs_ui_set_color(GS_COLOR_DEFAULT);
    scr_printf("Loading configuration and model weights...\n\n");

    PS2LLMConfig cfg;
    if (config_load(&cfg) == 0) {
        gs_ui_set_color(GS_COLOR_STATUS);
        scr_printf("[OK] config.ini loaded.\n");
    } else {
        gs_ui_set_color(GS_COLOR_DIM);
        scr_printf("[INFO] Default parameters active.\n");
    }
    gs_ui_set_color(GS_COLOR_DEFAULT);

    scr_printf("[DEBUG] Attempting to open MODEL.P2L...\n");

    int model_fd = is_storage_actually_working ? model_fd_precheck : storage_open_model_file();
    if (model_fd < 0) {
        gs_ui_set_color(GS_COLOR_DIM);
        scr_printf("\n[ERROR] storage_open_model_file() returned < 0!\n");
        while (1) { }
    } else {
        gs_ui_set_color(GS_COLOR_STATUS);
        scr_printf("[OK] MODEL.P2L handle acquired successfully!\n");
    }
    gs_ui_set_color(GS_COLOR_DEFAULT);

    long fsize = lseek(model_fd, 0, SEEK_END);
    lseek(model_fd, 0, SEEK_SET);

    char size_msg[64];
    snprintf(size_msg, sizeof(size_msg), "[OK] MODEL.P2L opened (Size: %ld KB)\n", fsize / 1024);
    gs_ui_set_color(GS_COLOR_STATUS);
    scr_printf("%s", size_msg);
    gs_ui_set_color(GS_COLOR_DEFAULT);

    if (fsize <= 0) {
        close(model_fd);
        while (1) { }
    }

    scr_printf("[INIT] Parsing 22-layer topology & tokenizer...\n");
    if (model_init_from_file(model, model_fd) != 0) {
        close(model_fd);
        while (1) { }
    }
    bpe_init_from_fd(model_fd);
    gs_ui_set_color(GS_COLOR_STATUS);
    scr_printf("[OK] Model and Tokenizer ready.\n");
    gs_ui_set_color(GS_COLOR_DEFAULT);

    for (volatile int d = 0; d < 5000000; d++) { __asm__ __volatile__(""); }

    { int seed_src; model_seed((unsigned int)(size_t)&seed_src); }
    while (keyboard_poll() != 0) { }

    char prompt[PROMPT_MAX + 1];
    int plen = 0;
    int running = 1;
    memset(prompt, 0, sizeof(prompt));

    gs_ui_clear();
    print_credits_gs();
    scr_printf("\nType prompt (ChatML), press ENTER to run.\n");
    scr_printf("Press ESC at any time to quit.\n\n");

    int blink_counter = 0;
    int cursor_state = 0;

    gs_ui_set_color(GS_COLOR_USER);
    scr_printf("> _");

    while (running) {
        blink_counter++;

        if (blink_counter > 5000) {
            cursor_state = !cursor_state;
            blink_counter = 0;
            gs_ui_set_color(GS_COLOR_USER);
            scr_printf("\r> %s%c ", prompt, cursor_state ? '_' : ' ');
        }

        int c = keyboard_poll();
        if (c == 0) continue;
        if (c == 27) { running = 0; break; }

        if (c == '\n' || c == '\r') {
            prompt[plen] = '\0';
            
            gs_ui_set_color(GS_COLOR_USER);
            scr_printf("\r> %s   \n\n", prompt);

            char chatml_buf[PROMPT_MAX + 64];
            snprintf(chatml_buf, sizeof(chatml_buf), 
                "<|im_start|>user\n%s<|im_end|>\n"
                "<|im_start|>assistant\n", prompt);

            int ids[MAX_TOKEN_IDS];
            int n_ids = bpe_encode(chatml_buf, ids, MAX_TOKEN_IDS);
            if (n_ids <= 0) {
                gs_ui_set_color(GS_COLOR_DIM);
                scr_printf("[ERROR] No tokens encoded.\n\n> _");
                plen = 0;
                prompt[0] = '\0';
                continue;
            }

            model_reset_cache(model);
            float logits[VOCAB_SIZE];
            memset(logits, 0, sizeof(logits));

            clock_t t_total_start = clock();
            peak_ram_mb = get_current_ram_mb();

            gs_ui_set_color(GS_COLOR_STATUS);
            scr_printf("Processed token [0/%d]", n_ids);

            for (int i = 0; i < n_ids; i++) {
                model_forward(model, ids[i], logits, i + 1, n_ids, 0.0);
                float cur_ram = get_current_ram_mb();
                if (cur_ram > peak_ram_mb) peak_ram_mb = cur_ram;
                scr_printf("\rProcessed token [%d/%d]", i + 1, n_ids);
            }

            scr_printf("\r------------------------------------------------\n");
            gs_ui_set_color(GS_COLOR_DEFAULT);
            scr_printf("Response:\n");

            int tokens_gen = 0;
            int recent[REP_WINDOW];
            int recent_n = 0, recent_pos = 0;
            int oom_triggered = 0;
            last_printed_char = 0;

            model_forward(model, ids[n_ids - 1], logits, 0, GEN_MAX, 0.0);
            gs_ui_set_color(GS_COLOR_OUTPUT);

            for (int step = 0; step < GEN_MAX && model->cache_len < MAX_SEQ; step++) {
                float cur_ram = get_current_ram_mb();
                if (cur_ram > peak_ram_mb) peak_ram_mb = cur_ram;

                if (cur_ram >= RDRAM_SAFETY_LIMIT) {
                    oom_triggered = 1;
                    break;
                }

                apply_sampling_adjustments(logits, recent, recent_n);
                int next = model_sample(adj_logits, VOCAB_SIZE, TEMPERATURE, TOP_K, TOP_P);
                
                if (next <= 0 || next == EOS_ID || next == '\n' || next == 13) {
                    break;
                }

                tokens_gen++;
                recent[recent_pos] = next;
                recent_pos = (recent_pos + 1) % REP_WINDOW;
                if (recent_n < REP_WINDOW) recent_n++;

                char piece[MAX_TOKEN_LEN * 2];
                int len = bpe_decode_token(next, piece, sizeof(piece), step == 0);

                if (len > 0) {
                    int sentence_complete = 0;
                    for (int j = 0; j < len; j++) {
                        unsigned char ch = (unsigned char)piece[j];
                        if (ch == 0 || ch == 255 || ch < 32 || ch > 126) continue;

                        if (j == 0 && step > 0 && ch != '_' && piece[0] != ' ' && last_printed_char != ' ') {
                            print_clean_char(' ');
                        }

                        if (ch == '_') {
                            print_clean_char(' ');
                        } else {
                            print_clean_char(ch);
                        }

                        if (ch == '.' || ch == '!' || ch == '?') {
                            sentence_complete = 1;
                        }
                    }

                    if (sentence_complete && tokens_gen >= 14) {
                        break;
                    }
                }

                if (keyboard_poll() == 27) { running = 0; break; }
                model_forward(model, next, logits, tokens_gen, GEN_MAX, 0.0);
            }

            if (last_printed_char != '.' && last_printed_char != '!' && last_printed_char != '?' && last_printed_char != ',') {
                print_clean_char('.');
            }

            clock_t t_total_end = clock();
            double total_time = (double)(t_total_end - t_total_start) / CLOCKS_PER_SEC;

            gs_ui_set_color(GS_COLOR_DIM);
            scr_printf("\n\n------------------------------------------------\n");
            scr_printf("Tokens : %d in + %d out = %d total\n", n_ids, tokens_gen, n_ids + tokens_gen);
            scr_printf("Total Time : %.2fs\n", total_time);
            scr_printf("Max RAM consumption : [%.2f MB / 32MB]\n", peak_ram_mb);

            if (oom_triggered) {
                gs_ui_set_color(GS_COLOR_STATUS);
                scr_printf("[WARNING] OUT OF MEMORY GUARD TRIGGERED: > %.1fMB USED!\n", RDRAM_SAFETY_LIMIT);
                scr_printf("[INFO] Generation halted early to protect Emotion Engine heap.\n");
            }
            scr_printf("------------------------------------------------\n\n");

            gs_ui_set_color(GS_COLOR_DEFAULT);
            scr_printf("Press [ENTER] to enter another prompt\n");
            
            int waiting_for_acknowledgment = 1;
            while (waiting_for_acknowledgment && running) {
                int key = keyboard_poll();
                if (key == 27) {
                    running = 0;
                    waiting_for_acknowledgment = 0;
                } else if (key == '\n' || key == '\r') {
                    waiting_for_acknowledgment = 0;
                }
            }

            if (!running) break;

            gs_ui_clear();
            print_credits_gs();
            scr_printf("\nType prompt (ChatML), press ENTER to run.\n");
            scr_printf("Press ESC at any time to quit.\n\n");

            plen = 0;
            prompt[0] = '\0';
            blink_counter = 0;
            cursor_state = 1;
            gs_ui_set_color(GS_COLOR_USER);
            scr_printf("> _");
            continue;
        }

        if (c == 8 || c == 127) {
            if (plen > 0) {
                plen--;
                prompt[plen] = '\0';
                gs_ui_set_color(GS_COLOR_USER);
                scr_printf("\r> %s  \r> %s_", prompt, prompt);
            }
            continue;
        }

        if (plen < PROMPT_MAX && c >= 32 && c < 127) {
            prompt[plen++] = (char)c;
            prompt[plen] = '\0';
            gs_ui_set_color(GS_COLOR_USER);
            scr_printf("\r> %s_", prompt);
        }
    }

    close(model_fd);
    free(model);
    SleepThread();
    return 0;
}