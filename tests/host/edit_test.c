// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the console's line editor (user/libconsole/edit.c) and its
// key translation (keys.c). The editor's echo goes into a real terminal of
// the library (term.c), so the tests check what the screen shows and where
// the cursor is, wrapped lines and all, not just the bytes.

#include "test.h"
#include "libconsole/edit.h"
#include "libconsole/keys.h"
#include "libconsole/term.h"

char *strstr(const char *haystack, const char *needle);
int memcmp(const void *a, const void *b, size_t n);

static uint8_t arena[4 * 1024 * 1024] __attribute__((aligned(16)));
static size_t arena_used;

static void *host_alloc(void *ctx, size_t bytes) {
    (void)ctx;
    bytes = (bytes + 15) & ~(size_t)15;
    if (arena_used + bytes > sizeof(arena)) {
        return NULL;
    }
    void *p = arena + arena_used;
    arena_used += bytes;
    return p;
}

static const term_host_t host = {host_alloc, NULL, NULL};
static term_t t;
static con_edit_t e;

static void echo(void *ctx, const char *bytes, size_t length) {
    CHECK(ctx == &t, "the echo's context");
    for (size_t i = 0; i < length; i++) {
        term_putc(&t, bytes[i]);
    }
}

static void feed_term(const char *s) {
    while (*s) {
        term_putc(&t, *s++);
    }
}

// A console of `cols` by 6 rows with `prompt` printed, and a fresh editor.
static void start(uint32_t cols, const char *prompt) {
    arena_used = 0;
    CHECK(term_init(&t, cols, 6, 0, &host), "a terminal");
    con_edit_init(&e, echo, &t);
    feed_term(prompt);
}

// Typed bytes, as one key each (a key's bytes come in one call).
static void type(const char *s) {
    while (*s) {
        con_edit_place(&e, t.cols, t.col);
        con_edit_input(&e, s, 1);
        s++;
    }
}

static void key(unsigned keycode, unsigned modifiers) {
    con_key_t k;
    con_key_bytes(CON_KEY_DOWN_EVENT, modifiers, keycode, 0, &k);
    con_edit_place(&e, t.cols, t.col);
    con_edit_input(&e, k.bytes, k.length);
}

static void ctrl(char letter) {
    con_key_t k;
    con_key_bytes(CON_KEY_DOWN_EVENT, CON_MOD_CTRL, 0x04 + (unsigned)(letter - 'a'),
                  (uint32_t)letter, &k);
    con_edit_place(&e, t.cols, t.col);
    con_edit_input(&e, k.bytes, k.length);
}

// The text of screen row `row`, trailing blanks dropped.
static const char *row_text(uint32_t row) {
    static char text[TERM_MAX_COLS + 1];
    uint32_t n = 0;
    for (uint32_t col = 0; col < t.cols; col++) {
        uint8_t ch = term_view_cell(&t, row, col)->ch;
        text[n++] = ch ? (char)ch : ' ';
    }
    while (n > 0 && text[n - 1] == ' ') {
        n--;
    }
    text[n] = '\0';
    return text;
}

static int cursor_at(uint32_t row, uint32_t col) {
    return t.row == row && t.col == col && !t.wrap_pending;
}

static char got[CON_READY_MAX + 1];

// One read of up to `n` bytes, as text ("" when nothing is ready, "<end>" for
// an end of input).
static const char *read_n(size_t n) {
    bool end;
    size_t k = con_edit_read(&e, got, n, &end);
    if (end) {
        return "<end>";
    }
    got[k] = '\0';
    return got;
}

#define SHOWS(row, text) CHECK(strcmp(row_text(row), text) == 0, "row %u: \"%s\", want \"%s\"", \
                               (unsigned)(row), row_text(row), text)

static void test_typing(void) {
    start(40, "$ ");
    type("echo hi");
    SHOWS(0, "$ echo hi");
    CHECK(cursor_at(0, 9), "the cursor follows the typing");
    CHECK(strcmp(read_n(64), "") == 0, "nothing is read before Enter");
    key(CON_KEY_ENTER, 0);
    CHECK(cursor_at(1, 0), "Enter moves to the next line");
    CHECK(strcmp(read_n(64), "echo hi\n") == 0, "a read gets the line and its newline");
    CHECK(strcmp(read_n(64), "") == 0, "once");

    type("one\n");
    type("two\n");
    CHECK(strcmp(read_n(64), "one\n") == 0 && strcmp(read_n(64), "two\n") == 0,
          "typed-ahead lines are read one per read, in order");
    type("abcdef\n");
    CHECK(strcmp(read_n(4), "abcd") == 0 && strcmp(read_n(4), "ef\n") == 0,
          "a short read takes the rest next time");
    type("\x7f\x7f");
    CHECK(strcmp(read_n(4), "") == 0, "backspace on an empty line does nothing");
}

static void test_editing(void) {
    start(40, "> ");
    type("abc");
    key(CON_KEY_LEFT, 0);
    key(CON_KEY_LEFT, 0);
    CHECK(cursor_at(0, 3), "left moves back");
    type("X");
    SHOWS(0, "> aXbc");
    CHECK(cursor_at(0, 4), "inserting in the middle keeps the cursor after it");
    key(CON_KEY_BACKSPACE, 0);
    SHOWS(0, "> abc");
    CHECK(cursor_at(0, 3), "backspace in the middle");
    key(CON_KEY_DELETE, 0);
    SHOWS(0, "> ac");
    CHECK(cursor_at(0, 3), "delete takes the character under the cursor");
    key(CON_KEY_HOME, 0);
    CHECK(cursor_at(0, 2), "Home");
    key(CON_KEY_LEFT, 0);
    CHECK(cursor_at(0, 2), "left stops at the start");
    key(CON_KEY_END, 0);
    CHECK(cursor_at(0, 4), "End");
    key(CON_KEY_RIGHT, 0);
    CHECK(cursor_at(0, 4), "right stops at the end");
    ctrl('a');
    type("[");
    ctrl('e');
    type("]");
    SHOWS(0, "> [ac]");
    key(CON_KEY_LEFT, 0);
    key(CON_KEY_LEFT, 0);
    ctrl('k');
    SHOWS(0, "> [a");
    ctrl('u');
    SHOWS(0, ">");
    CHECK(cursor_at(0, 2), "Ctrl+U clears to the start, Ctrl+K to the end");
    type("ok");
    key(CON_KEY_TAB, 0);
    key(CON_KEY_ESCAPE, 0);
    type("!");
    key(CON_KEY_INSERT, 0);
    key(CON_KEY_PAGE_UP, 0);
    key(CON_KEY_ENTER, 0);
    CHECK(strcmp(read_n(64), "ok!\n") == 0,
          "Tab, Escape (alone, then a key), Insert and PageUp add nothing");
}

static void test_wrapping(void) {
    start(10, "$$$ ");  // 6 cells left on the first row
    type("abcdefghijklmnop");  // to row 2, column 0 + 16 - 6 - 10
    SHOWS(0, "$$$ abcdef");
    SHOWS(1, "ghijklmnop");
    CHECK(cursor_at(2, 0), "a line ending at the last column leaves the cursor below");
    ctrl('a');
    CHECK(cursor_at(0, 4), "Home goes back up two rows");
    type("12");
    SHOWS(0, "$$$ 12abcd");
    SHOWS(1, "efghijklmn");
    SHOWS(2, "op");
    CHECK(cursor_at(0, 6), "inserting reflows the rows below");
    key(CON_KEY_END, 0);
    CHECK(cursor_at(2, 2), "End");
    for (int i = 0; i < 7; i++) {
        key(CON_KEY_BACKSPACE, 0);
    }
    SHOWS(1, "efghi");
    SHOWS(2, "");
    CHECK(cursor_at(1, 5), "deleting across a row clears what was left");
    key(CON_KEY_ENTER, 0);
    CHECK(strcmp(read_n(64), "12abcdefghi\n") == 0 && cursor_at(2, 0), "the wrapped line");
}

static void test_history(void) {
    start(40, "");
    type("first\n");
    type("second\n");
    type("second\n");
    read_n(64), read_n(64), read_n(64);
    CHECK(e.history_count == 2, "a line entered twice is kept once (%u)", (unsigned)e.history_count);
    type("dra");
    key(CON_KEY_UP, 0);
    SHOWS(3, "second");
    key(CON_KEY_UP, 0);
    SHOWS(3, "first");
    CHECK(cursor_at(3, 5), "the cursor at the recalled line's end");
    key(CON_KEY_UP, 0);
    SHOWS(3, "first");
    key(CON_KEY_DOWN, 0);
    key(CON_KEY_DOWN, 0);
    SHOWS(3, "dra");
    key(CON_KEY_DOWN, 0);
    SHOWS(3, "dra");
    type("ft");
    key(CON_KEY_UP, 0);
    key(CON_KEY_LEFT, 0);
    type("X");
    key(CON_KEY_ENTER, 0);
    CHECK(strcmp(read_n(64), "seconXd\n") == 0, "a recalled line can be edited and entered");
    key(CON_KEY_UP, 0);
    key(CON_KEY_ENTER, 0);
    CHECK(strcmp(read_n(64), "seconXd\n") == 0, "and is the newest entry");

    for (int i = 0; i < CON_HISTORY + 5; i++) {
        char line[8] = {'l', (char)('a' + i % 26), (char)('a' + i / 26), '\n', 0};
        type(line);
        read_n(64);
    }
    CHECK(e.history_count == CON_HISTORY, "the history keeps %d lines", CON_HISTORY);
    CHECK(strcmp(e.history[0], "lfa") == 0, "the oldest dropped first (%s)", e.history[0]);
}

static void test_end_and_break(void) {
    start(40, "");
    type("a");
    ctrl('d');
    CHECK(strcmp(read_n(64), "") == 0, "Ctrl+D in a line does nothing");
    key(CON_KEY_ENTER, 0);
    ctrl('d');
    type("b\n");
    CHECK(strcmp(read_n(64), "a\n") == 0 && strcmp(read_n(64), "<end>") == 0 &&
          strcmp(read_n(64), "b\n") == 0 && strcmp(read_n(64), "") == 0,
          "an end of input comes in its place among the lines");

    type("typed ahead\n");
    type("half");
    CHECK(!con_edit_break(&e), "no break yet");
    ctrl('c');
    CHECK(con_edit_break(&e) && !con_edit_break(&e), "Ctrl+C is a break, reported once");
    CHECK(strstr(row_text(t.row - 1), "half^C") != NULL && t.col == 0, "^C shown, a new line");
    CHECK(strcmp(read_n(64), "") == 0, "the typed-ahead input goes");
    type("next\n");
    CHECK(strcmp(read_n(64), "next\n") == 0, "a new line afterwards");
}

static void test_raw(void) {
    start(40, "");
    type("cook");
    con_edit_mode(&e, CON_RAW);
    type("ab");
    key(CON_KEY_UP, 0);
    key(CON_KEY_ENTER, 0);
    key(CON_KEY_BACKSPACE, 0);
    CHECK(strcmp(row_text(0), "cook") == 0, "raw mode echoes nothing");
    CHECK(strcmp(read_n(64), "ab\x1b[A\n\x7f") == 0, "raw bytes as typed, not by line");
    ctrl('c');
    CHECK(con_edit_break(&e) && strcmp(read_n(64), "") == 0 && strcmp(row_text(0), "cook") == 0,
          "Ctrl+C is still a break, silently");
    ctrl('d');
    CHECK(strcmp(read_n(64), "\x04") == 0, "Ctrl+D is a byte");
    con_edit_mode(&e, CON_COOKED);
    type("ed\n");
    CHECK(strcmp(read_n(64), "cooked\n") == 0, "the line begun before raw mode ends in cooked");
}

static void test_utf8(void) {
    start(40, "");
    con_key_t k;
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, 0x34, 0xE9, &k);  // é
    type("a");
    con_edit_input(&e, k.bytes, k.length);
    type("b");
    CHECK(cursor_at(0, 3), "a two-byte character takes one cell");
    key(CON_KEY_LEFT, 0);
    key(CON_KEY_LEFT, 0);
    CHECK(cursor_at(0, 1), "and one step");
    key(CON_KEY_DELETE, 0);
    key(CON_KEY_ENTER, 0);
    CHECK(strcmp(read_n(64), "ab\n") == 0, "delete takes both bytes");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, 0x08, 0x20AC, &k);  // €
    con_edit_input(&e, k.bytes, k.length);
    key(CON_KEY_BACKSPACE, 0);
    con_edit_input(&e, "\xC3x", 2);  // a broken character
    key(CON_KEY_ENTER, 0);
    CHECK(strcmp(read_n(64), "x\n") == 0, "backspace takes three bytes; a broken one is dropped");
}

static void test_keys(void) {
    con_key_t k;
    con_key_bytes(2, 0, CON_KEY_ENTER, 0, &k);
    CHECK(k.length == 0, "a key up sends nothing");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, 0xE1, 0, &k);
    CHECK(k.length == 0, "a modifier alone sends nothing");
    con_key_bytes(CON_KEY_DOWN_EVENT, CON_MOD_SHIFT, CON_KEY_PAGE_UP, 0, &k);
    CHECK(k.length == 0 && k.scroll == 1, "Shift+PageUp scrolls back");
    con_key_bytes(CON_KEY_DOWN_EVENT, CON_MOD_SHIFT, CON_KEY_PAGE_DOWN, 0, &k);
    CHECK(k.length == 0 && k.scroll == -1, "Shift+PageDown forward");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, CON_KEY_PAGE_DOWN, 0, &k);
    CHECK(k.length == 4 && memcmp(k.bytes, "\x1b[6~", 4) == 0, "PageDown");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, CON_KEY_DELETE, 0, &k);
    CHECK(k.length == 4 && memcmp(k.bytes, "\x1b[3~", 4) == 0, "Delete");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, CON_KEY_LEFT, 0, &k);
    CHECK(k.length == 3 && memcmp(k.bytes, "\x1b[D", 3) == 0, "Left");
    con_key_bytes(CON_KEY_DOWN_EVENT, CON_MOD_CTRL, 0x06, 'c', &k);
    CHECK(k.length == 1 && k.bytes[0] == 3, "Ctrl+C");
    con_key_bytes(CON_KEY_DOWN_EVENT, CON_MOD_CTRL, 0x2C, ' ', &k);
    CHECK(k.length == 1 && k.bytes[0] == 0, "Ctrl+Space is NUL");
    con_key_bytes(CON_KEY_DOWN_EVENT, CON_MOD_CTRL, 0x20, '3', &k);
    CHECK(k.length == 0, "Ctrl with a key that has no control code sends nothing");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0x0004, 0x04, 'a', &k);
    CHECK(k.length == 1 && k.bytes[0] == 'a', "Alt is ignored");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, 0x1F, 0x1F600, &k);
    CHECK(k.length == 4 && memcmp(k.bytes, "\xF0\x9F\x98\x80", 4) == 0, "four-byte UTF-8");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, 0x1F, 0xD800, &k);
    CHECK(k.length == 0, "a surrogate sends nothing");
    con_key_bytes(CON_KEY_DOWN_EVENT, 0, CON_KEY_KP_ENTER, 0, &k);
    CHECK(k.length == 1 && k.bytes[0] == '\n', "keypad Enter");
}

TEST_MAIN(test_typing, test_editing, test_wrapping, test_history, test_end_and_break, test_raw,
          test_utf8, test_keys)
