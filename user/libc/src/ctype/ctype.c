#include <ctype.h>

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int islower(int c) {
    return c >= 'a' && c <= 'z';
}

int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int isxdigit(int c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

int iscntrl(int c) {
    return (c >= 0 && c <= 31) || c == 127;
}

int isgraph(int c) {
    return c > 32 && c < 127;
}
int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

int isblank(int c) {
    return c == ' ' || c == '\t';
}

int isprint(int c) {
    return c >= 32 && c < 127;
}

int ispunct(int c) {
    return isgraph(c) && !isalnum(c);
}

int tolower(int c) {
    if (islower(c)) {
        return c;
    } else if (isupper(c)) {
        return c - 'A' + 'a';
    } else {
        return c;
    }
}

int toupper(int c) {
    if (isupper(c)) {
        return c;
    } else if (islower(c)) {
        return c - 'a' + 'A';
    } else {
        return c;
    }
}
