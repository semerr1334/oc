#include "kprintf.h"
#include "console.h"
#include "serial.h"
#include "string.h"

/* Мини-vsprintf: %c %s %d %i %u %x %X %p %% и длины l/ll/z.
 * Выводит и на консоль, и в COM1 (два парсинга не нужен — печатаем
 * через единый колбэк). */
typedef struct {
    char  *buf;
    size_t size;
    size_t pos;
} outbuf_t;

static void out_ch(outbuf_t *o, char c)
{
    if (o->buf && o->pos + 1 < o->size)
        o->buf[o->pos] = c;
    o->pos++;
}

static void out_str(outbuf_t *o, const char *s, int width, bool left)
{
    size_t len = strlen(s);
    int pad = width - (int)len;
    if (pad < 0)
        pad = 0;
    if (!left)
        while (pad-- > 0)
            out_ch(o, ' ');
    while (*s)
        out_ch(o, *s++);
    if (left)
        while (pad-- > 0)
            out_ch(o, ' ');
}

static void out_num(outbuf_t *o, u64 val, bool neg, int base, bool upper,
                    int width, bool zero, bool left)
{
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (val == 0)
        tmp[i++] = '0';
    while (val) {
        tmp[i++] = digits[val % (u64)base];
        val /= (u64)base;
    }
    if (neg)
        tmp[i++] = '-';
    int pad = width - i;
    if (pad < 0)
        pad = 0;
    if (!left && !zero)
        while (pad-- > 0)
            out_ch(o, ' ');
    if (neg && zero)
        out_ch(o, '-');
    if (!left && zero)
        while (pad-- > 0)
            out_ch(o, '0');
    while (i--)
        out_ch(o, tmp[i]);
    if (left)
        while (pad-- > 0)
            out_ch(o, ' ');
}

static int format(outbuf_t *o, const char *fmt, va_list ap)
{
    o->pos = 0;
    while (*fmt) {
        if (*fmt != '%') {
            out_ch(o, *fmt++);
            continue;
        }
        fmt++;
        bool left = false, zero = false;
        for (;; fmt++) {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                zero = true;
            else
                break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int longness = 0;
        while (*fmt == 'l') {
            longness++;
            fmt++;
        }
        if (*fmt == 'z') {
            longness = 2;
            fmt++;
        }

        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!left)
                while (--width > 0)
                    out_ch(o, ' ');
            out_ch(o, c);
            if (left)
                while (--width > 0)
                    out_ch(o, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            out_str(o, s ? s : "(null)", width, left);
            break;
        }
        case 'd':
        case 'i': {
            s64 v = longness >= 2 ? va_arg(ap, s64)
                  : longness == 1 ? va_arg(ap, long)
                  : va_arg(ap, int);
            bool neg = v < 0;
            u64 uv = neg ? (u64)-v : (u64)v;
            out_num(o, uv, neg, 10, false, width, zero, left);
            break;
        }
        case 'u': {
            u64 v = longness >= 2 ? va_arg(ap, u64)
                  : longness == 1 ? va_arg(ap, unsigned long)
                  : va_arg(ap, unsigned);
            out_num(o, v, false, 10, false, width, zero, left);
            break;
        }
        case 'x':
        case 'X': {
            u64 v = longness >= 2 ? va_arg(ap, u64)
                  : longness == 1 ? va_arg(ap, unsigned long)
                  : va_arg(ap, unsigned);
            out_num(o, v, false, 16, *fmt == 'X', width, zero, left);
            break;
        }
        case 'p': {
            u64 v = (u64)va_arg(ap, void *);
            out_ch(o, '0');
            out_ch(o, 'x');
            out_num(o, v, false, 16, false, 16, true, false);
            break;
        }
        case '%':
            out_ch(o, '%');
            break;
        case '\0':
            goto done;
        default:
            out_ch(o, '%');
            out_ch(o, *fmt);
            break;
        }
        fmt++;
    }
done:
    if (o->buf && o->size)
        o->buf[o->pos < o->size ? o->pos : o->size - 1] = '\0';
    return (int)o->pos;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    outbuf_t o = { buf, size, 0 };
    return format(&o, fmt, ap);
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_write(buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
    return n;
}
