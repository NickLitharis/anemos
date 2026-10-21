/* Freestanding helpers lwIP needs. The kernel has Rtl* routines, but lwIP is
 * compiled without builtins and calls the C names directly. */
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;

    while (count-- > 0) {
        *to++ = *from++;
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *to = destination;

    while (count-- > 0) {
        *to++ = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = left;
    const unsigned char *b = right;

    while (count-- > 0) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        a++;
        b++;
    }
    return 0;
}

int anem_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int anem_isxdigit(int c)
{
    return anem_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int anem_islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int anem_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int atoi(const char *text)
{
    int value = 0;
    int sign = 1;

    while (anem_isspace((unsigned char)*text)) {
        text++;
    }
    if (*text == '-') {
        sign = -1;
        text++;
    } else if (*text == '+') {
        text++;
    }
    while (anem_isdigit((unsigned char)*text)) {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value * sign;
}

int strncmp(const char *left, const char *right, size_t count)
{
    while (count-- > 0) {
        if (*left != *right) {
            return (int)(unsigned char)*left - (int)(unsigned char)*right;
        }
        if (*left == '\0') {
            return 0;
        }
        left++;
        right++;
    }
    return 0;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;

    if (to < from) {
        while (count-- > 0) {
            *to++ = *from++;
        }
    } else {
        to += count;
        from += count;
        while (count-- > 0) {
            *--to = *--from;
        }
    }
    return destination;
}

size_t strlen(const char *text)
{
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}
