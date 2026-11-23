// SPDX-License-Identifier: GPL-3.0-or-later
//
// libk's syscall stubs for host tests: the C twin of user/libk/syscall.S. Each
// stub passes its arguments to the libc's __syscall(), which the tests link
// as the fake kernel (user/libc/tests/fake_syscall.c). The fake kernel serves
// the console, the files, memory and sync itself and hands every other call
// to the handler a test installs with fake_set_handler().
//
// Keep the list in step with syscall.S and libk_host_rename.h
// (tests/check-syscall-abi.py compares the three).

#include "libk/koraos.h"

long __syscall(long number, long a0, long a1, long a2, long a3);

#define CALL(number, a0, a1, a2, a3) \
    __syscall((number), (long)(a0), (long)(a1), (long)(a2), (long)(a3))

ssize_t write(int fd, const void *buf, size_t len) {
    return CALL(SYS_WRITE, fd, buf, len, 0);
}

ssize_t read(int fd, void *buf, size_t len) {
    return CALL(SYS_READ, fd, buf, len, 0);
}

int libk_spawn_flags(const char *name, int argc, char *const argv[], int flags) {
    return (int)CALL(SYS_SPAWN_FLAGS, name, argc, argv, flags);
}

int waitpid(int pid, int *code, int flags) {
    return (int)CALL(SYS_WAITPID, pid, code, flags, 0);
}

int getpid(void) {
    return (int)CALL(SYS_GETPID, 0, 0, 0, 0);
}

void yield(void) {
    CALL(SYS_YIELD, 0, 0, 0, 0);
}

int fb_info(struct fb_info *out) {
    return (int)CALL(SYS_FB_INFO, out, 0, 0, 0);
}

int open(const char *path, int flags) {
    return (int)CALL(SYS_OPEN, path, flags, 0, 0);
}

int close(int fd) {
    return (int)CALL(SYS_CLOSE, fd, 0, 0, 0);
}

long lseek(int fd, long offset, int whence) {
    return CALL(SYS_LSEEK, fd, offset, whence, 0);
}

int readdir(int fd, struct dirent *out) {
    return (int)CALL(SYS_READDIR, fd, out, 0, 0);
}

int stat(const char *path, struct stat *out) {
    return (int)CALL(SYS_STAT, path, out, 0, 0);
}

void *alloc_pages(size_t count) {
    return (void *)CALL(SYS_ALLOC_PAGES, count, 0, 0, 0);
}

int free_pages(void *base) {
    return (int)CALL(SYS_FREE_PAGES, base, 0, 0, 0);
}

void msleep(unsigned long ms) {
    CALL(SYS_MSLEEP, ms, 0, 0, 0);
}

int forbid(void) {
    return (int)CALL(SYS_FORBID, 0, 0, 0, 0);
}

int permit(void) {
    return (int)CALL(SYS_PERMIT, 0, 0, 0, 0);
}

int task_info(unsigned index, struct task_info *out) {
    return (int)CALL(SYS_TASK_INFO, index, out, 0, 0);
}

int thread_new(void (*start)(void), thread_fn fn, void *arg, int core) {
    return (int)CALL(SYS_THREAD_NEW, start, fn, arg, core);
}

void thread_exit(long value) {
    CALL(SYS_THREAD_EXIT, value, 0, 0, 0);
    __builtin_trap();  // the kernel never returns from it
}

int thread_join(int tid, long *value) {
    return (int)CALL(SYS_THREAD_JOIN, tid, value, 0, 0);
}

int kill(int pid) {
    return (int)CALL(SYS_KILL, pid, 0, 0, 0);
}

int sysinfo(struct sysinfo *out) {
    return (int)CALL(SYS_SYSINFO, out, 0, 0, 0);
}

int chdir(const char *path) {
    return (int)CALL(SYS_CHDIR, path, 0, 0, 0);
}

int getcwd(char *buf, size_t size) {
    return (int)CALL(SYS_GETCWD, buf, size, 0, 0);
}

int volume_info(unsigned int index, struct volume_info *out) {
    return (int)CALL(SYS_VOLUME_INFO, index, out, 0, 0);
}

int assign(const char *name, const char *target) {
    return (int)CALL(SYS_ASSIGN, name, target, 0, 0);
}

int assign_info(unsigned int index, struct assign_info *out) {
    return (int)CALL(SYS_ASSIGN_INFO, index, out, 0, 0);
}

int sync(void) {
    return (int)CALL(SYS_SYNC, 0, 0, 0, 0);
}

int unlink(const char *path) {
    return (int)CALL(SYS_UNLINK, path, 0, 0, 0);
}

int mkdir(const char *path) {
    return (int)CALL(SYS_MKDIR, path, 0, 0, 0);
}

int rmdir(const char *path) {
    return (int)CALL(SYS_RMDIR, path, 0, 0, 0);
}

int port_create(const char *name) {
    return (int)CALL(SYS_PORT_CREATE, name, 0, 0, 0);
}

int port_delete(int port) {
    return (int)CALL(SYS_PORT_DELETE, port, 0, 0, 0);
}

int port_find(const char *name) {
    return (int)CALL(SYS_PORT_FIND, name, 0, 0, 0);
}

int port_put(int port, void *msg, int reply_port) {
    return (int)CALL(SYS_PORT_PUT, port, msg, reply_port, 0);
}

int port_get(int port, struct port_message *out) {
    return (int)CALL(SYS_PORT_GET, port, out, 0, 0);
}

int port_wait(int port, struct port_message *out) {
    return (int)CALL(SYS_PORT_WAIT, port, out, 0, 0);
}

int port_reply(void *msg) {
    return (int)CALL(SYS_PORT_REPLY, msg, 0, 0, 0);
}

int std_handles(struct std_handle out[3]) {
    return (int)CALL(SYS_STD_HANDLES, out, 0, 0, 0);
}

int libk_spawn_std(const struct spawn_request *request) {
    return (int)CALL(SYS_SPAWN_STD, request, 0, 0, 0);
}

int sig_alloc(int bit) {
    return (int)CALL(SYS_SIG_ALLOC, bit, 0, 0, 0);
}

int sig_free(int bit) {
    return (int)CALL(SYS_SIG_FREE, bit, 0, 0, 0);
}

unsigned sig_wait(unsigned mask) {
    return (unsigned)CALL(SYS_SIG_WAIT, mask, 0, 0, 0);
}

unsigned sig_set(unsigned values, unsigned mask) {
    return (unsigned)CALL(SYS_SIG_SET, values, mask, 0, 0);
}

int sig_send(int tid, unsigned mask) {
    return (int)CALL(SYS_SIG_SEND, tid, mask, 0, 0);
}

int port_signal(int port, int bit) {
    return (int)CALL(SYS_PORT_SIGNAL, port, bit, 0, 0);
}

int sig_on_end(int pid, int bit) {
    return (int)CALL(SYS_SIG_ON_END, pid, bit, 0, 0);
}

int input_claim(int bit) {
    return (int)CALL(SYS_INPUT_CLAIM, bit, 0, 0, 0);
}

int input_read(struct input_event *events, int max) {
    return (int)CALL(SYS_INPUT_READ, events, max, 0, 0);
}

int input_focus(int pid) {
    return (int)CALL(SYS_INPUT_FOCUS, pid, 0, 0, 0);
}

int fb_claim(int on) {
    return (int)CALL(SYS_FB_CLAIM, on, 0, 0, 0);
}

int gettid(void) {
    return (int)CALL(SYS_GETTID, 0, 0, 0, 0);
}

long conlog(const char *text, size_t length) {
    return CALL(SYS_CONLOG, text, length, 0, 0);
}
