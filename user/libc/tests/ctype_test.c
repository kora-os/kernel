// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for user/libc/ctype.c. Built with the host compiler under
// ASan/UBSan; the functions are renamed to kora_* by host_rename.h.
#include "test.h"
#include <ctype.h>

static void test_character_classes(void) {
    for (int c = 0; c <= 255; c++) {
        int alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        int lower = c >= 'a' && c <= 'z';
        int upper = c >= 'A' && c <= 'Z';
        int digit = c >= '0' && c <= '9';
        int xdigit = digit || (c >= 'A' && c <= 'F') ||
                     (c >= 'a' && c <= 'f');
        int control = (c >= 0 && c <= 31) || c == 127;
        int graph = c > 32 && c < 127;
        int space = c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                    c == '\v' || c == '\f';
        int blank = c == ' ' || c == '\t';
        int print = c >= 32 && c < 127;
        int punct = graph && !alpha && !digit;

        CHECK(!!isalnum(c) == (alpha || digit), "isalnum(%d)", c);
        CHECK(!!isalpha(c) == alpha, "isalpha(%d)", c);
        CHECK(!!islower(c) == lower, "islower(%d)", c);
        CHECK(!!isupper(c) == upper, "isupper(%d)", c);
        CHECK(!!isdigit(c) == digit, "isdigit(%d)", c);
        CHECK(!!isxdigit(c) == xdigit, "isxdigit(%d)", c);
        CHECK(!!iscntrl(c) == control, "iscntrl(%d)", c);
        CHECK(!!isgraph(c) == graph, "isgraph(%d)", c);
        CHECK(!!isspace(c) == space, "isspace(%d)", c);
        CHECK(!!isblank(c) == blank, "isblank(%d)", c);
        CHECK(!!isprint(c) == print, "isprint(%d)", c);
        CHECK(!!ispunct(c) == punct, "ispunct(%d)", c);
    }

    CHECK(!isalnum(-1), "isalnum(EOF)");
    CHECK(!isalpha(-1), "isalpha(EOF)");
    CHECK(!isdigit(-1), "isdigit(EOF)");
    CHECK(!isxdigit(-1), "isxdigit(EOF)");
    CHECK(!iscntrl(-1), "iscntrl(EOF)");
    CHECK(!isgraph(-1), "isgraph(EOF)");
    CHECK(!isspace(-1), "isspace(EOF)");
    CHECK(!isblank(-1), "isblank(EOF)");
    CHECK(!isprint(-1), "isprint(EOF)");
    CHECK(!ispunct(-1), "ispunct(EOF)");
}

static void test_case_conversion(void) {
    for (int c = 0; c <= 255; c++) {
        int expected_lower = c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
        int expected_upper = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;

        CHECK(tolower(c) == expected_lower, "tolower(%d)", c);
        CHECK(toupper(c) == expected_upper, "toupper(%d)", c);
    }

    CHECK(tolower(-1) == -1, "tolower(EOF) is unchanged");
    CHECK(toupper(-1) == -1, "toupper(EOF) is unchanged");
}

TEST_MAIN(test_character_classes, test_case_conversion)
