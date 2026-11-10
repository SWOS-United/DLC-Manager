/* unzip.h -- unpacks a mod's zip into a drawer.
 *
 * The index's mods are zips whose root is the mod's folder (mod.json at the
 * top). Entries are stored or deflated, as Python's zipfile and every
 * archiver write them; anything else (encryption, zip64, other methods) is
 * refused rather than half-unpacked. Each entry's CRC-32 is checked.
 *
 * Names are checked before anything is written: no device ("x:"), no
 * absolute path, no "..", no backslash, and no part longer than the 30
 * characters AmigaDOS keeps. A bad name refuses the whole zip.
 */
#ifndef SWOSDLC_UNZIP_H
#define SWOSDLC_UNZIP_H

#include "port.h"

/* done/total in uncompressed bytes; FALSE cancels. May be NULL. */
typedef BOOL (*UnzipProgress)(void *ctx, ULONG done, ULONG total);

/* Unpacks `zip_path` into the drawer `dest`, which must exist. FALSE with
 * the reason in `err` on any failure; what was written so far stays, so the
 * caller unpacks into a scratch drawer and renames it on success. */
BOOL unzip_to(const char *zip_path, const char *dest, UnzipProgress progress, void *ctx,
              char *err, ULONG err_size);

/* The longest name part allowed: 30, AmigaDOS's. The host tests raise it to
 * unpack the PC mods' zips. */
extern ULONG unzip_name_max;

/* The CRC-32 the zip format uses, for the host tests. */
ULONG unzip_crc32(ULONG crc, const UBYTE *data, ULONG len);

#endif
