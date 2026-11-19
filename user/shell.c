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
        long n = kgetline(line + used, sizeof(line) - 1 - used);
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
        long n = kgetline(discard, sizeof(discard));
        if (n <= 0 || discard[n - 1] == '\n') {
            break;
        }
    }
    fputs("shell: line too long\n", stderr);
    return 0;
}

// Whether each token was quoted or escaped: such a token is never an operator
// (see take_background).
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
    fputs("KoraOS shell commands:\n"
          "  help            show this text\n"
          "  exit            leave the shell\n"
          "  cd [path]       change directory, or print cwd\n"
          "  pwd             print current directory\n"
          "  volumes         list devices and volume labels\n"
          "  name:           change to a device, volume or assign\n"
          "  assign          list assigns\n"
          "  assign name target  assign an existing directory\n"
          "  assign name     remove an assign (sys: is fixed)\n"
          "  ls [path]       list a directory (default current)\n"
          "  cat <path>...   print file contents\n"
          "  cp/rm/mkdir/rmdir/mv  manage files (cp and mv preserve existing targets)\n"
          "  sync            flush writable volumes\n"
          "  quote paths containing spaces; backslash escapes the next character\n"
          "  <program> [args...]  run a program from c:\n"
          "  <program> [args...] &  run it in the background\n"
          "  <program> [args...] @N  run it on core N (with & too: ... @N &)\n"
          "  <program> [args...] < in > out  read stdin from a file, write stdout\n"
          "                  to one (>> appends); the shell's console otherwise\n"
          "  ps              list tasks and the core each runs on\n"
          "  kill <pid>      end a program (a background job)\n"
          "programs in /bin: hello, echo, gfxdemo, termdemo, condemo, jobdemo, pindemo,\n"
          "  ls, cat\n"
          "  (try 'ls /bin')\n",
          stdout);
}

static void print_cwd(void) {
    if (getcwd(cwd_buffer, sizeof(cwd_buffer)) < 0) {
        fputs("pwd: cannot read current directory\n", stderr);
        return;
    }
    puts(cwd_buffer);
}

static void list_volumes(void) {
    struct volume_info info;
    int rc;
    for (unsigned i = 0; (rc = volume_info(i, &info)) == 1; i++) {
        printf("%s: %s%s%s\n", info.device, info.label[0] ? info.label : "(unlabelled)",
               info.flags & VOLUME_BOOT ? " [boot]" : "",
               info.flags & VOLUME_READ_ONLY ? " [read-only]" : "");
    }
    if (rc < 0) {
        fputs("volumes: enumeration failed\n", stderr);
    }
}

static void assign_command(int argc, char **argv) {
    if (argc > 3) {
        fputs("usage: assign [name [target]]\n", stderr);
        return;
    }
    if (argc > 1) {
        if (assign(argv[1], argc == 3 ? argv[2] : (const char *)0) < 0) {
            fprintf(stderr, "assign: cannot update %s\n", argv[1]);
        }
        return;
    }
    int rc;
    for (unsigned i = 0; (rc = assign_info(i, &assign_buffer)) == 1; i++) {
        printf("%s: = %s%s\n", assign_buffer.name, assign_buffer.target,
               assign_buffer.flags & ASSIGN_IMMUTABLE ? " [fixed]" : "");
    }
    if (rc < 0) {
        fputs("assign: enumeration failed\n", stderr);
    }
}

// kill <pid>: end a program. Its exit (EXIT_KILLED) is reported as a job's at
// the next prompt, once its threads are gone.
static void kill_command(int argc, char **argv) {
    int pid = 0;
    const char *p = argc == 2 ? argv[1] : "";
    for (; *p >= '0' && *p <= '9' && pid < 100000000; p++) {
        pid = pid * 10 + (*p - '0');
    }
    if (argc != 2 || *p != '\0' || pid <= 0) {
        fputs("usage: kill <pid>\n", stderr);
    } else if (kill(pid) < 0) {
        fprintf(stderr, "kill: no such program: %s\n", argv[1]);
    }
}

// Job lines go out in one write(): a job on another core, or on this one
// between two writes, prints at the same time, and only a single write is
// never split. stdout is line buffered and empty when a job line starts, so
// each printf is one write at its newline.
static void print_job_end(int pid, const char *what, int code) {
    printf("[%d%s%d\n", pid, what, code);
}

// Report background jobs that finished since the last prompt (reaping them).
static void reap_jobs(void) {
    int code;
    int pid;
    while ((pid = waitpid(-1, &code, WNOHANG)) > 0) {
        print_job_end(pid, "] done, exit ", code);
    }
}

// A trailing "&" (its own word, or the end of the last one) asks for a
// background job; strip it and say so. A quoted or escaped token is literal:
// `echo '&'` and `echo a\&` print an ampersand.
static int take_background(int *argc, char **argv) {
    char *last = argv[*argc - 1];
    size_t len = strlen(last);
    if (literal_tokens[*argc - 1] || len == 0 || last[len - 1] != '&') {
        return 0;
    }
    if (len == 1) {
        (*argc)--;
    } else {
        last[len - 1] = '\0';
    }
    return 1;
}

// A trailing "@N" word (after any "&" is taken off) pins the program to core
// N: returns N, -1 if there is none, or -2 if the word is malformed. A quoted
// or escaped "@2" is an ordinary argument.
#define MAX_CORE_DIGITS 3

static int take_core(int *argc, char **argv) {
    if (*argc < 2) {
        return -1;  // "@2" alone would be the program name
    }
    char *last = argv[*argc - 1];
    if (literal_tokens[*argc - 1] || last[0] != '@') {
        return -1;
    }
    int core = 0;
    unsigned digits = 0;
    for (const char *p = last + 1; *p; p++, digits++) {
        if (*p < '0' || *p > '9' || digits == MAX_CORE_DIGITS) {
            return -2;
        }
        core = core * 10 + (*p - '0');
    }
    if (digits == 0) {
        return -2;
    }
    (*argc)--;
    return core;
}

// "< path", "> path" and ">> path" (or the operator glued to the path:
// ">out.txt") give the program a file for its stdin or stdout. Takes them out
// of argv. A quoted or escaped operator is an ordinary argument. Returns 0, or
// -1 after saying what is wrong.
struct redirections {
    const char *in;
    const char *out;
    int append;
};

static int take_redirections(int *argc, char **argv, struct redirections *r) {
    int kept = 1;  // argv[0] is the program
    for (int i = 1; i < *argc; i++) {
        char *token = argv[i];
        int length = 0;  // of the operator
        if (!literal_tokens[i]) {
            length = token[0] == '>' && token[1] == '>' ? 2 : token[0] == '>' || token[0] == '<';
        }
        if (length == 0) {
            literal_tokens[kept] = literal_tokens[i];
            argv[kept++] = token;
            continue;
        }
        const char *path = token + length;
        if (*path == '\0') {
            if (i + 1 == *argc) {
                fprintf(stderr, "shell: %.*s needs a file\n", length, token);
                return -1;
            }
            path = argv[++i];
        }
        const char **slot = token[0] == '<' ? &r->in : &r->out;
        if (*slot != NULL) {
            fprintf(stderr, "shell: more than one %s redirection\n", token[0] == '<' ? "input" : "output");
            return -1;
        }
        *slot = path;
        if (token[0] == '>') {
            r->append = length == 2;
        }
    }
    *argc = kept;
    return 0;
}

// Open a redirection's file: an fd, or -1 after saying why not.
static int open_redirection(const char *path, int flags) {
    struct stat st;
    if (stat(path, &st) == 0 && st.is_dir) {
        fprintf(stderr, "shell: %s is a directory\n", path);
        return -1;
    }
    int fd = open(path, flags);
    if (fd < 0) {
        fprintf(stderr, "shell: cannot open %s\n", path);
    }
    return fd;
}

int main(void) {
    fputs("\nKoraOS shell. Type 'help'.\n", stdout);

    char *argv[MAX_ARGV];

    for (;;) {
        reap_jobs();
        fputs("$ ", stdout);  // no newline: kgetline flushes it
        if (!read_command_line()) {
            continue;
        }

        int argc = tokenize(line, argv);
        if (argc < 0) {
            fputs(argc == -1 ? "shell: too many arguments\n" : "shell: invalid quoting\n", stderr);
            continue;
        }
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) {
            puts("bye");
            return 0;
        }
        if (strcmp(argv[0], "help") == 0) {
            help();
            continue;
        }

        if (strcmp(argv[0], "pwd") == 0) {
            if (argc == 1) {
                print_cwd();
            } else {
                fputs("usage: pwd\n", stderr);
            }
            continue;
        }
        if (strcmp(argv[0], "cd") == 0) {
            if (argc == 1) {
                print_cwd();
            } else if (argc == 2) {
                if (chdir(argv[1]) < 0) {
                    fprintf(stderr, "cd: cannot change directory to %s\n", argv[1]);
                }
            } else {
                fputs("usage: cd [path]\n", stderr);
            }
            continue;
        }
        if (strcmp(argv[0], "volumes") == 0) {
            if (argc == 1) {
                list_volumes();
            } else {
                fputs("usage: volumes\n", stderr);
            }
            continue;
        }
        if (strcmp(argv[0], "sync") == 0) {
            if (argc != 1) {
                fputs("usage: sync\n", stderr);
            } else if (sync() < 0) {
                fputs("sync: failed\n", stderr);
            }
            continue;
        }
        if (strcmp(argv[0], "assign") == 0) {
            assign_command(argc, argv);
            continue;
        }
        if (strcmp(argv[0], "kill") == 0) {
            kill_command(argc, argv);
            continue;
        }
        size_t command_len = strlen(argv[0]);
        if (argc == 1 && command_len > 1 && argv[0][command_len - 1] == ':') {
            if (chdir(argv[0]) < 0) {
                fprintf(stderr, "cd: cannot change directory to %s\n", argv[0]);
            }
            continue;
        }

        int background = take_background(&argc, argv);
        if (argc == 0) {
            continue;
        }
        struct redirections redirect = {NULL, NULL, 0};
        if (take_redirections(&argc, argv, &redirect) < 0) {
            continue;
        }
        int core = take_core(&argc, argv);
        if (core == -2) {
            fputs("shell: @N needs a core number, e.g. @1\n", stderr);
            continue;
        }
        int flags = (background ? SPAWN_NOWAIT : 0) | (core >= 0 ? SPAWN_CORE(core) : 0);
        int pid;
        if (redirect.in != NULL || redirect.out != NULL) {
            // The child gets the shell's own handles but for the files named;
            // the shell closes its copies once the child has them.
            struct std_handle std[3];
            if (std_handles(std) < 0) {
                fputs("shell: no standard handles\n", stderr);
                continue;
            }
            int in = -1, out = -1;
            if (redirect.in != NULL && (in = open_redirection(redirect.in, O_RDONLY)) < 0) {
                continue;
            }
            if (redirect.out != NULL &&
                (out = open_redirection(redirect.out, O_WRONLY | O_CREAT |
                                                      (redirect.append ? O_APPEND : O_TRUNC))) < 0) {
                if (in >= 0) {
                    close(in);
                }
                continue;
            }
            if (in >= 0) {
                std[0] = (struct std_handle){STD_FILE, in};
            }
            if (out >= 0) {
                std[1] = (struct std_handle){STD_FILE, out};
            }
            pid = spawn_std(argv[0], argc, argv, flags, std);
            if (in >= 0) {
                close(in);
            }
            if (out >= 0) {
                close(out);
            }
        } else {
            pid = spawn_flags(argv[0], argc, argv, flags);
        }
        if (pid < 0) {
            if (core >= 0) {
                fprintf(stderr, "shell: cannot run %s on cpu %d (no such program, or no such core)\n",
                        argv[0], core);
            } else {
                fprintf(stderr, "shell: no such program: %s\n", argv[0]);
            }
            continue;
        }
        if (background) {
            printf("[%d] started\n", pid);
            continue;
        }
        print_job_end(pid, "] exited with ", wait(pid));
    }
}
