/* port.h -- what the portable parts (json, sha256, inflate, unzip, fio) need.
 *
 * Those parts build for the Amiga and, with -DHOST_TEST, for the PC, so they
 * can be tested against real mod zips there (tests/host_test.c). Everything
 * here is the exec types on the Amiga and their stdint equivalents on a PC.
 */
#ifndef SWOSDLC_PORT_H
#define SWOSDLC_PORT_H

#ifdef HOST_TEST
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t UBYTE;
typedef int8_t BYTE;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int16_t BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define mem_alloc(n) calloc(1, (n))
#define mem_free(p) free(p)
#else
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "util.h"
/* Cleared, and from any memory: none of these buffers is ever seen by the
 * custom chips. */
#define mem_alloc(n) AllocVec((n), MEMF_ANY | MEMF_CLEAR)
#define mem_free(p) FreeVec(p)
#endif

#endif
