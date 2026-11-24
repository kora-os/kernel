// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 fixture: a program in a console (user/libscreen/console.c, newcon), for
// the smoke test, which types to it. It reports on raw fd 1, the kernel
// console (the serial line under QEMU), since its stdout is the console.
//   conprobe        reports "conprobe: isatty 1 1 1", then each line it
//                   reads in cooked mode ("conprobe: line <text>") until the
//                   end of input ("conprobe: end of input"), then switches
//                   to raw mode and reports the bytes of the next 4 keys
//                   ("conprobe: raw 71 1b 5b 41 ..."), and "conprobe: done"
//   conprobe hang   reports "conprobe: hanging" and sleeps until killed
//   conprobe size   asks its console for its size in characters (CSI 18 t,
//                   answered in raw mode as typed input) and reports
//                   "conprobe: size COLSxROWS" ("conprobe: no size" if the
//                   answer is not one)
#include "../../user/libk/koraos.h"

static void report(const char *text) {
    write(1, text, strlen(text));
}

int main(int argc, char **argv) {
    char text[600];
    if (argc > 1 && !strcmp(argv[1], "hang")) {
        report("conprobe: hanging\n");
        for (;;) {
            msleep(1000);
        }
    }
    if (argc > 1 && !strcmp(argv[1], "size")) {
        console_mode(CON_RAW);
        fputs("\x1b[18t", stdout);
        fflush(stdout);
        char answer[32];  // ESC [ 8 ; rows ; cols t
        size_t n = 0;
        int c;
        while (n < sizeof(answer) - 1 && (c = getchar()) != EOF) {
            answer[n++] = (char)c;
            if (c == 't') {
                break;
            }
        }
        answer[n] = '\0';
        console_mode(CON_COOKED);
        unsigned rows, cols;
        if (sscanf(answer, "\x1b[8;%u;%ut", &rows, &cols) == 2) {
            snprintf(text, sizeof(text), "conprobe: size %ux%u\n", cols, rows);
        } else {
            snprintf(text, sizeof(text), "conprobe: no size\n");
        }
        report(text);
        return 0;
    }
    snprintf(text, sizeof(text), "conprobe: isatty %d %d %d\n", isatty(0), isatty(1), isatty(2));
    report(text);
    printf("conprobe: type lines, then Ctrl+D\n");
    fflush(stdout);
    char line[512];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        snprintf(text, sizeof(text), "conprobe: line <%s>\n", line);
        report(text);
    }
    clearerr(stdin);
    report("conprobe: end of input\n");

    if (console_mode(CON_RAW) != 0) {
        report("conprobe: FAIL raw mode\n");
        return 1;
    }
    printf("conprobe: raw mode, 4 keys\n");
    fflush(stdout);
    char raw[64];
    size_t n = 0;
    int keys = 0;
    while (keys < 4 && n < sizeof(raw)) {
        int c = getc(stdin);
        if (c == EOF) {
            break;
        }
        raw[n++] = (char)c;
        // A key is one byte, or an escape sequence to its final byte.
        if (c == 0x1b) {
            continue;
        }
        if (n >= 2 && raw[n - 2] == 0x1b && c == '[') {
            continue;
        }
        if (n >= 3 && raw[n - 3] == 0x1b && raw[n - 2] == '[' && (c < 0x40 || c > 0x7e)) {
            continue;
        }
        keys++;
    }
    console_mode(CON_COOKED);
    int length = snprintf(text, sizeof(text), "conprobe: raw");
    for (size_t i = 0; i < n; i++) {
        length += snprintf(text + length, sizeof(text) - (size_t)length, " %02x",
                           (unsigned char)raw[i]);
    }
    snprintf(text + length, sizeof(text) - (size_t)length, "\n");
    report(text);
    report("conprobe: done\n");
    return 0;
}
