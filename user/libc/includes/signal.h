#ifndef LIBC_SIGNAL_H
#define LIBC_SIGNAL_H

// Signals as ISO C defines them (C11 7.14), and no further: a signal is
// raised by the program itself, with raise() or abort(), and its handler runs
// at once, in the caller. Nothing is delivered from outside the program.

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#define SIGINT 2    // interactive attention
#define SIGILL 4    // invalid instruction
#define SIGABRT 6   // abnormal termination: abort()
#define SIGFPE 8    // erroneous arithmetic
#define SIGSEGV 11  // invalid access to storage
#define SIGTERM 15  // termination request

void (*signal(int sig, void (*func)(int)))(int);
int raise(int sig);

#endif // LIBC_SIGNAL_H
