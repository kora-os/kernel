/* termdemo: exercise the screen terminal's xterm escape sequences -- colours
 * (16, 256 and 24-bit), text attributes, line drawing (DEC charset and UTF-8),
 * and cursor addressing / save / restore. A visual check, like gfxdemo.
 */
#include "libk/koraos.h"

static void out(const char *s) {
    fputs(s, stdout);
}

// ESC [ <a> ; <b> <final>, or with b < 0 just ESC [ <a> <final>.
static void csi2(unsigned a, int b, char final) {
    if (b >= 0) {
        printf("\x1b[%u;%d%c", a, b, final);
    } else {
        printf("\x1b[%u%c", a, final);
    }
}

static void bg256(unsigned i) {
    printf("\x1b[48;5;%um ", i);
}

static void bg_rgb(unsigned r, unsigned g, unsigned b) {
    printf("\x1b[48;2;%u;%u;%um ", r, g, b);
}

int main(void) {
    out("\x1b[2J\x1b[H");  // clear, home
    out("\x1b[1;4mKoraOS terminal demo\x1b[0m\n\n");

    out("16 colours:  ");
    for (unsigned i = 0; i < 8; i++) {
        csi2(30 + i, -1, 'm');
        out("#");
    }
    for (unsigned i = 0; i < 8; i++) {
        csi2(90 + i, -1, 'm');
        out("#");
    }
    out("\x1b[0m  ");
    for (unsigned i = 0; i < 8; i++) {
        csi2(40 + i, -1, 'm');
        out(" ");
    }
    for (unsigned i = 0; i < 8; i++) {
        csi2(100 + i, -1, 'm');
        out(" ");
    }
    out("\x1b[0m\n\n256 colours:\n");
    for (unsigned row = 0; row < 6; row++) {
        for (unsigned i = 0; i < 36; i++) {
            bg256(16 + row * 36 + i);
        }
        out("\x1b[0m\n");
    }
    for (unsigned i = 232; i < 256; i++) {
        bg256(i);
    }
    out("\x1b[0m\n\n24-bit colour:\n");
    for (unsigned i = 0; i < 64; i++) {
        bg_rgb(i * 4, 255 - i * 4, 128);
    }
    out("\x1b[0m\n\n");

    out("Attributes: \x1b[1mbold\x1b[0m \x1b[2mdim\x1b[0m \x1b[4munderline\x1b[0m "
        "\x1b[7mreverse\x1b[0m \x1b[9mstrike\x1b[0m "
        "\x1b[1;33;44mbold yellow on blue\x1b[0m\n\n");

    // Line drawing: DEC special graphics (ESC ( 0) and UTF-8 box characters.
    out("\x1b(0lqqqqqqqqwqqqqqqqqk\x1b(B  \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
        "\xe2\x94\x80\xe2\x94\x90\n");
    out("\x1b(0x\x1b(B  DEC   \x1b(0x\x1b(B charset\x1b(0x\x1b(B  \xe2\x94\x82utf8\xe2\x94\x82\n");
    out("\x1b(0mqqqqqqqqvqqqqqqqqj\x1b(B  \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
        "\xe2\x94\x80\xe2\x94\x98\n\n");

    // Cursor addressing: save the position, draw in the top-right corner,
    // come back.
    out("\x1b" "7");
    csi2(1, 100, 'H');
    out("\x1b[30;42m top-right via CUP \x1b[0m");
    csi2(2, 100, 'H');
    out("\x1b[1;36mrow 2, col 100\x1b[0m");
    out("\x1b" "8");

    out("Cursor is back after the saved position (ESC 7 / ESC 8).\n");
    out("Shift+PgUp / Shift+PgDn scroll back through history.\n");
    // stdout is line buffered and escape sequences carry no newline: a
    // program that draws with them ends its frame with a flush.
    fflush(stdout);
    return 0;
}
