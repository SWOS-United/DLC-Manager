/* stdlib.h -- empty stand-in for a -nostdlib build.
 *
 * Roadshow's <sys/socket.h> includes <stdlib.h> only so that memmove() and
 * memset() are declared for its inline macros; util.h declares those, and
 * nothing here uses the rest of the C library. */
#ifndef SWOSDLC_SHIM_STDLIB_H
#define SWOSDLC_SHIM_STDLIB_H
#include <stddef.h>
#endif
