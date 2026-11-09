// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for user/libc/string.c. Built with the host compiler under
// ASan/UBSan; the functions are renamed to kora_* by host_rename.h.
#include "test.h"
#include <string.h>

// A buffer between two canary zones, to catch out-of-bounds writes.
#define CANARY 0xA5
#define GUARD 16
#define BUFSZ 64

typedef struct {
    unsigned char pre[GUARD];
    char buf[BUFSZ];
    unsigned char post[GUARD];
} guarded_t;

static void guard_fill(guarded_t *g, int fill) {
    for (size_t i = 0; i < GUARD; i++) {
        g->pre[i] = CANARY;
        g->post[i] = CANARY;
    }
    for (size_t i = 0; i < BUFSZ; i++)
        g->buf[i] = (char)fill;
}

static int guard_intact(const guarded_t *g) {
    for (size_t i = 0; i < GUARD; i++) {
        if (g->pre[i] != CANARY || g->post[i] != CANARY)
            return 0;
    }
    return 1;
}

static int sign(int v) { return (v > 0) - (v < 0); }

// Reference length, so tests do not depend on the strlen under test.
static size_t ref_len(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void test_strlen(void) {
    CHECK(strlen("") == 0, "strlen of empty string");
    CHECK(strlen("a") == 1, "strlen of one char");
    CHECK(strlen("hello") == 5, "strlen of hello");
    CHECK(strlen("hello\0world") == 5, "strlen stops at first NUL");
    CHECK(strlen("\xff\xfe") == 2, "strlen counts high-bit bytes");
    char big[BUFSZ];
    for (size_t i = 0; i < BUFSZ - 1; i++)
        big[i] = 'x';
    big[BUFSZ - 1] = '\0';
    CHECK(strlen(big) == BUFSZ - 1, "strlen of long string");
}

static void test_strcpy(void) {
    guarded_t g;
    guard_fill(&g, 'z');
    char *r = strcpy(g.buf, "hello");
    CHECK(r == g.buf, "strcpy returns dest");
    CHECK(ref_len(g.buf) == 5 && g.buf[0] == 'h' && g.buf[4] == 'o', "strcpy copies the text");
    CHECK(g.buf[5] == '\0', "strcpy copies the terminator");
    CHECK(g.buf[6] == 'z', "strcpy does not write past the terminator");
    CHECK(guard_intact(&g), "strcpy stays inside the buffer");

    guard_fill(&g, 'z');
    strcpy(g.buf, "");
    CHECK(g.buf[0] == '\0' && g.buf[1] == 'z', "strcpy of empty string writes only the NUL");

    guard_fill(&g, 'z');
    strcpy(g.buf, "abc");
    strcpy(g.buf, "x");
    CHECK(g.buf[0] == 'x' && g.buf[1] == '\0' && g.buf[2] == 'c', "strcpy of shorter string keeps the tail");
}

static void test_strncpy(void) {
    guarded_t g;

    guard_fill(&g, 'z');
    char *r = strncpy(g.buf, "abc", 8);
    CHECK(r == g.buf, "strncpy returns dest");
    CHECK(g.buf[0] == 'a' && g.buf[1] == 'b' && g.buf[2] == 'c', "strncpy copies the text");
    int padded = 1;
    for (size_t i = 3; i < 8; i++)
        padded &= g.buf[i] == '\0';
    CHECK(padded, "strncpy pads with NULs up to n");
    CHECK(g.buf[8] == 'z', "strncpy writes exactly n bytes");
    CHECK(guard_intact(&g), "strncpy (padding) stays inside the buffer");

    guard_fill(&g, 'z');
    strncpy(g.buf, "abcdef", 3);
    CHECK(g.buf[0] == 'a' && g.buf[1] == 'b' && g.buf[2] == 'c', "strncpy truncates to n");
    CHECK(g.buf[3] == 'z', "strncpy does not NUL-terminate a truncated copy");

    guard_fill(&g, 'z');
    strncpy(g.buf, "abc", 3);
    CHECK(g.buf[2] == 'c' && g.buf[3] == 'z', "strncpy with n == strlen copies no NUL");

    guard_fill(&g, 'z');
    strncpy(g.buf, "abc", 0);
    CHECK(g.buf[0] == 'z', "strncpy with n == 0 writes nothing");
    CHECK(guard_intact(&g), "strncpy n == 0 stays inside the buffer");

    guard_fill(&g, 'z');
    strncpy(g.buf, "", 4);
    CHECK(g.buf[0] == '\0' && g.buf[3] == '\0' && g.buf[4] == 'z', "strncpy of empty string zero-fills n bytes");

    // The source may be shorter than n and must not be read past its NUL.
    char exact[4] = {'a', 'b', 'c', '\0'};
    guard_fill(&g, 'z');
    strncpy(g.buf, exact, 32);
    CHECK(g.buf[2] == 'c' && g.buf[31] == '\0' && g.buf[32] == 'z', "strncpy does not read past the source NUL");
}

static void test_strcat(void) {
    guarded_t g;

    guard_fill(&g, 'z');
    g.buf[0] = '\0';
    char *r = strcat(g.buf, "abc");
    CHECK(r == g.buf, "strcat returns dest");
    CHECK(ref_len(g.buf) == 3 && g.buf[0] == 'a', "strcat onto empty string");

    guard_fill(&g, 'z');
    strcpy(g.buf, "foo");
    strcat(g.buf, "bar");
    CHECK(ref_len(g.buf) == 6 && g.buf[2] == 'o' && g.buf[3] == 'b' && g.buf[5] == 'r', "strcat appends");
    CHECK(g.buf[6] == '\0' && g.buf[7] == 'z', "strcat terminates and stops");

    strcat(g.buf, "");
    CHECK(ref_len(g.buf) == 6, "strcat of empty string changes nothing");

    strcat(g.buf, "!");
    strcat(g.buf, "?");
    CHECK(ref_len(g.buf) == 8 && g.buf[6] == '!' && g.buf[7] == '?', "strcat chains");
    CHECK(guard_intact(&g), "strcat stays inside the buffer");
}

static void test_strncat(void) {
    guarded_t g;

    guard_fill(&g, 'z');
    strcpy(g.buf, "foo");
    char *r = strncat(g.buf, "barbaz", 3);
    CHECK(r == g.buf, "strncat returns dest");
    CHECK(ref_len(g.buf) == 6 && g.buf[3] == 'b' && g.buf[5] == 'r', "strncat appends at most n chars");
    CHECK(g.buf[6] == '\0', "strncat always NUL-terminates");
    CHECK(g.buf[7] == 'z', "strncat writes at most n + 1 bytes");

    guard_fill(&g, 'z');
    strcpy(g.buf, "foo");
    strncat(g.buf, "ab", 10);
    CHECK(ref_len(g.buf) == 5 && g.buf[4] == 'b', "strncat with n > strlen(src) appends all of src");
    CHECK(g.buf[6] == 'z', "strncat does not pad like strncpy");

    guard_fill(&g, 'z');
    strcpy(g.buf, "foo");
    strncat(g.buf, "bar", 0);
    CHECK(ref_len(g.buf) == 3 && g.buf[3] == '\0', "strncat with n == 0 appends nothing");

    guard_fill(&g, 'z');
    g.buf[0] = '\0';
    strncat(g.buf, "abc", 3);
    CHECK(ref_len(g.buf) == 3 && g.buf[3] == '\0', "strncat with n == strlen(src) terminates");

    // The source need not be NUL-terminated within n.
    char unterminated[3] = {'x', 'y', 'z'};
    guard_fill(&g, 'q');
    g.buf[0] = '\0';
    strncat(g.buf, unterminated, 3);
    CHECK(ref_len(g.buf) == 3 && g.buf[2] == 'z', "strncat reads at most n bytes of src");
    CHECK(guard_intact(&g), "strncat stays inside the buffer");
}

static void test_strcmp(void) {
    CHECK(strcmp("", "") == 0, "strcmp of two empty strings");
    CHECK(strcmp("abc", "abc") == 0, "strcmp of equal strings");
    CHECK(sign(strcmp("abc", "abd")) < 0, "strcmp: smaller last char");
    CHECK(sign(strcmp("abd", "abc")) > 0, "strcmp: larger last char");
    CHECK(sign(strcmp("ab", "abc")) < 0, "strcmp: prefix sorts first");
    CHECK(sign(strcmp("abc", "ab")) > 0, "strcmp: longer sorts last");
    CHECK(sign(strcmp("", "a")) < 0, "strcmp: empty sorts before non-empty");
    CHECK(sign(strcmp("a", "")) > 0, "strcmp: non-empty sorts after empty");
    CHECK(sign(strcmp("A", "a")) < 0, "strcmp is case sensitive");
    CHECK(sign(strcmp("a\0x", "a\0y")) == 0, "strcmp stops at the first NUL");
    // Bytes compare as unsigned char, as the C standard requires.
    CHECK(sign(strcmp("\x80", "a")) > 0, "strcmp compares bytes as unsigned (high bit)");
    CHECK(sign(strcmp("a", "\xff")) < 0, "strcmp compares bytes as unsigned (0xff)");
}

static void test_strncmp(void) {
    CHECK(strncmp("abc", "xyz", 0) == 0, "strncmp with n == 0 is equal");
    CHECK(strncmp("abcd", "abce", 3) == 0, "strncmp ignores chars after n");
    CHECK(sign(strncmp("abcd", "abce", 4)) < 0, "strncmp sees a difference at n");
    CHECK(sign(strncmp("abce", "abcd", 4)) > 0, "strncmp sign, reversed");
    CHECK(strncmp("abc", "abc", 100) == 0, "strncmp with n > length stops at NUL");
    CHECK(sign(strncmp("ab", "abc", 3)) < 0, "strncmp: shorter string sorts first");
    CHECK(sign(strncmp("abc", "ab", 3)) > 0, "strncmp: longer string sorts last");
    CHECK(strncmp("", "", 5) == 0, "strncmp of empty strings");
    CHECK(strncmp("a\0x", "a\0y", 3) == 0, "strncmp stops at the first NUL");
    CHECK(sign(strncmp("\x80", "a", 1)) > 0, "strncmp compares bytes as unsigned");
    CHECK(strncmp("x", "x", 1) == 0, "strncmp of one equal char");
}

static void test_strstr(void) {
    const char *h = "hello world";
    CHECK(strstr(h, "hello") == h, "strstr finds a prefix");
    CHECK(strstr(h, "world") == h + 6, "strstr finds a suffix");
    CHECK(strstr(h, "o w") == h + 4, "strstr finds a middle match");
    CHECK(strstr(h, "o") == h + 4, "strstr returns the first of several matches");
    CHECK(strstr(h, "hello world") == h, "strstr finds the whole haystack");
    CHECK(strstr(h, "d") == h + 10, "strstr finds the last char");
    CHECK(strstr(h, "xyz") == NULL, "strstr returns NULL when absent");
    CHECK(strstr(h, "hello world!") == NULL, "strstr: needle longer than haystack");
    CHECK(strstr(h, "Hello") == NULL, "strstr is case sensitive");
    CHECK(strstr(h, "") == h, "strstr with an empty needle returns haystack");
    CHECK(strstr("", "") != NULL, "strstr of empty in empty");
    CHECK(strstr("", "a") == NULL, "strstr in an empty haystack");
    const char *p = "aaab";
    CHECK(strstr(p, "aab") == p + 1, "strstr recovers from a partial match");
    const char *q = "abababc";
    CHECK(strstr(q, "ababc") == q + 2, "strstr handles overlapping prefixes");
    const char *m = "mississippi";
    CHECK(strstr(m, "issip") == m + 4, "strstr skips an earlier partial match");
}

static void test_strrstr(void) {
    const char *h = "hello hello";
    CHECK(strrstr(h, "hello") == h + 6, "strrstr returns the last of several matches");
    CHECK(strrstr(h, "o") == h + 10, "strrstr finds the last single char");
    CHECK(strrstr(h, "h") == h + 6, "strrstr of h");
    const char *u = "abcdef";
    CHECK(strrstr(u, "abc") == u, "strrstr finds a single prefix match");
    CHECK(strrstr(u, "def") == u + 3, "strrstr finds a single suffix match");
    CHECK(strrstr(u, "cd") == u + 2, "strrstr finds a middle match");
    CHECK(strrstr(u, "abcdef") == u, "strrstr finds the whole haystack");
    CHECK(strrstr(u, "xyz") == NULL, "strrstr returns NULL when absent");
    CHECK(strrstr(u, "abcdefg") == NULL, "strrstr: needle longer than haystack");
    CHECK(strrstr("", "a") == NULL, "strrstr in an empty haystack");
    const char *o = "aaaa";
    CHECK(strrstr(o, "aa") == o + 2, "strrstr prefers the last overlapping match");
    const char *t = "abcabcab";
    CHECK(strrstr(t, "abc") == t + 3, "strrstr skips a trailing partial match");
    // strrstr() is not standard C; an empty needle is deliberately not tested.
}

static void test_memcpy(void) {
    guarded_t g;
    const char src[8] = {1, 2, 3, 0, 5, 6, 7, 8};

    guard_fill(&g, 'z');
    void *r = memcpy(g.buf, src, sizeof(src));
    CHECK(r == g.buf, "memcpy returns dest");
    int same = 1;
    for (size_t i = 0; i < sizeof(src); i++)
        same &= g.buf[i] == src[i];
    CHECK(same, "memcpy copies every byte, including NULs");
    CHECK(g.buf[8] == 'z', "memcpy writes exactly n bytes");
    CHECK(guard_intact(&g), "memcpy stays inside the buffer");

    guard_fill(&g, 'z');
    memcpy(g.buf, src, 0);
    CHECK(g.buf[0] == 'z', "memcpy with n == 0 writes nothing");

    guard_fill(&g, 'z');
    memcpy(g.buf, src, 1);
    CHECK(g.buf[0] == 1 && g.buf[1] == 'z', "memcpy of one byte");

    // Sizes around typical word/vector boundaries, at every alignment.
    char in[40], out[40 + GUARD];
    for (size_t i = 0; i < sizeof(in); i++)
        in[i] = (char)(i + 1);
    for (size_t off = 0; off < 8; off++) {
        for (size_t n = 0; n <= 32; n++) {
            for (size_t i = 0; i < sizeof(out); i++)
                out[i] = 'z';
            memcpy(out + off, in, n);
            int ok = out[off + n] == 'z' && (off == 0 || out[off - 1] == 'z');
            for (size_t i = 0; i < n; i++)
                ok &= out[off + i] == in[i];
            CHECK(ok, "memcpy off=%zu n=%zu", off, n);
        }
    }
}

static void test_memset(void) {
    guarded_t g;

    guard_fill(&g, 'z');
    void *r = memset(g.buf, 'a', 10);
    CHECK(r == g.buf, "memset returns s");
    int ok = 1;
    for (size_t i = 0; i < 10; i++)
        ok &= g.buf[i] == 'a';
    CHECK(ok, "memset fills n bytes");
    CHECK(g.buf[10] == 'z', "memset writes exactly n bytes");
    CHECK(guard_intact(&g), "memset stays inside the buffer");

    guard_fill(&g, 'z');
    memset(g.buf, 'a', 0);
    CHECK(g.buf[0] == 'z', "memset with n == 0 writes nothing");

    guard_fill(&g, 'z');
    memset(g.buf, 0, 4);
    CHECK(g.buf[0] == 0 && g.buf[3] == 0 && g.buf[4] == 'z', "memset with zero");

    // c is converted to unsigned char.
    guard_fill(&g, 'z');
    memset(g.buf, 0x1FF, 4);
    CHECK((unsigned char)g.buf[0] == 0xFF && (unsigned char)g.buf[3] == 0xFF, "memset truncates c to a byte");
    guard_fill(&g, 'z');
    memset(g.buf, -1, 4);
    CHECK((unsigned char)g.buf[0] == 0xFF, "memset with a negative c");

    // Sizes around word/vector boundaries, at every alignment.
    char out[48];
    for (size_t off = 0; off < 8; off++) {
        for (size_t n = 0; n <= 32; n++) {
            for (size_t i = 0; i < sizeof(out); i++)
                out[i] = 'z';
            memset(out + off, 'q', n);
            int fine = out[off + n] == 'z' && (off == 0 || out[off - 1] == 'z');
            for (size_t i = 0; i < n; i++)
                fine &= out[off + i] == 'q';
            CHECK(fine, "memset off=%zu n=%zu", off, n);
        }
    }
}

static void test_memmove(void) {
    char buf[32];
    guarded_t g;

    // Disjoint regions behave like memcpy.
    guard_fill(&g, 'z');
    void *r = memmove(g.buf, "abcdef", 6);
    CHECK(r == g.buf, "memmove returns dest");
    CHECK(g.buf[0] == 'a' && g.buf[5] == 'f' && g.buf[6] == 'z', "memmove of disjoint regions");
    CHECK(guard_intact(&g), "memmove stays inside the buffer");

    memmove(g.buf, "xyz", 0);
    CHECK(g.buf[0] == 'a', "memmove with n == 0 writes nothing");

    // Overlap, destination after source (must copy backwards).
    for (size_t i = 0; i < 10; i++)
        buf[i] = (char)('a' + i);
    memmove(buf + 2, buf, 6);
    CHECK(buf[0] == 'a' && buf[1] == 'b', "memmove forward overlap keeps the head");
    int ok = 1;
    for (size_t i = 0; i < 6; i++)
        ok &= buf[2 + i] == (char)('a' + i);
    CHECK(ok, "memmove forward overlap (dest > src)");
    CHECK(buf[8] == 'i' && buf[9] == 'j', "memmove forward overlap keeps the tail");

    // Overlap, destination before source (must copy forwards).
    for (size_t i = 0; i < 10; i++)
        buf[i] = (char)('a' + i);
    memmove(buf, buf + 2, 6);
    ok = 1;
    for (size_t i = 0; i < 6; i++)
        ok &= buf[i] == (char)('c' + i);
    CHECK(ok, "memmove backward overlap (dest < src)");
    CHECK(buf[6] == 'g' && buf[9] == 'j', "memmove backward overlap keeps the tail");

    // Same pointer is a no-op.
    for (size_t i = 0; i < 10; i++)
        buf[i] = (char)('a' + i);
    memmove(buf, buf, 10);
    ok = 1;
    for (size_t i = 0; i < 10; i++)
        ok &= buf[i] == (char)('a' + i);
    CHECK(ok, "memmove onto itself");

    // Every overlap distance in both directions.
    for (size_t dist = 1; dist < 9; dist++) {
        for (size_t i = 0; i < 24; i++)
            buf[i] = (char)(i + 1);
        memmove(buf + dist, buf, 16);
        ok = 1;
        for (size_t i = 0; i < 16; i++)
            ok &= buf[dist + i] == (char)(i + 1);
        CHECK(ok, "memmove up by %zu", dist);

        for (size_t i = 0; i < 24; i++)
            buf[i] = (char)(i + 1);
        memmove(buf, buf + dist, 16);
        ok = 1;
        for (size_t i = 0; i < 16; i++)
            ok &= buf[i] == (char)(dist + i + 1);
        CHECK(ok, "memmove down by %zu", dist);
    }
}

static void test_memcmp(void) {
    CHECK(memcmp("abc", "abd", 0) == 0, "memcmp with n == 0 is equal");
    CHECK(memcmp("abc", "abc", 3) == 0, "memcmp of equal buffers");
    CHECK(sign(memcmp("abc", "abd", 3)) < 0, "memcmp: smaller");
    CHECK(sign(memcmp("abd", "abc", 3)) > 0, "memcmp: larger");
    CHECK(memcmp("abc", "abd", 2) == 0, "memcmp ignores bytes after n");
    CHECK(sign(memcmp("abc", "xbc", 3)) < 0, "memcmp: first byte differs");
    // Embedded NULs do not terminate the comparison.
    CHECK(memcmp("a\0b", "a\0b", 3) == 0, "memcmp of equal buffers with NUL");
    CHECK(sign(memcmp("a\0b", "a\0c", 3)) < 0, "memcmp continues past a NUL");
    CHECK(sign(memcmp("a\0c", "a\0b", 3)) > 0, "memcmp continues past a NUL (reversed)");
    // Bytes compare as unsigned char.
    CHECK(sign(memcmp("\x80", "\x7f", 1)) > 0, "memcmp compares bytes as unsigned");
    CHECK(sign(memcmp("\x00", "\xff", 1)) < 0, "memcmp: 0x00 sorts before 0xff");
    CHECK(memcmp("x", "x", 1) == 0, "memcmp of one equal byte");

    // A difference only at the last byte, across several lengths.
    char a[40], b[40];
    for (size_t n = 1; n <= 33; n++) {
        for (size_t i = 0; i < sizeof(a); i++)
            a[i] = b[i] = (char)i;
        CHECK(memcmp(a, b, n) == 0, "memcmp equal, n=%zu", n);
        b[n - 1] = (char)(b[n - 1] + 1);
        CHECK(sign(memcmp(a, b, n)) < 0, "memcmp last-byte difference, n=%zu", n);
        CHECK(memcmp(a, b, n - 1) == 0, "memcmp stops before the differing byte, n=%zu", n);
    }
}

TEST_MAIN(test_strlen, test_strcpy, test_strncpy, test_strcat, test_strncat,
          test_strcmp, test_strncmp, test_strstr, test_strrstr,
          test_memcpy, test_memset, test_memmove, test_memcmp)
