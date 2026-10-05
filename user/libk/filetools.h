#pragma once

#include "koraos.h"

static inline void filetool_error(const char *tool, const char *message, const char *path) {
    kputs(tool);
    kputs(": ");
    kputs(message);
    if (path) {
        kputs(" ");
        kputs(path);
    }
    kputs("\n");
}

// Report durable completion, including mutations made before an earlier error.
static inline int filetool_finish(const char *tool, int status) {
    if (sync() < 0) {
        filetool_error(tool, "sync failed", NULL);
        return 1;
    }
    return status;
}
