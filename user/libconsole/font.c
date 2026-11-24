// SPDX-License-Identifier: GPL-3.0-or-later
//
// Terminal fonts: the built-in one and PSF2. See font.h.

#include "libconsole/font.h"

#include "libconsole/font8x8.h"

const term_font_t term_font_builtin = {
    .width = 8, .height = 8, .row_bytes = 1, .glyph_bytes = 8,
    .glyph_count = TERM_GLYPH_END, .glyphs = NULL,
};

const uint8_t *term_font_bitmap(const term_font_t *font, uint8_t code) {
    if (font->glyphs == NULL) {
        return code < 0x80 ? font8x8_basic[code] : term_glyph(code);
    }
    uint16_t glyph = code < TERM_GLYPH_END ? font->code_glyph[code] : font->code_glyph['?'];
    if (glyph == TERM_FONT_SCALED) {
        return NULL;
    }
    return font->glyphs + (size_t)glyph * font->glyph_bytes;
}

bool term_font_pixel(const term_font_t *font, uint8_t code, uint32_t x, uint32_t y) {
    const uint8_t *bitmap = term_font_bitmap(font, code);
    if (bitmap == NULL) {
        // The built-in glyph, each of its pixels covering width/8 by height/8.
        bitmap = term_glyph(code);
        x = x * 8 / font->width;
        y = y * 8 / font->height;
        return (bitmap[y] & (0x80u >> x)) != 0;
    }
    return (bitmap[y * font->row_bytes + x / 8] & (0x80u >> (x % 8))) != 0;
}

// --- PSF2 -----------------------------------------------------------------------

#define PSF2_MAGIC 0x864AB572u
#define PSF2_HAS_UNICODE_TABLE 0x01
#define PSF2_SEQUENCE 0xFE   // a sequence of code points follows, to the separator
#define PSF2_SEPARATOR 0xFF  // the end of a glyph's entry

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Decode one UTF-8 code point at *p (before end); -1 when it is not one.
static int64_t utf8(const uint8_t **p, const uint8_t *end) {
    uint8_t b = **p;
    uint32_t cp;
    int more;
    if (b < 0x80) {
        cp = b;
        more = 0;
    } else if ((b & 0xE0) == 0xC0) {
        cp = b & 0x1F;
        more = 1;
    } else if ((b & 0xF0) == 0xE0) {
        cp = b & 0x0F;
        more = 2;
    } else if ((b & 0xF8) == 0xF0) {
        cp = b & 0x07;
        more = 3;
    } else {
        return -1;
    }
    if (end - *p <= more) {
        return -1;
    }
    for (int i = 1; i <= more; i++) {
        uint8_t c = (*p)[i];
        if ((c & 0xC0) != 0x80) {
            return -1;
        }
        cp = cp << 6 | (c & 0x3F);
    }
    *p += more + 1;
    return cp;
}

// The code point a cell code shows.
static uint32_t code_point(uint8_t code) {
    return code < 0x80 ? code : term_glyph_codepoint(code);
}

// Find the glyphs for the cell codes in the Unicode table at [p, end): each
// glyph's entry lists its code points, then sequences (which cells never
// hold), then the separator. 0, or -1 when the table is malformed or cut short.
static int read_table(term_font_t *font, const uint8_t *p, const uint8_t *end, bool *found) {
    for (uint32_t glyph = 0; glyph < font->glyph_count; glyph++) {
        bool sequence = false;
        for (;;) {
            if (p >= end) {
                return -1;
            }
            if (*p == PSF2_SEPARATOR) {
                p++;
                break;
            }
            if (*p == PSF2_SEQUENCE) {
                sequence = true;
                p++;
                continue;
            }
            int64_t cp = utf8(&p, end);
            if (cp < 0) {
                return -1;
            }
            if (sequence) {
                continue;
            }
            for (uint32_t code = 0; code < TERM_GLYPH_END; code++) {
                if (!found[code] && code_point((uint8_t)code) == (uint32_t)cp) {
                    font->code_glyph[code] = (uint16_t)glyph;
                    found[code] = true;
                }
            }
        }
    }
    return 0;
}

int term_font_psf2(term_font_t *font, const void *data, size_t size) {
    const uint8_t *d = data;
    if (size < 32 || le32(d) != PSF2_MAGIC || le32(d + 4) != 0) {
        return -1;
    }
    uint32_t header = le32(d + 8), flags = le32(d + 12), count = le32(d + 16);
    uint32_t glyph_bytes = le32(d + 20), height = le32(d + 24), width = le32(d + 28);
    if (header < 32 || header > size || count == 0 || count > 0xFFFF || width == 0 ||
        width > TERM_FONT_MAX_WIDTH || height == 0 || height > TERM_FONT_MAX_HEIGHT ||
        glyph_bytes != (width + 7) / 8 * height ||
        (size - header) / glyph_bytes < count) {
        return -1;
    }
    term_font_t f = {
        .width = width, .height = height, .row_bytes = (width + 7) / 8,
        .glyph_bytes = glyph_bytes, .glyph_count = count, .glyphs = d + header,
    };
    bool found[TERM_GLYPH_END] = {false};
    if (flags & PSF2_HAS_UNICODE_TABLE) {
        const uint8_t *table = f.glyphs + (size_t)count * glyph_bytes;
        if (read_table(&f, table, d + size, found) != 0) {
            return -1;
        }
    } else {
        // No table: glyph n is code point n, as far as there are glyphs.
        for (uint32_t code = 0; code < 0x80 && code < count; code++) {
            f.code_glyph[code] = (uint16_t)code;
            found[code] = true;
        }
    }
    uint16_t fallback = found['?'] ? f.code_glyph['?'] : 0;
    for (uint32_t code = 0; code < TERM_GLYPH_END; code++) {
        if (!found[code]) {
            f.code_glyph[code] = code < 0x80 ? fallback : TERM_FONT_SCALED;
        }
    }
    *font = f;
    return 0;
}
