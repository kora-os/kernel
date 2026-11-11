// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for src/string/search.c: memchr, strchr, strrchr, strspn,
// strcspn, strpbrk, strtok, strcoll and strxfrm, including the terminating
// NUL as a searchable character, bytes above 127, and every position of a
// match in buffers of several lengths.

#include "test.h"

#include <string.h>

static void test_memchr(void) {
    char buf[64];
    for (int len = 0; len <= 40; len++) {
        for (int at = 0; at < len; at++) {
            memset(buf, 'a', sizeof buf);
            buf[at] = 'x';
            CHECK(memchr(buf, 'x', (size_t)len) == buf + at, "memchr len %d at %d", len, at);
        }
        memset(buf, 'a', sizeof buf);
        buf[len] = 'x';
        CHECK(memchr(buf, 'x', (size_t)len) == NULL, "memchr stops at n (%d)", len);
    }
    unsigned char high[3] = {1, 0xff, 2};
    CHECK(memchr(high, 0xff, 3) == high + 1, "memchr compares as unsigned char");
    CHECK(memchr(high, -1, 3) == high + 1, "memchr converts c to unsigned char");
    char nul[] = {'a', '\0', 'b'};
    CHECK(memchr(nul, 'b', 3) == nul + 2, "memchr looks past NUL bytes");
    CHECK(memchr(NULL, 'a', 0) == NULL, "memchr with n = 0");
}

static void test_strchr_strrchr(void) {
    const char *s = "hello, world";
    CHECK(strchr(s, 'o') == s + 4, "strchr first o");
    CHECK(strrchr(s, 'o') == s + 8, "strrchr last o");
    CHECK(strchr(s, 'z') == NULL && strrchr(s, 'z') == NULL, "no match");
    CHECK(strchr(s, '\0') == s + 12 && strrchr(s, '\0') == s + 12, "the NUL is found");
    CHECK(strchr("", 'a') == NULL && *strchr("", '\0') == '\0', "empty string");
    char high[] = "a\xe9" "b\xe9";
    CHECK(strchr(high, 0xe9) == high + 1 && strrchr(high, 0xe9) == high + 3,
          "bytes above 127 (c converted to char)");
    CHECK(strchr(s, 'h') == s && strrchr(s, 'd') == s + 11, "first and last character");
}

static void test_spans(void) {
    CHECK(strspn("abcde", "abc") == 3, "strspn");
    CHECK(strspn("abcde", "") == 0, "strspn with an empty set");
    CHECK(strspn("", "abc") == 0, "strspn of empty");
    CHECK(strspn("aaaa", "a") == 4, "strspn all");
    CHECK(strcspn("abcde", "dc") == 2, "strcspn");
    CHECK(strcspn("abcde", "") == 5, "strcspn with an empty set");
    CHECK(strcspn("abcde", "xyz") == 5, "strcspn no match");
    CHECK(strspn("\xff\xfe\x01", "\xfe\xff") == 2, "strspn with high bytes");
    const char *s = "key=value;other";
    CHECK(strpbrk(s, "=;") == s + 3, "strpbrk");
    CHECK(strpbrk(s, "!?") == NULL, "strpbrk no match");
    CHECK(strpbrk(s, "") == NULL, "strpbrk empty set");
}

static void test_strtok(void) {
    char line[] = "  ls -l   /bin;; echo hi ";
    const char *want[] = {"ls", "-l", "/bin", "echo", "hi"};
    char *tok = strtok(line, " ;");
    int i = 0, ok = 1;
    for (; tok != NULL; tok = strtok(NULL, " ;"), i++) {
        ok &= i < 5 && strcmp(tok, want[i]) == 0;
    }
    CHECK(ok && i == 5, "strtok splits on a set of delimiters (%d tokens)", i);
    CHECK(strtok(NULL, " ") == NULL, "strtok stays at the end");

    char only[] = ";;;";
    CHECK(strtok(only, ";") == NULL, "only delimiters: no token");
    char empty[] = "";
    CHECK(strtok(empty, ";") == NULL, "empty string: no token");

    // The delimiters may change between calls.
    char mixed[] = "a,b c,d";
    CHECK(strcmp(strtok(mixed, ","), "a") == 0, "first token");
    CHECK(strcmp(strtok(NULL, " "), "b") == 0, "second with other delimiters");
    CHECK(strcmp(strtok(NULL, ","), "c") == 0, "third");
    CHECK(strcmp(strtok(NULL, ","), "d") == 0 && strtok(NULL, ",") == NULL, "last");
}

static void test_collation(void) {
    CHECK(strcoll("abc", "abd") < 0 && strcoll("b", "a") > 0 && strcoll("x", "x") == 0,
          "strcoll is strcmp in the C locale");
    char buf[8];
    CHECK(strxfrm(buf, "hello", sizeof buf) == 5 && strcmp(buf, "hello") == 0, "strxfrm copies");
    CHECK(strxfrm(buf, "a much longer string", 4) == 20 && strcmp(buf, "a m") == 0,
          "strxfrm truncates and returns the full length");
    CHECK(strxfrm(NULL, "abc", 0) == 3, "strxfrm measures with n = 0");
}

TEST_MAIN(test_memchr, test_strchr_strrchr, test_spans, test_strtok, test_collation)
