/* sha256.h -- SHA-256, fed in pieces, for checking a download against the
 * index's "sha256" while it arrives. */
#ifndef SWOSDLC_SHA256_H
#define SWOSDLC_SHA256_H

#include "port.h"

struct Sha256 {
    ULONG state[8];
    ULONG bytes_hi, bytes_lo;   /* total length, 64 bits */
    UBYTE block[64];
    UWORD used;
};

void sha256_init(struct Sha256 *s);
void sha256_update(struct Sha256 *s, const UBYTE *data, ULONG len);
/* Writes the digest as 64 lower-case hex digits and a NUL. */
void sha256_hex(struct Sha256 *s, char out[65]);

#endif
