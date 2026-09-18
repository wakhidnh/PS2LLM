#ifndef GS_UI_H
#define GS_UI_H

/* Color palette constants (0x00BBGGRR format used by libdebug) */
#define GS_COLOR_DEFAULT  0x00FFFFFF  // White
#define GS_COLOR_USER     0x0000FF00  // Bright Green
#define GS_COLOR_STATUS   0x00FFFF00  // Cyan (Blue + Green)
#define GS_COLOR_OUTPUT   0x0055FFFF  // Warm Amber / Light Yellow
#define GS_COLOR_DIM      0x00808080  // Gray
#define GS_COLOR_BG       0x00000000  // Black

void gs_ui_init(int progressive);
void gs_ui_clear(void);
void gs_ui_putc(char c);
void gs_ui_print(const char *s);
void gs_ui_set_color(unsigned int color);
void gs_ui_redraw(const char *current_prompt);
void gs_ui_set_status(const char *status_msg);
void gs_ui_draw_fps(void);

#endif