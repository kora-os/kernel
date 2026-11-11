// SPDX-License-Identifier: GPL-3.0-or-later
//
// <errno.h> (C11 7.5). errno is a macro over __errno_location() from the
// start, so it can become per thread (once the kernel keeps a thread pointer)
// without changing the ABI. Today it is one variable shared by the threads of
// a program.
//
// The values are Linux's, for familiarity; the KoraOS kernel returns only -1,
// so the libc sets the most likely code for a failed call.
#ifndef LIBC_ERRNO_H
#define LIBC_ERRNO_H

int *__errno_location(void);
#define errno (*__errno_location())

#define EPERM 1          // operation not permitted
#define ENOENT 2         // no such file or directory
#define EINTR 4          // interrupted
#define EIO 5            // input/output error
#define EBADF 9          // bad file descriptor
#define ECHILD 10        // no child process
#define EAGAIN 11        // resource temporarily unavailable
#define ENOMEM 12        // out of memory
#define EACCES 13        // permission denied
#define EFAULT 14        // bad address
#define EBUSY 16         // resource busy
#define EEXIST 17        // file exists
#define EXDEV 18         // cross-device link
#define ENODEV 19        // no such device
#define ENOTDIR 20       // not a directory
#define EISDIR 21        // is a directory
#define EINVAL 22        // invalid argument
#define ENFILE 23        // too many open files in the system
#define EMFILE 24        // too many open files
#define ENOTTY 25        // not a terminal
#define EFBIG 27         // file too large
#define ENOSPC 28        // no space left on device
#define ESPIPE 29        // invalid seek
#define EROFS 30         // read-only file system
#define EPIPE 32         // broken pipe
#define EDOM 33          // argument out of domain (C)
#define ERANGE 34        // result out of range (C)
#define ENAMETOOLONG 36  // file name too long
#define ENOSYS 38        // function not implemented
#define ENOTEMPTY 39     // directory not empty
#define EOVERFLOW 75     // value too large for its type
#define EILSEQ 84        // invalid multibyte sequence (C)

#endif // LIBC_ERRNO_H
