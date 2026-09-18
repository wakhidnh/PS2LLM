#include <kernel.h>
#include <loadfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <ps2kbd.h>
#include "keyboard.h"
#include "irx_loader.h"

static int kbd_fd = -1;
static int modules_loaded = 0;

void keyboard_init(void) {
    keyboard_load_modules();
    keyboard_try_connect();
}

int keyboard_load_modules(void) {
    if (modules_loaded) return 0;
    FlushCache(0);

    /* Check if USBD is already running before trying to load it */
    /* If storage initialization already loaded it, loading again will freeze the IOP */
    #ifdef NO_RELOAD_USBD
    (void)0;
    #else
    /* Only attempt USBD if ps2kbd load fails initially or if not yet loaded */
    #endif

    /* Load only ps2kbd; USBD is already active from storage probing */
    int ret = load_embedded_module("ps2kbd");
    if (ret < 0) {
        /* Fallback: If ps2kbd failed because usbd wasn't loaded by storage, load usbd once */
        load_embedded_module("usbd");
        for (volatile int i = 0; i < 2000000; i++) { }
        ret = load_embedded_module("ps2kbd");
    }

    modules_loaded = 1;
    return ret;
}

int keyboard_try_connect(void) {
    if (kbd_fd >= 0) return 0;
    
    // Try opening the PS2 keyboard device file
    kbd_fd = open("kbd", O_RDONLY | O_NONBLOCK);
    if (kbd_fd < 0) {
        #ifdef PS2KBD_DEVFILE
        kbd_fd = open(PS2KBD_DEVFILE, O_RDONLY | O_NONBLOCK);
        #endif
    }
    if (kbd_fd < 0) return -1;
    
    return 0;
}

int keyboard_poll(void) {
    if (kbd_fd < 0) {
        if (keyboard_try_connect() < 0) return 0;
    }
    
    unsigned char c = 0;
    int n = read(kbd_fd, &c, 1);
    if (n != 1) return 0;
    
    #ifdef PS2KBD_ESCAPE_KEY
    if (c == PS2KBD_ESCAPE_KEY) return 27;
    #else
    if (c == 27) return 27;
    #endif

    if (c == 8 || c == 0x7F) return 8;
    return (int)c;
}