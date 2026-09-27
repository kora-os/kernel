// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the terminal model and escape-sequence parser
// (src/video/term.c).

#include "test.h"
#include "video/term.h"

// --- Stubs for the kernel services term.c uses -------------------------------

void *frame_alloc_pages(size_t count) {
    return calloc(count, 4096);  // leaked: each test program is short-lived
}

// Status replies the terminal sends back as keyboard input.
static char replies[256];
static int nreplies;

void tty_input_push(char c) {
    if (nreplies < (int)sizeof(replies) - 1) {
        replies[nreplies++] = c;
        replies[nreplies] = '\0';
    }
}

// --- Helpers ------------------------------------------------------------------

static term_t t;

static void feed(const char *s) {
    while (*s) {
        term_putc(&t, *s++);
    }
}

// Text of a screen row as seen through the view, trailing blanks trimmed.
// Glyph codes >= 0x80 show as '*'.
static const char *row_text(unsigned row) {
    static char buf[TERM_MAX_COLS + 1];
    unsigned n = 0;
    for (unsigned c = 0; c < t.cols; c++) {
        uint8_t ch = term_view_cell(&t, row, c)->ch;
        buf[n++] = ch >= 0x80 ? '*' : (char)ch;
    }
    while (n > 0 && buf[n - 1] == ' ') {
        n--;
    }
    buf[n] = '\0';
    return buf;
}

#define ROW(r, want) \
    CHECK(strcmp(row_text(r), want) == 0, "row %d = '%s', want '%s'", r, row_text(r), want)

static const term_cell_t *cell(unsigned row, unsigned col) {
    return term_view_cell(&t, row, col);
}

// --- Tests --------------------------------------------------------------------

static void test_autowrap(void) {
    term_init(&t, 10, 4);
    feed("abcdefghij");
    CHECK(t.col == 9 && t.wrap_pending, "pending wrap at the last column (col=%u)", t.col);
    feed("K");
    ROW(0, "abcdefghij");
    ROW(1, "K");
}

static void test_scrollback(void) {
    term_init(&t, 10, 4);
    feed("L1\r\nL2\r\nL3\r\nL4\r\nL5\r\nL6\r\n");
    ROW(0, "L4");
    ROW(1, "L5");
    ROW(2, "L6");
    ROW(3, "");
    CHECK(t.main_buf.history == 3, "history=%u", t.main_buf.history);

    term_scroll_view(&t, 2);
    ROW(0, "L2");
    ROW(3, "L5");
    CHECK(!term_cursor_shown(&t), "cursor hidden while scrolled back");

    term_scroll_view(&t, 100);
    CHECK(t.view_offset == 3, "view clamped to the history (%u)", t.view_offset);
    ROW(0, "L1");

    feed("x");
    CHECK(t.view_offset == 0, "output snaps the view back");
    ROW(3, "x");
}

static void test_history_ring_wraps(void) {
    term_init(&t, 8, 3);
    for (int i = 0; i < TERM_SCROLLBACK_LINES + 50; i++) {
        feed("row\r\n");
    }
    feed("end");
    CHECK(t.main_buf.history == TERM_SCROLLBACK_LINES, "history full (%u)", t.main_buf.history);
    ROW(2, "end");
    term_scroll_view(&t, TERM_SCROLLBACK_LINES);
    ROW(0, "row");
}

static void test_scroll_region(void) {
    term_init(&t, 10, 5);
    feed("A\r\nB\r\nC\r\nD\r\nE");
    feed("\x1b[2;4r");  // region rows 2..4
    CHECK(t.row == 0 && t.col == 0, "DECSTBM homes the cursor");

    feed("\x1b[4;1H\n");  // LF at the bottom margin
    ROW(0, "A");
    ROW(1, "C");
    ROW(2, "D");
    ROW(3, "");
    ROW(4, "E");
    CHECK(t.main_buf.history == 0, "a region scroll adds no history");

    feed("\x1b[2;1H\x1bM");  // RI at the top margin
    ROW(1, "");
    ROW(2, "C");
    ROW(3, "D");
    ROW(4, "E");
}

static void test_insert_delete_erase(void) {
    term_init(&t, 10, 4);
    feed("11\r\n22\r\n33\r\n44");
    feed("\x1b[2;1H\x1b[L");  // IL at row 2
    ROW(0, "11");
    ROW(1, "");
    ROW(2, "22");
    ROW(3, "33");
    feed("\x1b[M");  // DL
    ROW(1, "22");
    ROW(3, "");

    feed("\x1b[1;1Habcdef\x1b[1;3H\x1b[2@");  // ICH
    ROW(0, "ab  cdef");
    feed("\x1b[3P");  // DCH
    ROW(0, "abdef");
    feed("\x1b[1;2H\x1b[2X");  // ECH
    ROW(0, "a  ef");
    feed("\x1b[1;3H\x1b[K");  // EL to the end of the line
    ROW(0, "a");
    feed("\x1b[2;1H\x1b[J");  // ED below
    ROW(0, "a");
    ROW(1, "");
    ROW(2, "");
}

static void test_alternate_screen(void) {
    term_init(&t, 10, 4);
    feed("main\r\nsecond");
    unsigned col = t.col, row = t.row;
    feed("\x1b[?1049h");
    ROW(0, "");
    feed("alt text");
    feed("\x1b[?1049l");
    ROW(0, "main");
    ROW(1, "second");
    CHECK(t.col == col && t.row == row, "cursor restored after 1049l");
}

static void test_status_replies(void) {
    term_init(&t, 20, 10);
    nreplies = 0;
    feed("\x1b[5;7H\x1b[6n");
    CHECK(strcmp(replies, "\x1b[5;7R") == 0, "CPR reply 'ESC%s'", replies + 1);
    nreplies = 0;
    feed("\x1b[18t");
    CHECK(strcmp(replies, "\x1b[8;10;20t") == 0, "size reply 'ESC%s'", replies + 1);
}

static void test_colours(void) {
    term_init(&t, 20, 4);
    feed("\x1b[38;5;196ma\x1b[38;2;1;2;3mb\x1b[38:2::4:5:6mc"
         "\x1b[0;1;31md\x1b[0;7;44me\x1b[0m");
    CHECK(cell(0, 0)->fg == 0xff0000, "256-colour fg %06x", cell(0, 0)->fg);
    CHECK(cell(0, 1)->fg == 0x010203, "24-bit fg %06x", cell(0, 1)->fg);
    CHECK(cell(0, 2)->fg == 0x040506, "24-bit fg, colon form %06x", cell(0, 2)->fg);
    CHECK(cell(0, 3)->fg == 0xff0000 && (cell(0, 3)->attr & TERM_ATTR_BOLD),
          "bold red is bright red %06x", cell(0, 3)->fg);
    CHECK(cell(0, 4)->bg == 0x0000ee && (cell(0, 4)->attr & TERM_ATTR_REVERSE),
          "reverse attribute and blue bg");
    feed("\x1b[41m\x1b[2K");
    CHECK(cell(0, 5)->bg == 0xcd0000, "erase uses the current bg (bce)");
}

static void test_utf8_and_line_drawing(void) {
    term_init(&t, 20, 4);
    feed("\xc3\xa9x\xe2\x94\x80\x1b(0q\x1b(Bq");
    CHECK(cell(0, 0)->ch == '?', "U+00E9 shows as one '?'");
    CHECK(cell(0, 1)->ch == 'x', "then x");
    CHECK(cell(0, 2)->ch == TERM_GLYPH_HLINE, "U+2500 is a horizontal line");
    CHECK(cell(0, 3)->ch == TERM_GLYPH_HLINE, "DEC graphics q is a horizontal line");
    CHECK(cell(0, 4)->ch == 'q', "back to ASCII after ESC ( B");
}

static void test_modes_and_misc(void) {
    term_init(&t, 20, 6);
    feed("\tX");
    CHECK(cell(0, 8)->ch == 'X', "tab to column 8");

    feed("\x1b[2;4r\x1b[?6h\x1b[1;1HO");
    CHECK(t.row == 1 && cell(1, 0)->ch == 'O', "origin mode addresses the region");
    feed("\x1b[?6l\x1b[r");

    feed("\x1b[4 q");
    CHECK(t.cursor_style == TERM_CURSOR_UNDERLINE, "DECSCUSR 4: underline");
    feed("\x1b[0 q");
    CHECK(t.cursor_style == TERM_DEFAULT_CURSOR, "DECSCUSR 0: default");
    feed("\x1b[?25l");
    CHECK(!term_cursor_shown(&t), "DECTCEM hides the cursor");

    feed("\x1b[?25h\x1b]0;window title\x07Z");
    CHECK(cell(t.row, t.col - 1)->ch == 'Z', "OSC swallowed, Z printed");

    feed("\x1b[1;1H-\x1b[4b");  // REP x4; the tab test left X at column 8
    ROW(0, "-----   X");

    for (int i = 0; i < 10; i++) {
        feed("\r\n");
    }
    CHECK(t.main_buf.history > 0, "history before ED 3");
    feed("\x1b[3J");
    CHECK(t.main_buf.history == 0, "ED 3 clears the history");
}

TEST_MAIN(test_autowrap, test_scrollback, test_history_ring_wraps, test_scroll_region,
          test_insert_delete_erase, test_alternate_screen, test_status_replies,
          test_colours, test_utf8_and_line_drawing, test_modes_and_misc)
