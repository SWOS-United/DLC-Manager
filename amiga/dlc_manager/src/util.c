/* util.c -- see util.h. */
#include "util.h"

#include <proto/exec.h>

void *memcpy(void *dst, const void *src, unsigned long n)
{
    UBYTE *d = dst;
    const UBYTE *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned long n)
{
    UBYTE *d = dst;
    const UBYTE *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, unsigned long n)
{
    UBYTE *d = dst;
    while (n--) *d++ = (UBYTE)c;
    return dst;
}

unsigned long strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (unsigned long)(p - s);
}

void str_copy(char *dst, const char *src, ULONG size)
{
    if (!size) return;
    while (--size && *src) *dst++ = *src++;
    *dst = 0;
}

void str_append(char *dst, const char *src, ULONG size)
{
    ULONG len = strlen(dst);
    if (len < size) str_copy(dst + len, src, size - len);
}

BOOL str_equal(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

BOOL str_starts(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++) return FALSE;
    return TRUE;
}

/* move.b d0,(a3)+ ; rts -- the classic RawDoFmt PutChProc. */
static const UWORD put_char_proc[] = {0x16c0, 0x4e75};

void str_format(char *buf, ULONG size, const char *fmt, ...)
{
    char scratch[512];
    __builtin_va_list ap;

    /* On m68k every variadic argument sits on the stack in a 4-byte slot,
     * which is exactly the data stream RawDoFmt walks. */
    __builtin_va_start(ap, fmt);
    RawDoFmt((CONST_STRPTR)fmt, (APTR)ap, (VOID (*)())put_char_proc, scratch);
    __builtin_va_end(ap);
    str_copy(buf, scratch, size);
}

/* --- libgcc arithmetic for a plain 68000 ------------------------------- */
/* The 68000 has only 16x16 multiply and 32/16 divide; GCC calls these for
 * 32-bit operands. Written so they cannot recurse into themselves. */

unsigned long __mulsi3(unsigned long a, unsigned long b)
{
    unsigned long lo = (unsigned long)(UWORD)a * (UWORD)b;
    unsigned long mid = (unsigned long)(UWORD)(a >> 16) * (UWORD)b +
                        (unsigned long)(UWORD)a * (UWORD)(b >> 16);
    return lo + (mid << 16);
}

static unsigned long udivmod(unsigned long n, unsigned long d, unsigned long *rem)
{
    unsigned long q = 0, r = 0;
    int i;
    if (!d) {
        if (rem) *rem = 0;
        return 0;
    }
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= 1UL << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

unsigned long __udivsi3(unsigned long n, unsigned long d) { return udivmod(n, d, 0); }

unsigned long __umodsi3(unsigned long n, unsigned long d)
{
    unsigned long r;
    udivmod(n, d, &r);
    return r;
}

long __divsi3(long n, long d)
{
    int neg = (n < 0) != (d < 0);
    unsigned long q = udivmod(n < 0 ? -(unsigned long)n : (unsigned long)n,
                              d < 0 ? -(unsigned long)d : (unsigned long)d, 0);
    return neg ? -(long)q : (long)q;
}

long __modsi3(long n, long d)
{
    unsigned long r;
    udivmod(n < 0 ? -(unsigned long)n : (unsigned long)n,
            d < 0 ? -(unsigned long)d : (unsigned long)d, &r);
    return n < 0 ? -(long)r : (long)r;
}
