// SPDX-License-Identifier: GPL-3.0-or-later
//
// Terminal fonts: the built-in one and PSF2. See font.h.

#include "libconsole/font.h"

#include "libconsole/font8x8.h"

const term_font_t term_font_builtin = {
    .width = 8, .height = 8, .row_bytes = 1, .glyph_bytes = 8,
    .glyph_count = 128 + TERM_GLYPH_END - 0x80, .glyphs = NULL,
};

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

// The header's fields, checked, into `f` (glyphs at their place in `d`).
static int read_header(term_font_t *f, const uint8_t *d, size_t size, bool *table) {
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
    *f = (term_font_t){
        .width = width, .height = height, .row_bytes = (width + 7) / 8,
        .glyph_bytes = glyph_bytes, .glyph_count = count, .glyphs = d + header,
    };
    *table = (flags & PSF2_HAS_UNICODE_TABLE) != 0;
    return 0;
}

// Walk the Unicode table at [p, end): each glyph's entry lists its code
// points, then sequences (which cells never hold), then the separator. Put
// each code point and its glyph in `map` (NULL: only count them). The count,
// or -1 when the table is malformed, cut short, or `map` too small.
static int64_t read_table(const term_font_t *font, const uint8_t *p, const uint8_t *end,
                          term_font_entry_t *map, size_t capacity) {
    size_t n = 0;
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
            if (map != NULL) {
                if (n == capacity) {
                    return -1;
                }
                map[n] = (term_font_entry_t){(uint32_t)cp, glyph};
            }
            n++;
        }
    }
    return (int64_t)n;
}

static bool entry_less(const term_font_entry_t *a, const term_font_entry_t *b) {
    return a->cp < b->cp || (a->cp == b->cp && a->glyph < b->glyph);
}

static void sift_down(term_font_entry_t *map, size_t root, size_t n) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= n) {
            return;
        }
        if (child + 1 < n && entry_less(&map[child], &map[child + 1])) {
            child++;
        }
        if (!entry_less(&map[root], &map[child])) {
            return;
        }
        term_font_entry_t tmp = map[root];
        map[root] = map[child];
        map[child] = tmp;
        root = child;
    }
}

// Sort by code point (heapsort: in place, and no worse than n log n for any
// font), keeping only the first glyph a code point names.
static size_t sort_map(term_font_entry_t *map, size_t n) {
    for (size_t i = n / 2; i-- > 0;) {
        sift_down(map, i, n);
    }
    for (size_t end = n; end > 1; end--) {
        term_font_entry_t tmp = map[0];
        map[0] = map[end - 1];
        map[end - 1] = tmp;
        sift_down(map, 0, end - 1);
    }
    size_t kept = 0;
    for (size_t i = 0; i < n; i++) {
        if (kept == 0 || map[kept - 1].cp != map[i].cp) {
            map[kept++] = map[i];
        }
    }
    return kept;
}

long term_font_psf2_entries(const void *data, size_t size) {
    term_font_t f;
    bool table;
    if (read_header(&f, data, size, &table) != 0) {
        return -1;
    }
    if (!table) {
        return 1;
    }
    int64_t n = read_table(&f, f.glyphs + (size_t)f.glyph_count * f.glyph_bytes,
                           (const uint8_t *)data + size, NULL, 0);
    return n < 0 ? -1 : n == 0 ? 1 : (long)n;
}

// The glyph a font has for `cp` itself, in *glyph.
static bool lookup(const term_font_t *font, uint32_t cp, uint32_t *glyph) {
    if (font->map == NULL) {
        // No table: glyph n is code point n.
        if (cp < font->glyph_count) {
            *glyph = cp;
            return true;
        }
        return false;
    }
    size_t lo = 0, hi = font->map_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (font->map[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < font->map_count && font->map[lo].cp == cp) {
        *glyph = font->map[lo].glyph;
        return true;
    }
    return false;
}

int term_font_psf2(term_font_t *font, const void *data, size_t size, term_font_entry_t *map,
                   size_t capacity) {
    term_font_t f;
    bool table;
    if (read_header(&f, data, size, &table) != 0) {
        return -1;
    }
    if (table) {
        int64_t n = read_table(&f, f.glyphs + (size_t)f.glyph_count * f.glyph_bytes,
                               (const uint8_t *)data + size, map, capacity);
        if (n < 0) {
            return -1;
        }
        f.map = map;
        f.map_count = (uint32_t)sort_map(map, (size_t)n);
    }
    uint32_t question;
    f.fallback = lookup(&f, '?', &question) ? question : 0;
    *font = f;
    return 0;
}

const uint8_t *term_font_bitmap(const term_font_t *font, uint32_t cp) {
    uint8_t symbol = term_symbol(cp);
    if (font->glyphs == NULL) {
        if (cp < 0x80) {
            return font8x8_basic[cp];
        }
        return symbol >= 0x80 ? term_glyph(symbol) : font8x8_basic[symbol == ' ' ? ' ' : '?'];
    }
    uint32_t glyph = font->fallback;
    if (!lookup(font, cp, &glyph)) {
        if (symbol >= 0x80 && !lookup(font, term_glyph_codepoint(symbol), &glyph)) {
            return NULL;  // the built-in symbol, scaled
        }
        if (symbol == ' ') {
            lookup(font, ' ', &glyph);
        }
    }
    return font->glyphs + (size_t)glyph * font->glyph_bytes;
}

bool term_font_pixel(const term_font_t *font, uint32_t cp, uint32_t x, uint32_t y) {
    const uint8_t *bitmap = term_font_bitmap(font, cp);
    if (bitmap == NULL) {
        // The built-in symbol, each of its pixels covering width/8 by height/8.
        bitmap = term_glyph(term_symbol(cp));
        x = x * 8 / font->width;
        y = y * 8 / font->height;
        return (bitmap[y] & (0x80u >> x)) != 0;
    }
    return (bitmap[y * font->row_bytes + x / 8] & (0x80u >> (x % 8))) != 0;
}
