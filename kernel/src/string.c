#include "string.h"

void *memset(void *dst, int c, size_t n)
{
    u8 *d = dst;
    u64 q = (u64)(u8)c * 0x0101010101010101ULL;
    while (n && ((u64)(uintptr_t)d & 7)) {
        *d++ = (u8)c;
        n--;
    }
    u64 *d64 = (u64 *)(uintptr_t)d;
    while (n >= 8) {
        *d64++ = q;
        n -= 8;
    }
    d = (u8 *)(uintptr_t)d64;
    while (n--)
        *d++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n && ((u64)(uintptr_t)d & 7)) {
        *d++ = *s++;
        n--;
    }
    u64 *d64 = (u64 *)(uintptr_t)d;
    const u64 *s64 = (const u64 *)(uintptr_t)s;
    while (n >= 8) {
        *d64++ = *s64++;
        n -= 8;
    }
    d = (u8 *)(uintptr_t)d64;
    s = (const u8 *)(uintptr_t)s64;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s)
        return memcpy(dst, src, n);
    /* перекрытие: копируем с конца */
    d += n;
    s += n;
    while (n && ((u64)(uintptr_t)d & 7)) {
        *--d = *--s;
        n--;
    }
    u64 *d64 = (u64 *)(uintptr_t)d;
    const u64 *s64 = (const u64 *)(uintptr_t)s;
    while (n >= 8) {
        *--d64 = *--s64;
        n -= 8;
    }
    d = (u8 *)(uintptr_t)d64;
    s = (const u8 *)(uintptr_t)s64;
    while (n--)
        *--d = *--s;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *pa = a, *pb = b;
    while (n--) {
        if (*pa != *pb)
            return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? (u8)*a - (u8)*b : 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && (*d = *src) != '\0') {
        d++;
        src++;
        n--;
    }
    while (n--)
        *d++ = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : NULL;
}
