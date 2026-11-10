/* fio.h -- the file calls the installer needs: dos.library on the Amiga,
 * stdio/POSIX for the host tests. Paths are AmigaDOS style ("a/b/c"), which
 * a POSIX system reads the same way for relative paths. */
#ifndef SWOSDLC_FIO_H
#define SWOSDLC_FIO_H

#include "port.h"

typedef void *FioFile;

FioFile fio_open_read(const char *path);
FioFile fio_open_write(const char *path);   /* creates or truncates */
void fio_close(FioFile f);
LONG fio_read(FioFile f, void *buf, LONG len);         /* bytes read, -1 on error */
BOOL fio_write(FioFile f, const void *buf, LONG len);  /* all of it, or FALSE */
BOOL fio_seek(FioFile f, LONG pos);                    /* from the start */
LONG fio_size(FioFile f);                              /* -1 if unknown */

BOOL fio_exists(const char *path);         /* a file or a drawer */
BOOL fio_is_dir(const char *path);
BOOL fio_mkdir(const char *path);          /* TRUE if it is there afterwards */
BOOL fio_delete(const char *path);         /* a file, or an empty drawer */
BOOL fio_delete_tree(const char *path);    /* a drawer and everything in it */
BOOL fio_rename(const char *from, const char *to);

/* Calls fn(name, is_dir, ctx) for each entry of the drawer `path`. FALSE if
 * it cannot be read. */
typedef void (*FioDirFn)(const char *name, BOOL is_dir, void *ctx);
BOOL fio_list_dir(const char *path, FioDirFn fn, void *ctx);

/* Joins a drawer and a name: "dir" + "x" -> "dir/x", "DH0:" + "x" -> "DH0:x". */
void fio_join(char *out, ULONG size, const char *dir, const char *name);

/* A whole file, NUL-terminated, freed with mem_free. NULL if it cannot be
 * read or is bigger than `max`. */
char *fio_slurp(const char *path, ULONG max, ULONG *len);

#endif
