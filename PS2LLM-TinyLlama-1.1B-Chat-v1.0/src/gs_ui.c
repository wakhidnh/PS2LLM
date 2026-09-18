#include <tamtypes.h>
#include <kernel.h>
#include <debug.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <graph.h>
#include <gs_psm.h>

#include "gs_ui.h"
#include "font.h"

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  448

#define FONT_SCALE     2
#define CHAR_W         (8 * FONT_SCALE)
#define CHAR_H         (8 * FONT_SCALE)

#define COLS           (SCREEN_WIDTH / CHAR_W)
#define ROWS           (SCREEN_HEIGHT / CHAR_H)
#define SCROLL_ROW_MAX (ROWS - 2)

static char text_grid[ROWS][COLS + 1];
static char last_rendered_grid[ROWS][COLS + 1];
static int cursor_x = 0;
static int cursor_y = 0;
static int force_full_redraw = 1;

static int dirty_top = 0;
static int dirty_bottom = SCROLL_ROW_MAX - 1;

/* VSync / FPS tracking */
static int vsync_counter = 0;
static int last_fps = 60;
static clock_t last_time = 0;

void gs_ui_set_color(unsigned int color) {
    scr_setfontcolor(color);
}

void gs_ui_init(int progressive) {
    graph_wait_vsync();
    if (progressive) {
        // Set to 480p Progressive at boot
        graph_set_mode(GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_480P, GRAPH_MODE_FIELD, GRAPH_ENABLE);
    } else {
        // Set to 480i Interlaced (Default)
        graph_set_mode(GRAPH_MODE_INTERLACED, GRAPH_MODE_NTSC, GRAPH_MODE_FIELD, GRAPH_ENABLE);
    }
    graph_set_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    graph_set_framebuffer_filtered(0, SCREEN_WIDTH, GS_PSM_32, 0, 0);

    init_scr();
    scr_setbgcolor(GS_COLOR_BG);
    scr_setfontcolor(GS_COLOR_DEFAULT);
    scr_clear();
    memset(text_grid, 0, sizeof(text_grid));
    memset(last_rendered_grid, 0, sizeof(last_rendered_grid));
    force_full_redraw = 1;
    dirty_top = 0;
    dirty_bottom = SCROLL_ROW_MAX - 1;
    last_time = clock();
}

void gs_ui_clear(void) {
    memset(text_grid, 0, sizeof(text_grid));
    memset(last_rendered_grid, 0, sizeof(last_rendered_grid));
    cursor_x = 0;
    cursor_y = 0;
    scr_clear();
    force_full_redraw = 1;
    dirty_top = 0;
    dirty_bottom = SCROLL_ROW_MAX - 1;
}

static void scroll_up(void) {
    for (int y = 0; y < SCROLL_ROW_MAX - 1; y++) {
        memcpy(text_grid[y], text_grid[y + 1], COLS + 1);
    }
    memset(text_grid[SCROLL_ROW_MAX - 1], 0, COLS + 1);
    cursor_y = SCROLL_ROW_MAX - 1;
    dirty_top = 0;
    dirty_bottom = SCROLL_ROW_MAX - 1;
    force_full_redraw = 1;
}

void gs_ui_putc(char c) {
    if (cursor_y >= SCROLL_ROW_MAX) {
        scroll_up();
    }

    if (cursor_y < dirty_top) dirty_top = cursor_y;
    if (cursor_y > dirty_bottom) dirty_bottom = cursor_y;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
        memset(text_grid[cursor_y], 0, COLS + 1);
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            text_grid[cursor_y][cursor_x] = '\0';
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = COLS - 1;
            text_grid[cursor_y][cursor_x] = '\0';
        }
    } else {
        if (cursor_x < COLS) {
            text_grid[cursor_y][cursor_x++] = c;
            text_grid[cursor_y][cursor_x] = '\0';
        } else {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= SCROLL_ROW_MAX) {
                scroll_up();
            }
            text_grid[cursor_y][cursor_x++] = c;
            text_grid[cursor_y][cursor_x] = '\0';
        }
    }

    if (cursor_y < dirty_top) dirty_top = cursor_y;
    if (cursor_y > dirty_bottom) dirty_bottom = cursor_y;
}

void gs_ui_print(const char *s) {
    if (!s) return;
    while (*s) {
        gs_ui_putc(*s++);
    }
    gs_ui_redraw(NULL);
}

void gs_ui_set_status(const char *status_msg) {
    (void)status_msg;
}

void gs_ui_draw_fps(void) {
    vsync_counter++;
    clock_t current_time = clock();
    if ((double)(current_time - last_time) / CLOCKS_PER_SEC >= 1.0) {
        last_fps = vsync_counter;
        vsync_counter = 0;
        last_time = current_time;

        scr_setXY(COLS - 10, 0);
        scr_setfontcolor(GS_COLOR_DIM);
        scr_printf("[%2d FPS]", last_fps);
        scr_setfontcolor(GS_COLOR_DEFAULT);
    }
}

void gs_ui_redraw(const char *current_prompt) {
    scr_setbgcolor(GS_COLOR_BG);

    graph_wait_vsync();
    gs_ui_draw_fps();

    if (force_full_redraw) {
        scr_clear();
        for (int y = 0; y < SCROLL_ROW_MAX; y++) {
            if (text_grid[y][0] != '\0') {
                scr_setfontcolor(GS_COLOR_DEFAULT);
                scr_printf("%s\n", text_grid[y]);
            }
        }
        memcpy(last_rendered_grid, text_grid, sizeof(text_grid));
        force_full_redraw = 0;
        dirty_top = 0;
        dirty_bottom = SCROLL_ROW_MAX - 1;
    } else {
        for (int y = dirty_top; y <= dirty_bottom && y < SCROLL_ROW_MAX; y++) {
            if (strcmp(text_grid[y], last_rendered_grid[y]) != 0) {
                scr_setXY(0, y);
                scr_setfontcolor(GS_COLOR_BG);
                scr_printf("%s", last_rendered_grid[y]);
                
                scr_setXY(0, y);
                scr_setfontcolor(GS_COLOR_DEFAULT);
                scr_printf("%s", text_grid[y]);
                
                memcpy(last_rendered_grid[y], text_grid[y], COLS + 1);
            }
        }
        dirty_top = SCROLL_ROW_MAX;
        dirty_bottom = 0;
    }

    if (current_prompt) {
        scr_setXY(0, SCROLL_ROW_MAX);
        scr_setfontcolor(GS_COLOR_USER);
        scr_printf("> %s   ", current_prompt);
        scr_setfontcolor(GS_COLOR_DEFAULT);
    }
}