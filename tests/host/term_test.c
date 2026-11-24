// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the terminal model and escape-sequence parser
// (user/libconsole/term.c).

#include "test.h"
#include "libconsole/term.h"

// --- The host the terminal runs on -------------------------------------------

// Memory comes from a static arena rather than the heap, so LeakSanitizer (on
// by default with ASan on Linux) has nothing to report: buffers a resize
// replaces are only counted. The arena is filled with junk first: the terminal must not rely on
// zeroed memory.
static uint8_t arena[16 * 1024 * 1024] __attribute__((aligned(16)));
static size_t arena_used;
static unsigned allocations;
static size_t arena_limit = sizeof(arena);

static void *host_alloc(void *ctx, size_t bytes) {
    CHECK(ctx == &arena, "the host context reaches alloc");
    bytes = (bytes + 15) & ~(size_t)15;
    if (arena_used + bytes > arena_limit) {
        return NULL;
    }
    void *p = arena + arena_used;
    for (size_t i = 0; i < bytes; i++) {
        arena[arena_used + i] = 0xA5;
    }
    arena_used += bytes;
    allocations++;
    return p;
}

// Status replies the terminal sends back, as if typed.
static char replies[256];
static int nreplies;

static void host_reply(void *ctx, const char *bytes, size_t length) {
    CHECK(ctx == &arena, "the host context reaches reply");
    for (size_t i = 0; i < length; i++) {
        if (nreplies < (int)sizeof(replies) - 1) {
            replies[nreplies++] = bytes[i];
            replies[nreplies] = '\0';
        }
    }
}

static const term_host_t host = {host_alloc, host_reply, arena, NULL};

// --- Helpers ------------------------------------------------------------------

static term_t t;

// A fresh terminal with the kernel's scrollback.
static void start(uint32_t cols, uint32_t rows) {
    CHECK(term_init(&t, cols, rows, TERM_SCROLLBACK_LINES, &host), "term_init %ux%u", cols, rows);
}

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
    start(10, 4);
    feed("abcdefghij");
    CHECK(t.col == 9 && t.wrap_pending, "pending wrap at the last column (col=%u)", t.col);
    feed("K");
    ROW(0, "abcdefghij");
    ROW(1, "K");
}

static void test_scrollback(void) {
    start(10, 4);
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
    start(8, 3);
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
    start(10, 5);
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
    start(10, 4);
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
    start(10, 4);
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
    start(20, 10);
    nreplies = 0;
    feed("\x1b[5;7H\x1b[6n");
    CHECK(strcmp(replies, "\x1b[5;7R") == 0, "CPR reply 'ESC%s'", replies + 1);
    nreplies = 0;
    feed("\x1b[18t");
    CHECK(strcmp(replies, "\x1b[8;10;20t") == 0, "size reply 'ESC%s'", replies + 1);
}

static void test_colours(void) {
    start(20, 4);
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
    start(20, 4);
    feed("\xc3\xa9x\xe2\x94\x80\x1b(0q\x1b(Bq");
    CHECK(cell(0, 0)->ch == '?', "U+00E9 shows as one '?'");
    CHECK(cell(0, 1)->ch == 'x', "then x");
    CHECK(cell(0, 2)->ch == TERM_GLYPH_HLINE, "U+2500 is a horizontal line");
    CHECK(cell(0, 3)->ch == TERM_GLYPH_HLINE, "DEC graphics q is a horizontal line");
    CHECK(cell(0, 4)->ch == 'q', "back to ASCII after ESC ( B");
}

static void test_modes_and_misc(void) {
    start(20, 6);
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

// What term_init asks of its host, and what it does when the host has little.
static void test_host(void) {
    // Two buffers: the main screen with its history, then the alternate screen.
    unsigned before = allocations;
    start(10, 4);
    CHECK(allocations - before == 2, "a terminal makes two allocations (%u)", allocations - before);
    CHECK(t.main_buf.total_lines == 4 + TERM_SCROLLBACK_LINES && t.alt_buf.total_lines == 4,
          "the main screen holds its history, the alternate one none");
    ROW(0, "");
    CHECK(cell(3, 9)->ch == ' ' && cell(3, 9)->bg == TERM_DEFAULT_BG && cell(3, 9)->attr == 0,
          "junk memory still makes a blank screen");

    CHECK(term_init(&t, 10, 4, 0, &host) && t.main_buf.total_lines == 4,
          "a terminal may have no history");
    feed("1\r\n2\r\n3\r\n4\r\n5");
    ROW(0, "2");
    term_scroll_view(&t, 10);
    ROW(0, "2");

    // Room for both screens but not for the history: no history.
    arena_limit = arena_used + 3 * 10 * 4 * sizeof(term_cell_t);
    CHECK(term_init(&t, 10, 4, 100, &host) && t.main_buf.total_lines == 4,
          "too little memory for history gives none");
    arena_limit = arena_used + 10 * 4 * sizeof(term_cell_t);
    CHECK(!term_init(&t, 10, 4, 100, &host), "too little memory for both screens fails");
    arena_limit = sizeof(arena);

    term_host_t silent = {host_alloc, NULL, arena, NULL};
    CHECK(term_init(&t, 10, 4, 0, &silent), "a host may take no replies");
    nreplies = 0;
    feed("\x1b[6n\x1b[c");
    CHECK(nreplies == 0, "status requests without a reply hook are dropped");
    term_host_t none = {NULL, NULL, NULL, NULL};
    CHECK(!term_init(&t, 10, 4, 0, &none), "a host with no memory gets no terminal");
    CHECK(!term_init(&t, 0, 4, 0, &host), "zero columns fail");
    CHECK(term_init(&t, TERM_MAX_COLS + 50, TERM_MAX_ROWS + 50, 0, &host) &&
          t.cols == TERM_MAX_COLS && t.rows == TERM_MAX_ROWS, "the grid is clamped to its maximum");
}

// Buffers given back by term_resize: the arena keeps them, as a host that
// never frees does; here they are only counted.
static void *freed[8];
static unsigned frees;

static void host_free(void *ctx, void *buffer) {
    CHECK(ctx == &arena, "the host context reaches free");
    freed[frees++ % 8] = buffer;
}

static void test_resize(void) {
    term_host_t freeing = {host_alloc, host_reply, arena, host_free};
    CHECK(term_init(&t, 10, 4, 5, &freeing), "a 10x4 terminal with 5 lines of scrollback");
    feed("1\r\n2\r\nabcdefgh");
    void *main_cells = t.main_buf.cells, *alt_cells = t.alt_buf.cells;
    frees = 0;
    unsigned before = allocations;
    CHECK(term_resize(&t, 10, 4) == 1 && allocations == before && frees == 0,
          "the same size: nothing to do");

    // Fewer rows: the cursor's line stays, the lines above it go to the
    // scrollback.
    CHECK(term_resize(&t, 10, 2) == 1 && t.cols == 10 && t.rows == 2, "10x2");
    ROW(0, "2");
    ROW(1, "abcdefgh");
    CHECK(t.row == 1 && t.col == 8 && t.main_buf.history == 1 && t.main_buf.total_lines == 7,
          "the cursor follows its line; one line of history, its capacity kept");
    CHECK(frees == 2 && freed[0] == main_cells && freed[1] == alt_cells,
          "the old buffers are given back");
    term_scroll_view(&t, 1);
    ROW(0, "1");
    term_scroll_view(&t, -1);

    // Narrower: lines cut; wider: padded with default blanks, whatever the pen.
    CHECK(term_resize(&t, 3, 2) == 1, "3x2");
    ROW(1, "abc");
    CHECK(t.col == 2, "the cursor's column clamped (%u)", t.col);
    feed("\x1b[41m");
    CHECK(term_resize(&t, 6, 5) == 1, "6x5");
    ROW(0, "2");
    ROW(1, "abc");
    ROW(2, "");
    CHECK(cell(1, 5)->ch == ' ' && cell(1, 5)->bg == TERM_DEFAULT_BG && cell(4, 0)->bg == TERM_DEFAULT_BG,
          "the padding is a default blank, not the pen's red");
    CHECK(t.row == 1 && t.main_buf.history == 1, "more rows take nothing back from the history");

    // Text below the cursor stays too.
    feed("\x1b[5;1HZ\x1b[1;1H");
    CHECK(term_resize(&t, 6, 3) == 1, "6x3");
    ROW(0, "");
    ROW(1, "");
    ROW(2, "Z");
    CHECK(t.row == 0 && t.main_buf.history == 3, "the cursor at the top; two more lines of history");
    term_scroll_view(&t, 3);
    ROW(0, "1");
    ROW(1, "2");
    ROW(2, "abc");
    term_scroll_view(&t, -3);

    // The margins and the view are reset.
    feed("\x1b[2;3r");
    term_scroll_view(&t, 1);
    CHECK(term_resize(&t, 6, 4) == 1 && t.margin_top == 0 && t.margin_bottom == 3 &&
              t.view_offset == 0, "margins and the view reset");

    // The alternate screen is resized too, and the main one is there after it.
    feed("\x1b[?1049h\x1b[HALT");
    CHECK(term_resize(&t, 8, 2) == 1 && t.buf == &t.alt_buf, "resized on the alternate screen");
    ROW(0, "ALT");
    feed("\x1b[?1049l");
    CHECK(t.buf == &t.main_buf, "back on the main screen");
    ROW(1, "Z");

    // Short of memory: as it was.
    arena_limit = arena_used;
    frees = 0;
    CHECK(term_resize(&t, 20, 10) == 0 && t.cols == 8 && t.rows == 2 && frees == 0,
          "with no memory for the new buffers, the terminal stays as it was");
    ROW(1, "Z");
    arena_limit = arena_used + (size_t)20 * 10 * sizeof(term_cell_t) * 2 + 64;
    CHECK(term_resize(&t, 20, 10) == 1 && t.main_buf.total_lines == 10 &&
              t.main_buf.history == 0, "with memory for the screens only, no scrollback");
    arena_limit = sizeof(arena);
    CHECK(term_resize(&t, 0, 5) == 0 && t.cols == 20, "no columns: refused");
    term_host_t no_alloc = {NULL, NULL, arena, NULL};
    t.host = no_alloc;
    CHECK(term_resize(&t, 5, 5) == 0, "no allocator: refused");
}

TEST_MAIN(test_host, test_autowrap, test_scrollback, test_history_ring_wraps, test_scroll_region,
          test_insert_delete_erase, test_alternate_screen, test_status_replies,
          test_colours, test_utf8_and_line_drawing, test_modes_and_misc, test_resize)
