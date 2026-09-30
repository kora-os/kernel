#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hold a portable POSIX lock for build.sh's owned userfs and its consumers."""

import fcntl
import os
import signal
import subprocess
import sys
import time

if len(sys.argv) < 3:
    sys.exit("usage: with-userfs-lock.py LOCKFILE COMMAND [ARGS...]")
lock_path = sys.argv[1]
try:
    timeout = float(os.environ.get("KORAOS_USERFS_LOCK_TIMEOUT", "120"))
    if not 0 < timeout <= 3600:
        raise ValueError
except ValueError:
    sys.exit("KORAOS_USERFS_LOCK_TIMEOUT must be a number from 0 to 3600 seconds")
with open(lock_path, "a", encoding="utf-8") as lock:
    deadline = time.monotonic() + timeout
    while True:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.monotonic() >= deadline:
                sys.exit(f"Timed out waiting for userfs producer lock: {lock_path}")
            time.sleep(0.1)
    environment = dict(
        os.environ,
        KORAOS_USERFS_LOCK_PATH=lock_path,
        KORAOS_USERFS_LOCK_FD=str(lock.fileno()),
    )
    child = None
    interrupted = None

    def forward(signum, _frame):
        global interrupted
        interrupted = signum
        if child is not None:
            try:
                os.killpg(child.pid, signum)
            except ProcessLookupError:
                pass

    signal.signal(signal.SIGTERM, forward)
    signal.signal(signal.SIGINT, forward)
    if interrupted is not None:
        sys.exit(128 + interrupted)
    # The whole build runs in one process group, so interrupts reach CMake,
    # make and their children. Inheriting the lock's open file description
    # also keeps it held if this wrapper is killed before descendants exit.
    child = subprocess.Popen(
        sys.argv[2:], env=environment, start_new_session=True, pass_fds=(lock.fileno(),)
    )
    if interrupted is not None:
        forward(interrupted, None)
    result = child.wait()
    sys.exit(
        128 + interrupted if interrupted else (128 - result if result < 0 else result)
    )
