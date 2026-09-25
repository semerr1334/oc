/* Мини-библиотека программ OC (.ocp) — системные вызовы int 0x80.
 *   0=write(str,len)  1=uptime  2=getkey  3=exit(код)  4=sleep(мс)
 */
#ifndef OCP_H
#define OCP_H

typedef unsigned short u16;

static long ocp_sys(long n, long a, long b)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b)
                     : "memory");
    return ret;
}

static inline long writes(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return ocp_sys(0, (long)s, n);
}
static inline long getkey(void)          { return ocp_sys(2, 0, 0); }
static inline long uptime_ms(void)       { return ocp_sys(1, 0, 0); }
static inline void clear(void)           { ocp_sys(5, 0, 0); }
static inline void tone(long hz)         { ocp_sys(6, hz, 0); }
static inline void sleep_ms(long ms)     { volatile long n = ms * 30000L; while (n--) ; }
static inline void exit(int code)        { ocp_sys(3, code, 0); for (;;) ; }

#endif
