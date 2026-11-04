/* util.h -- the few C-library pieces the program needs.
 *
 * The amiga-debug toolchain links with -nostdlib: there is no libc and no
 * libgcc, so string handling, formatting (through exec's RawDoFmt) and the
 * 32-bit multiply/divide helpers GCC calls on a plain 68000 live in util.c.
 */
#ifndef SWOSDLC_UTIL_H
#define SWOSDLC_UTIL_H

#include <exec/types.h>

extern struct ExecBase *SysBase;

void *memcpy(void *dst, const void *src, unsigned long n);
void *memmove(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);
unsigned long strlen(const char *s);

/* Bounded copy/append: always NUL-terminate, never write past `size`. */
void str_copy(char *dst, const char *src, ULONG size);
void str_append(char *dst, const char *src, ULONG size);
BOOL str_equal(const char *a, const char *b);
BOOL str_starts(const char *s, const char *prefix);

/* exec RawDoFmt into `buf`. Integer arguments must use %ld/%lu/%lx: GCC
 * passes every int as 32 bits, and RawDoFmt's plain %d reads 16. The result
 * is truncated to `size` (the formatted text is built in a 512-byte scratch
 * buffer first, so keep individual messages shorter than that). */
void str_format(char *buf, ULONG size, const char *fmt, ...);

#endif
