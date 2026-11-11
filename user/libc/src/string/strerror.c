// SPDX-License-Identifier: GPL-3.0-or-later
//
// strerror (C11 7.24.6.2): a fixed message for every code in <errno.h>.

#include <errno.h>
#include <string.h>

static const struct {
    int code;
    const char *message;
} messages[] = {
    {0, "No error"},
    {EPERM, "Operation not permitted"},
    {ENOENT, "No such file or directory"},
    {EINTR, "Interrupted"},
    {EIO, "Input/output error"},
    {EBADF, "Bad file descriptor"},
    {ECHILD, "No child process"},
    {EAGAIN, "Resource temporarily unavailable"},
    {ENOMEM, "Out of memory"},
    {EACCES, "Permission denied"},
    {EFAULT, "Bad address"},
    {EBUSY, "Resource busy"},
    {EEXIST, "File exists"},
    {EXDEV, "Cross-device link"},
    {ENODEV, "No such device"},
    {ENOTDIR, "Not a directory"},
    {EISDIR, "Is a directory"},
    {EINVAL, "Invalid argument"},
    {ENFILE, "Too many open files in the system"},
    {EMFILE, "Too many open files"},
    {ENOTTY, "Not a terminal"},
    {EFBIG, "File too large"},
    {ENOSPC, "No space left on device"},
    {ESPIPE, "Invalid seek"},
    {EROFS, "Read-only file system"},
    {EPIPE, "Broken pipe"},
    {EDOM, "Argument out of domain"},
    {ERANGE, "Result out of range"},
    {ENAMETOOLONG, "File name too long"},
    {ENOSYS, "Function not implemented"},
    {ENOTEMPTY, "Directory not empty"},
    {EOVERFLOW, "Value too large"},
    {EILSEQ, "Invalid multibyte sequence"},
};

char *strerror(int errnum) {
    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
        if (messages[i].code == errnum) {
            return (char *)messages[i].message;
        }
    }
    return "Unknown error";
}
