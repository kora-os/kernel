// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the console library's fonts (user/libconsole/font.c): the
// built-in font's bitmaps, and PSF2 fonts made here in memory: their size,
// the glyph each cell code gets through the Unicode table (several code
// points to a glyph, sequences skipped, UTF-8 of every length, the map
// sorted with the first glyph for each code point), the fallbacks,
// fonts without a table, and every malformed font refused. Then Spleen
// 8x16, converted as the build converts it (spleen_8x16_psf.h, made by
// tests/run-host-tests.sh).

#include "test.h"
#include "libconsole/font.h"
#include "libconsole/font8x8.h"
#include "spleen_8x16_psf.h"

// A 12x3 font of 5 glyphs: 2 bytes a row, 6 a glyph. Glyph n's first byte
// is 0x10 + n, so the tests can tell them apart.
#define WIDTH 12
#define HEIGHT 3
#define GLYPHS 5
#define GLYPH_BYTES 6

static uint8_t font_data[256];
static size_t font_size;

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// A PSF2 font: the header, the glyphs, then `table` (`table_size` bytes) if
// there is one.
static void make(uint32_t flags, const uint8_t *table, size_t table_size) {
    memset(font_data, 0, sizeof(font_data));
    put32(font_data, 0x864AB572u);
    put32(font_data + 4, 0);
    put32(font_data + 8, 32);
    put32(font_data + 12, flags);
    put32(font_data + 16, GLYPHS);
    put32(font_data + 20, GLYPH_BYTES);
    put32(font_data + 24, HEIGHT);
    put32(font_data + 28, WIDTH);
    for (int g = 0; g < GLYPHS; g++) {
        font_data[32 + g * GLYPH_BYTES] = (uint8_t)(0x10 + g);
    }
    memcpy(font_data + 32 + GLYPHS * GLYPH_BYTES, table, table_size);
    font_size = 32 + GLYPHS * GLYPH_BYTES + table_size;
}

// Glyph 0 is '?'; 1 is 'A' and 'a'; 2 the light horizontal line (3 bytes of
// UTF-8) and a sequence starting with 'e'; 3 is U+00E9 and a sequence ('e'
// and a combining acute); 4 the middle dot (2 bytes), a 4-byte code point
// and 'A' again (glyph 1 had it first). Not in code point order.
static const uint8_t table[] = {
    '?', 0xFF,
    'A', 'a', 0xFF,
    0xE2, 0x94, 0x80, 0xFE, 'e', 0xCC, 0x81, 0xFF,
    0xC3, 0xA9, 0xFE, 'e', 0xCC, 0x81, 0xFF,
    0xC2, 0xB7, 0xF0, 0x9F, 0x98, 0x80, 'A', 0xFF,
};
#define TABLE_ENTRIES 8  // its code points, sequences aside

static term_font_entry_t map[64];

// term_font_psf2 with room for the map term_font_psf2_entries asks for.
static int load(term_font_t *f, const void *data, size_t size) {
    long entries = term_font_psf2_entries(data, size);
    if (entries < 0) {
        return -1;
    }
    CHECK(entries <= 64 || data != font_data, "the test's map is large enough");
    static term_font_entry_t big[2048];
    return term_font_psf2(f, data, size, entries <= 64 ? map : big, (size_t)entries);
}

static uint8_t first_byte(const term_font_t *f, uint32_t cp) {
    return term_font_bitmap(f, cp)[0];
}

static void test_builtin(void) {
    const term_font_t *f = &term_font_builtin;
    CHECK(f->width == 8 && f->height == 8 && f->row_bytes == 1 && f->glyph_bytes == 8,
          "the built-in font is 8x8");
    bool same = true;
    for (int i = 0; i < 8; i++) {
        same = same && term_font_bitmap(f, 'A')[i] == font8x8_basic['A'][i];
    }
    CHECK(same && term_font_bitmap(f, 0x2500) == term_glyph(TERM_GLYPH_HLINE) &&
              term_font_bitmap(f, 0x2501) == term_glyph(TERM_GLYPH_HLINE),
          "its ASCII from font8x8, its symbols from term_glyph(), a heavy line as a light one");
    for (int i = 0; i < 8; i++) {
        same = same && term_font_bitmap(f, 0xE9)[i] == font8x8_basic['?'][i] &&
               term_font_bitmap(f, 0xA0)[i] == font8x8_basic[' '][i];
    }
    CHECK(same, "U+00E9 is a '?', a no-break space a space");
}

static void test_psf2(void) {
    make(1, table, sizeof(table));
    term_font_t f;
    CHECK(term_font_psf2_entries(font_data, font_size) == TABLE_ENTRIES,
          "its map needs an entry for each code point, sequences aside (%ld)",
          term_font_psf2_entries(font_data, font_size));
    CHECK(term_font_psf2(&f, font_data, font_size, map, TABLE_ENTRIES - 1) == -1,
          "too little room for the map is refused");
    CHECK(term_font_psf2(&f, font_data, font_size, map, TABLE_ENTRIES) == 0,
          "a PSF2 font with a Unicode table");
    CHECK(f.map == map && f.map_count == TABLE_ENTRIES - 1, "its map: 'A' once (%u)", f.map_count);
    bool sorted = true;
    for (uint32_t i = 1; i < f.map_count; i++) {
        sorted = sorted && f.map[i - 1].cp < f.map[i].cp;
    }
    CHECK(sorted, "sorted by code point");
    CHECK(f.width == WIDTH && f.height == HEIGHT && f.row_bytes == 2 &&
              f.glyph_bytes == GLYPH_BYTES && f.glyph_count == GLYPHS &&
              f.glyphs == font_data + 32,
          "its size, and its glyphs read where they are");
    CHECK(first_byte(&f, '?') == 0x10 && first_byte(&f, 'A') == 0x11 && first_byte(&f, 'a') == 0x11,
          "a glyph for each of its code points; 'A' the first glyph that names it");
    CHECK(first_byte(&f, 0x2500) == 0x12 && first_byte(&f, 0xB7) == 0x14 &&
              first_byte(&f, 0xE9) == 0x13 && first_byte(&f, 0x1F600) == 0x14,
          "code points of 3, 2 and 4 bytes of UTF-8");
    CHECK(first_byte(&f, 'e') == 0x10, "a sequence does not name a glyph for its first code point");
    CHECK(first_byte(&f, 'B') == 0x10 && first_byte(&f, 0xE8) == 0x10,
          "a character the font lacks shows its '?'");
    CHECK(first_byte(&f, 0x2501) == 0x12 && first_byte(&f, 0x2550) == 0x12 &&
              first_byte(&f, 0x23BC) == 0x12,
          "a heavy, a double line and a scan line the font lacks: its light line");
    // A symbol it lacks: the built-in one, each of its pixels 12/8 by 3/8 of
    // the cell's (the cross: the middle two rows and columns).
    CHECK(term_font_bitmap(&f, 0x253C) == NULL && term_font_bitmap(&f, 0x254B) == NULL,
          "a symbol it lacks (U+253C, and the heavy U+254B) is scaled");
    bool scaled = true;
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            bool want = (term_glyph(TERM_GLYPH_CROSS)[y * 8 / HEIGHT] & (0x80 >> (x * 8 / WIDTH))) != 0;
            scaled = scaled && term_font_pixel(&f, 0x254B, x, y) == want;
        }
    }
    CHECK(scaled, "from the built-in glyph, to the cell's size");
    CHECK(term_font_pixel(&f, 'A', 0, 0) == false && term_font_pixel(&f, 'A', 3, 0) == true &&
              term_font_pixel(&f, 'A', 4, 0) == false,
          "a glyph's own pixels (0x11: bit 3 of the first byte)");

    static const uint8_t no_question[] = {'x', 0xFF, 'A', 0xFF, 0xFF, 0xFF, 0xFF};
    make(1, no_question, sizeof(no_question));
    CHECK(load(&f, font_data, font_size) == 0 && first_byte(&f, 'B') == 0x10 &&
              first_byte(&f, 'A') == 0x11,
          "with no '?', its first glyph; empty entries are fine");
    static const uint8_t spaced[] = {'?', 0xFF, ' ', 0xFF, 0xFF, 0xFF, 0xFF};
    make(1, spaced, sizeof(spaced));
    CHECK(load(&f, font_data, font_size) == 0 && first_byte(&f, 0xA0) == 0x11 &&
              first_byte(&f, 0xE9) == 0x10,
          "a no-break space it lacks: its space");
    static const uint8_t empty[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    make(1, empty, sizeof(empty));
    CHECK(term_font_psf2_entries(font_data, font_size) == 1 && load(&f, font_data, font_size) == 0 &&
              f.map_count == 0 && first_byte(&f, 'A') == 0x10,
          "a table that names nothing: every character its first glyph");

    make(0, NULL, 0);
    CHECK(load(&f, font_data, font_size) == 0 && f.map == NULL && first_byte(&f, 3) == 0x13 &&
              first_byte(&f, 'A') == 0x10 && term_font_bitmap(&f, 0x2500) == NULL,
          "with no table, glyph n is code point n, the rest its first glyph, symbols scaled");
}

static void test_refused(void) {
    term_font_t f = {.width = 99};
    struct {
        int offset;
        uint32_t value;
        const char *what;
    } bad[] = {
        {0, 0x864AB573u, "a wrong magic"},
        {4, 1, "another version"},
        {8, 31, "a header too short"},
        {8, 4096, "a header past the end"},
        {16, 0, "no glyphs"},
        {16, 0x10000, "too many glyphs"},
        {16, GLYPHS + 30, "glyphs past the end"},
        {20, GLYPH_BYTES - 1, "a glyph size smaller than its rows"},
        {20, GLYPH_BYTES + 1, "a glyph size larger than its rows"},
        {28, 0, "no width"},
        {28, TERM_FONT_MAX_WIDTH + 1, "too wide"},
        {24, TERM_FONT_MAX_HEIGHT + 1, "too tall"},
    };
    // With a table and without one: a table that no longer parses must not
    // be what refuses the font.
    for (size_t k = 0; k < 2 * sizeof(bad) / sizeof(bad[0]); k++) {
        size_t i = k / 2;
        int with_table = k % 2 == 0;
        make(with_table, table, with_table ? sizeof(table) : 0);
        if (bad[i].offset == 28 && bad[i].value > WIDTH) {
            put32(font_data + 20, (bad[i].value + 7) / 8 * HEIGHT);  // the glyph size to match
        }
        if (bad[i].offset == 24) {
            put32(font_data + 20, 2 * bad[i].value);
        }
        put32(font_data + bad[i].offset, bad[i].value);
        CHECK(load(&f, font_data, font_size) == -1, "refused: %s (%s a table)",
              bad[i].what, with_table ? "with" : "without");
    }
    make(1, table, sizeof(table));
    CHECK(load(&f, font_data, 31) == -1, "refused: shorter than a header");
    CHECK(load(&f, font_data, font_size - 1) == -1,
          "refused: a table cut short (no last separator)");
    static const uint8_t bad_utf8[] = {'?', 0xFF, 0x80, 0xFF, 0xFF, 0xFF, 0xFF};
    make(1, bad_utf8, sizeof(bad_utf8));
    CHECK(load(&f, font_data, font_size) == -1, "refused: a stray continuation byte");
    static const uint8_t cut_utf8[] = {'?', 0xFF, 0xFF, 0xFF, 0xFF, 0xE2, 0x94};
    make(1, cut_utf8, sizeof(cut_utf8));
    CHECK(load(&f, font_data, font_size) == -1, "refused: UTF-8 cut short");
    CHECK(f.width == 99, "and a refused font leaves the old one as it was");
}

static void test_spleen(void) {
    term_font_t f;
    CHECK(load(&f, spleen_8x16_psf, sizeof(spleen_8x16_psf)) == 0,
          "Spleen 8x16, converted from its BDF, is read");
    CHECK(f.width == 8 && f.height == 16 && f.glyph_bytes == 16 && f.glyph_count == 1018,
          "8x16, 1018 glyphs (%ux%u, %u)", f.width, f.height, f.glyph_count);
    // Its 'A': rows 2 to 11, the top flat, the bar on row 6.
    static const uint8_t a[16] = {0, 0, 0x7C, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6,
                                  0xC6, 0xC6, 0xC6, 0xC6, 0, 0, 0, 0};
    bool same = true;
    for (int i = 0; i < 16; i++) {
        same = same && term_font_bitmap(&f, 'A')[i] == a[i];
    }
    CHECK(same, "its 'A' as drawn in the BDF");
    for (unsigned code = 0x80; code < TERM_GLYPH_END; code++) {
        const uint8_t *bitmap = term_font_bitmap(&f, term_glyph_codepoint((uint8_t)code));
        CHECK(bitmap != NULL && bitmap != term_font_bitmap(&f, '?'),
              "symbol 0x%x has a glyph of its own", code);
    }
    const uint8_t *question = term_font_bitmap(&f, '?');
    CHECK(term_font_bitmap(&f, 'a') != term_font_bitmap(&f, 'A'), "'a' is not 'A'");
    CHECK(term_font_bitmap(&f, 0xE9) != question && term_font_bitmap(&f, 0xE9) != term_font_bitmap(&f, 'e') &&
              term_font_bitmap(&f, 0xFC) != question && term_font_bitmap(&f, 0x20AC) != question,
          "e acute, u umlaut and the euro sign have glyphs of their own");
    CHECK(term_font_bitmap(&f, 0x4E2D) == question, "a CJK character Spleen lacks: its '?'");
}

TEST_MAIN(test_builtin, test_psf2, test_refused, test_spleen)
