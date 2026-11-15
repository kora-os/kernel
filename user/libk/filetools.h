#pragma once

#include "koraos.h"

// "tool: message path" (or "tool: message") on stderr.
static inline void filetool_error(const char *tool, const char *message, const char *path) {
    if (path) {
        fprintf(stderr, "%s: %s %s\n", tool, message, path);
    } else {
        fprintf(stderr, "%s: %s\n", tool, message);
    }
}

// Report durable completion, including mutations made before an earlier error.
static inline int filetool_finish(const char *tool, int status) {
    if (sync() < 0) {
        filetool_error(tool, "sync failed", NULL);
        return 1;
    }
    return status;
}
