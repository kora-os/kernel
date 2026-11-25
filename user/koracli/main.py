# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's Python side, which the console task (user/koracli.c) runs as
# `micropython sys:koracli/main.py`. The editor and the shell are modules
# here in sys:koracli, plain Python to read and change. If they fail (an
# exception, a broken edit), MicroPython's own REPL takes over, so there is
# always a prompt to fix them from.

import sys

import kora

try:
    import shell

    shell.main()
except Exception as e:
    try:
        kora.mode(kora.COOKED)
        kora.esc_break(False)
    except Exception:
        pass
    print("\nkoracli: the editor failed; MicroPython's own REPL takes over (Ctrl+D ends it)")
    sys.print_exception(e)
    kora.repl()
