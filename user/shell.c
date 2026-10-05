/* KoraOS shell: a tiny interactive command line running in EL0. It reads a
 * line, splits it into argv, and spawns the named program with those arguments
 * (so, e.g., "echo hello world" runs the echo program with two args). The
 * kernel starts this as the first user program.
 */
#include "libk/koraos.h"

#define MAX_ARGV 16
#define LINE_MAX (2 * KORA_PATH_MAX + 64)

// Qualified paths and enumeration records live in BSS, keeping the one-page
// EL0 stack available for call frames and argv.
static char line[LINE_MAX];
static char cwd_buffer[KORA_PATH_MAX];
static struct assign_info assign_buffer;

// Read a whole command before dispatch. A full buffer without a newline is
// drained and rejected so its prefix cannot become a separate command.
static int read_command_line(void) {
    size_t used = 0;
    while (used < sizeof(line) - 1) {
        long n = read(0, line + used, sizeof(line) - 1 - used);
        if (n <= 0) {
            return 0;
        }
        used += (size_t)n;
        if (line[used - 1] == '\n') {
            line[used] = 0;
            return 1;
        }
    }
    char discard[64];
    for (;;) {
        long n = read(0, discard, sizeof(discard));
        if (n <= 0 || discard[n - 1] == '\n') {
            break;
        }
    }
    kputs("shell: line too long\n");
    return 0;
}

// Keep literal-token provenance for future background/operator parsing.
static unsigned char literal_tokens[MAX_ARGV];

static int whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Compact tokens in place. Quotes concatenate with adjacent text; empty quotes
// produce an empty argument. No expansion or command substitution is performed.
static int tokenize(char *text, char **argv) {
    char *source = text, *destination = text;
    int argc = 0;
    for (;;) {
        while (whitespace(*source)) source++;
        if (!*source) return argc;
        if (argc == MAX_ARGV) return -1;
        argv[argc] = destination;
        literal_tokens[argc] = 0;
        char quote = 0;
        while (*source) {
            char c = *source++;
            if (quote) {
                if (c == '\n' || c == '\r') return -2;
                if (c == quote) {
                    quote = 0;
                    continue;
                }
                if (c == '\\' && quote == '"') {
                    if (!*source || *source == '\n' || *source == '\r') return -2;
                    c = *source++;
                }
                *destination++ = c;
                continue;
            }
            if (whitespace(c)) break;
            if (c == '\'' || c == '"') {
                quote = c;
                literal_tokens[argc] = 1;
            } else if (c == '\\') {
                if (!*source || *source == '\n' || *source == '\r') return -2;
                literal_tokens[argc] = 1;
                *destination++ = *source++;
            } else {
                *destination++ = c;
            }
        }
        if (quote) return -2;
        *destination++ = 0;
        argc++;
    }
}

static void help(void) {
    kputs("KoraOS shell commands:\n");
    kputs("  help            show this text\n");
    kputs("  exit            leave the shell\n");
    kputs("  cd [path]       change directory, or print cwd\n");
    kputs("  pwd             print current directory\n");
    kputs("  volumes         list devices and volume labels\n");
    kputs("  name:           change to a device, volume or assign\n");
    kputs("  assign          list assigns\n");
    kputs("  assign name target  assign an existing directory\n");
    kputs("  assign name     remove an assign (sys: is fixed)\n");
    kputs("  ls [path]       list a directory (default current)\n");
    kputs("  cat <path>...   print file contents\n");
    kputs("  cp/rm/mkdir/rmdir/mv  manage files (cp and mv preserve existing targets)\n");
    kputs("  sync            flush writable volumes\n");
    kputs("  quote paths containing spaces; backslash escapes the next character\n");
    kputs("  <program> [args...]  run a program from c:\n");
    kputs("programs in /bin: hello, echo, gfxdemo, termdemo, ls, cat (try 'ls /bin')\n");
}

static void print_cwd(void) {
    if (getcwd(cwd_buffer, sizeof(cwd_buffer)) < 0) {
        kputs("pwd: cannot read current directory\n");
        return;
    }
    kputs(cwd_buffer);
    kputs("\n");
}

static void list_volumes(void) {
    struct volume_info info;
    int rc;
    for (unsigned i = 0; (rc = volume_info(i, &info)) == 1; i++) {
        kputs(info.device);
        kputs(": ");
        kputs(info.label[0] ? info.label : "(unlabelled)");
        if (info.flags & VOLUME_BOOT) {
            kputs(" [boot]");
        }
        if (info.flags & VOLUME_READ_ONLY) {
            kputs(" [read-only]");
        }
        kputs("\n");
    }
    if (rc < 0) {
        kputs("volumes: enumeration failed\n");
    }
}

static void assign_command(int argc, char **argv) {
    if (argc > 3) {
        kputs("usage: assign [name [target]]\n");
        return;
    }
    if (argc > 1) {
        if (assign(argv[1], argc == 3 ? argv[2] : (const char *)0) < 0) {
            kputs("assign: cannot update ");
            kputs(argv[1]);
            kputs("\n");
        }
        return;
    }
    int rc;
    for (unsigned i = 0; (rc = assign_info(i, &assign_buffer)) == 1; i++) {
        kputs(assign_buffer.name);
        kputs(": = ");
        kputs(assign_buffer.target);
        if (assign_buffer.flags & ASSIGN_IMMUTABLE) {
            kputs(" [fixed]");
        }
        kputs("\n");
    }
    if (rc < 0) {
        kputs("assign: enumeration failed\n");
    }
}

int main(void) {
    kputs("\nKoraOS shell. Type 'help'.\n");

    char *argv[MAX_ARGV];

    for (;;) {
        kputs("$ ");
        if (!read_command_line()) {
            continue;
        }

        int argc = tokenize(line, argv);
        if (argc < 0) {
            kputs(argc == -1 ? "shell: too many arguments\n" : "shell: invalid quoting\n");
            continue;
        }
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

        if (kstreq(argv[0], "pwd")) {
            if (argc == 1) {
                print_cwd();
            } else {
                kputs("usage: pwd\n");
            }
            continue;
        }
        if (kstreq(argv[0], "cd")) {
            if (argc == 1) {
                print_cwd();
            } else if (argc == 2) {
                if (chdir(argv[1]) < 0) {
                    kputs("cd: cannot change directory to ");
                    kputs(argv[1]);
                    kputs("\n");
                }
            } else {
                kputs("usage: cd [path]\n");
            }
            continue;
        }
        if (kstreq(argv[0], "volumes")) {
            if (argc == 1) {
                list_volumes();
            } else {
                kputs("usage: volumes\n");
            }
            continue;
        }
        if (kstreq(argv[0], "sync")) {
            if (argc != 1) {
                kputs("usage: sync\n");
            } else if (sync() < 0) {
                kputs("sync: failed\n");
            }
            continue;
        }
        if (kstreq(argv[0], "assign")) {
            assign_command(argc, argv);
            continue;
        }
        size_t command_len = kstrlen(argv[0]);
        if (argc == 1 && command_len > 1 && argv[0][command_len - 1] == ':') {
            if (chdir(argv[0]) < 0) {
                kputs("cd: cannot change directory to ");
                kputs(argv[0]);
                kputs("\n");
            }
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
