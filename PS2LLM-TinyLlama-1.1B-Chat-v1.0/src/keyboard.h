#ifndef KEYBOARD_H
#define KEYBOARD_H

/*
 * USB keyboard bring-up, isolated to this file. See README's "Known
 * risk points" - this is the part most likely to need adjustment for
 * your exact PS2SDK version (IRX filenames and kbd.h's exact function
 * names have changed across SDK releases).
 */

/* Loads usbd.irx + the keyboard HID IRX via the IOP. Call once at
 * startup, before any connect attempts. Returns 0 on success. */
int keyboard_load_modules(void);

/* Attempts a single, immediate open of the keyboard device node.
 * Does NOT retry or block internally - call this repeatedly from your
 * own loop (e.g. to wait for a keyboard to be physically plugged in)
 * with whatever pacing/UI you want between attempts. Returns 0 once a
 * keyboard is connected and ready to poll. */
int keyboard_try_connect(void);

/* Non-blocking poll. Returns an ASCII character if one was typed since
 * the last call, or 0 if nothing new / an unmapped key was pressed.
 * Returns 27 (ESC) if the Escape key was pressed, so callers can use
 * that to break out of loops. Handles Shift for letters/punctuation
 * via a small built-in US keymap - no support for dead keys, IME,
 * or non-US layouts. */
int keyboard_poll(void);

#endif
