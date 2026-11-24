// SPDX-License-Identifier: GPL-3.0-or-later
//
// EL0 regression fixture for the console's new parts (project_doc/
// koracli-plan.md, step 1a), run by the smoke test in a console of its own
// (`newcon keyprobe`) and driven with the debug console's `key`:
//
//   1. its size without bars and with a top and a bottom bar (two rows fewer):
//      "keyprobe: size COLSxROWS", then "keyprobe: bars, size COLSxROWS"
//   2. the key-event mode: "keyprobe: events", then a line per key pressed,
//      "keyprobe: key KK mods MMMM char C" (hex key and modifiers, the
//      character's code point in decimal), until F10
//   3. ESC as a break: "keyprobe: ESC is a break", then "keyprobe: break" when
//      ESC is pressed; "keyprobe: ESC is a key", then "keyprobe: ESC as a key"
//      when it comes as an event again
//   4. the bars taken away and the size back: "keyprobe: done COLSxROWS"
//
// A failure prints "keyprobe: FAILED ..." and the program exits with 1.
#include "../../user/libk/koraos.h"

static int fail(const char *what) {
    printf("keyprobe: FAILED %s\n", what);
    console_mode(CON_COOKED);
    return 1;
}

// The console's size, asked in raw mode (CSI 18 t), as conprobe asks it.
static int size(unsigned *cols, unsigned *rows) {
    console_mode(CON_RAW);
    fputs("\x1b[18t", stdout);
    fflush(stdout);
    char answer[32];  // ESC [ 8 ; rows ; cols t
    size_t n = 0;
    int c;
    while (n < sizeof answer - 1 && (c = getchar()) != EOF) {
        answer[n++] = (char)c;
        if (c == 't') {
            break;
        }
    }
    answer[n] = '\0';
    console_mode(CON_COOKED);
    return sscanf(answer, "\x1b[8;%u;%ut", rows, cols) == 2 ? 0 : -1;
}

// The next key event, waiting for it.
static int next_event(struct con_key_event *event) {
    return console_events(event, 1) == 1 ? 0 : -1;
}

int main(void) {
    unsigned cols, rows, bar_cols, bar_rows;
    if (size(&cols, &rows) != 0) {
        return fail("no size");
    }
    printf("keyprobe: size %ux%u\n", cols, rows);
    if (console_bar(CON_BAR_TOP, "keyprobe \xe2\x94\x80 top bar") != 0 ||
        console_bar(CON_BAR_BOTTOM, "KEY | F1 Help  F2 Mode  F10 Next") != 0) {
        return fail("bars refused");
    }
    if (size(&bar_cols, &bar_rows) != 0 || bar_cols != cols || bar_rows != rows - 2) {
        return fail("the grid did not lose a row for each bar");
    }
    printf("keyprobe: bars, size %ux%u\n", bar_cols, bar_rows);

    if (console_mode(CON_EVENTS) != 0) {
        return fail("no key-event mode");
    }
    printf("keyprobe: events\n");
    fflush(stdout);
    struct con_key_event event;
    for (;;) {
        if (next_event(&event) != 0) {
            return fail("reading events");
        }
        if (event.keycode == KEY_F1 + 9) {  // F10
            break;
        }
        printf("keyprobe: key %02x mods %04x char %u\n", event.keycode, event.modifiers,
               event.codepoint);
    }

    if (sig_alloc(SIG_BREAK) != SIG_BREAK || console_esc_break(1) != 0) {
        return fail("ESC as a break refused");
    }
    printf("keyprobe: ESC is a break\n");
    fflush(stdout);
    sig_wait(SIG_MASK(SIG_BREAK));
    printf("keyprobe: break\n");
    console_esc_break(0);
    printf("keyprobe: ESC is a key\n");
    fflush(stdout);
    do {
        if (next_event(&event) != 0) {
            return fail("reading events");
        }
    } while (event.keycode != KEY_ESCAPE);
    printf("keyprobe: ESC as a key\n");

    console_mode(CON_COOKED);
    console_bar(CON_BAR_TOP, NULL);
    console_bar(CON_BAR_BOTTOM, NULL);
    if (size(&bar_cols, &bar_rows) != 0 || bar_cols != cols || bar_rows != rows) {
        return fail("the grid did not get its rows back");
    }
    printf("keyprobe: done %ux%u\n", bar_cols, bar_rows);
    return 0;
}
