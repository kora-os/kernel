/* pindemo: unbalanced sub-processes placed by hand. Each argument starts a
 * worker program (pindemo itself, in worker mode) with `units` of work, each
 * about 100 ms of one core's time, pinned to a core with @core or else put on
 * the least-loaded core. The workers report as they finish.
 *
 *   pindemo [units[@core] ...]
 *
 * Without arguments, on three or more cores: 6@1 2@2 2@2 2@2, the heavy worker
 * alone on core 1 and three light ones sharing core 2, so every worker
 * finishes after about 600 ms: balanced by hand, as TempleOS would have it.
 * The least-loaded placement would give each its own core and leave the heavy
 * one finishing last. With fewer cores: 3 1 1 1, unpinned.
 */
#include "libk/koraos.h"

#define MAX_WORKERS 8
#define UNIT_US 100000  // the solo CPU time a unit is calibrated to

// Busy work no compiler can skip: a linear congruential generator. Not
// inlined, so calibration and workers run the very same loop.
__attribute__((noinline)) static unsigned long churn(unsigned long rounds) {
    unsigned long x = 1;
    for (unsigned long i = 0; i < rounds; i++) {
        x = x * 6364136223846793005ul + 1442695040888963407ul;
    }
    return x;
}

// Digits at *s, stopping before `limit` would be passed (no wrap-around).
static unsigned long parse(const char **s, unsigned long limit) {
    unsigned long v = 0;
    while (**s >= '0' && **s <= '9' && v <= limit / 10) {
        v = v * 10 + (unsigned long)(**s - '0');
        (*s)++;
    }
    return v;
}

// The core this program's main thread runs on. task_info walks the table by
// index, and other tasks ending meanwhile shift the entries, so one walk can
// miss us: try again.
static int my_core(void) {
    struct task_info info;
    for (int attempt = 0; attempt < 100; attempt++) {
        for (unsigned i = 0; task_info(i, &info) == 1; i++) {
            if (info.tid == getpid()) {
                return (int)info.core;
            }
        }
    }
    return -1;
}

struct worker {
    unsigned long units;
    int core;      // -1: least loaded
    int pid;
};

int main(int argc, char **argv) {
    // Worker mode: pindemo -w <rounds>; exits with the core it ran on.
    if (argc == 3 && strcmp(argv[1], "-w") == 0) {
        const char *s = argv[2];
        volatile unsigned long sink = churn(parse(&s, ~0ul / 2));
        (void)sink;
        return my_core();
    }

    static const char *const balanced[] = {"6@1", "2@2", "2@2", "2@2"};
    static const char *const unpinned[] = {"3", "1", "1", "1"};
    struct sysinfo info;
    unsigned cpus = sysinfo(&info) == 0 ? info.cpus : 1;
    const char *const *specs = (const char *const *)argv + 1;
    int count = argc - 1;
    if (count == 0) {
        specs = cpus >= 3 ? balanced : unpinned;
        count = 4;
    }
    if (count > MAX_WORKERS) {
        fputs("pindemo: at most 8 workers\n", stderr);
        return 2;
    }
    struct worker workers[MAX_WORKERS];
    for (int i = 0; i < count; i++) {
        const char *s = specs[i];
        workers[i].units = parse(&s, 1000);
        workers[i].core = -1;
        if (*s == '@') {
            s++;
            workers[i].core = (int)parse(&s, 1000);
        }
        if (*s != '\0' || workers[i].units == 0 || workers[i].units > 100) {
            fputs("usage: pindemo [units[@core] ...]   (1 to 100 units of ~100 ms)\n", stderr);
            return 2;
        }
    }

    // Calibrate a unit on this core: how many rounds take UNIT_US alone.
    unsigned long rounds = 1u << 16;
    unsigned long took;
    for (;;) {
        unsigned long start = uptime_us();
        volatile unsigned long sink = churn(rounds);
        (void)sink;
        took = uptime_us() - start;
        if (took >= 50000 || rounds >= (1ul << 40)) {
            break;
        }
        rounds *= 2;
    }
    unsigned long unit_rounds = rounds * UNIT_US / (took != 0 ? took : 1);

    unsigned long start = uptime_us();
    for (int i = 0; i < count; i++) {
        char arg[24];
        snprintf(arg, sizeof(arg), "%lu", workers[i].units * unit_rounds);
        char *child_argv[] = {"pindemo", "-w", arg};
        int flags = SPAWN_NOWAIT | (workers[i].core >= 0 ? SPAWN_CORE(workers[i].core) : 0);
        workers[i].pid = spawn_flags("pindemo", 3, child_argv, flags);
        if (workers[i].pid < 0) {
            fprintf(stderr, "pindemo: cannot start worker %d%s\n", i + 1,
                    workers[i].core >= 0 ? " (no such core?)" : "");
            while (i-- > 0) {
                kill(workers[i].pid);
                wait(workers[i].pid);
            }
            return 1;
        }
    }

    int failed = 0;
    for (int done = 0; done < count; done++) {
        int code = -1;
        int pid = waitpid(-1, &code, 0);
        int i = 0;
        while (i < count && workers[i].pid != pid) {
            i++;
        }
        if (i == count) {
            fputs("pindemo: FAILED, lost a worker\n", stderr);
            return 1;
        }
        // Each report is one line, so one write() at its newline.
        char cpu[24] = "?";
        if (code >= 0) {
            snprintf(cpu, sizeof(cpu), "%d", code);
        }
        printf("pindemo: worker %d (%lu %s, cpu %s%s) done after %lu ms\n", i + 1,
               workers[i].units, workers[i].units == 1 ? "unit" : "units", cpu,
               workers[i].core >= 0 ? " pinned" : "", (uptime_us() - start) / 1000);
        if (code < 0 || (workers[i].core >= 0 && code != workers[i].core)) {
            failed = 1;
        }
    }
    if (failed) {
        fputs("pindemo: FAILED, a worker ran on the wrong core\n", stderr);
    } else {
        printf("pindemo: %d workers done in %lu ms\n", count, (uptime_us() - start) / 1000);
    }
    return failed;
}
