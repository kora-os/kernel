char *itoa(int value, char *str, int base) {
    int i = 0;
    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }
    do {
        str[i++] = '0' + value % base;
        value /= base;
    } while (value > 0);
    if (is_negative) {
        str[i++] = '-';
    }
    str[i] = '\0';
    return str;
}
