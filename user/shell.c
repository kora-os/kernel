/* KoraOS shell: a tiny interactive command line running in EL0. It reads a
 * line, splits it into argv, and spawns the named program with those arguments
 * (so, e.g., "echo hello world" runs the echo program with two args). The
 * kernel starts this as the first user program.
 */
#include "libk/koraos.h"

#define MAX_ARGV 16
#define LINE_MAX 128

// Split a line into argv in place: whitespace runs become NUL terminators and
// each token gets a slot. Returns argc.
static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p && argc < MAX_ARGV) {
        while (*p == ' ' || *p == '\t' || *p == '\n') {
            *p++ = '\0';
        }
        if (!*p) {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
            p++;
        }
    }
    return argc;
}

static void help(void) {
    kputs("KoraOS shell commands:\n");
    kputs("  help            show this text\n");
    kputs("  exit            leave the shell\n");
    kputs("  ls [path]       list a directory (default /)\n");
    kputs("  cat <path>...   print file contents\n");
    kputs("  <program> [args...]  run an embedded program\n");
    kputs("available programs: hello, init, echo, gfxdemo, ls, cat\n");
}

int main(void) {
    kputs("\nKoraOS shell. Type 'help'.\n");

    char line[LINE_MAX];
    char *argv[MAX_ARGV];

    for (;;) {
        kputs("$ ");
        long n = read(0, line, sizeof(line) - 1);
        if (n <= 0) {
            continue;
        }
        line[n] = '\0';

        int argc = tokenize(line, argv);
        if (argc == 0) {
            continue;
        }

        if (kstreq(argv[0], "exit")) {
            kputs("bye\n");
            return 0;
        }
        if (kstreq(argv[0], "help")) {
            help();
            continue;
        }

        int pid = spawn(argv[0], argc, argv);
        if (pid < 0) {
            kputs("shell: no such program: ");
            kputs(argv[0]);
            kputs("\n");
            continue;
        }
        int code = wait(pid);
        kputs("[");
        kput_int(pid);
        kputs("] exited with ");
        kput_int(code);
        kputs("\n");
    }
}
