# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's `!` (project_doc/koracli-spec.md §5.2): OS commands typed in Python
# blocks, turned into Python before the block runs.
#
#   !cmd args          kora._bang(False, ...): runs live, prints nothing; the
#                      status is in kora.status
#   name = !cmd args   kora._bang(True, ...): the output captured
#
# at a statement's start: the line typed at >>>, or a line of a block that
# does not continue open brackets. The command line:
#
#   - words separated by spaces, '...' or "..." grouping them;
#   - {expr} a Python value: a list or tuple alone as a word gives one
#     argument per item, any other value str(value), inside a word or not;
#     {{ and }} are braces;
#   - < file, > file, >> file: redirection;
#   - & and @N as the last words: in the background, on core N.
#
# OS mode reads its lines the same way, without {expr} (interpolate=False).
# Pure Python: tested on the host (tests/micropython/koracli_bang.py).

import editor


class CommandError(ValueError):
    pass


def _expression(text, i):
    """The Python expression from `i` to its closing '}', and the index
    after it."""
    depth = 0
    quote = None
    start = i
    while i < len(text):
        c = text[i]
        if quote:
            if c == "\\":
                i += 2
                continue
            if c == quote:
                quote = None
        elif c in "'\"":
            quote = c
        elif c in "([{":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "}":
            if depth == 0:
                expr = text[start:i].strip()
                if not expr:
                    raise CommandError("an empty {}")
                return expr, i + 1
            depth -= 1
        i += 1
    raise CommandError("a { without its }")


def tokens(text, interpolate=True):
    """The command line's tokens: ("op", "<" or ">" or ">>"), or ("word",
    pieces, quoted), a piece being text or ("expr", source)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in " \t":
            i += 1
            continue
        if c in "<>":
            op = ">>" if text.startswith(">>", i) else c
            out.append(("op", op))
            i += len(op)
            continue
        pieces = []
        current = []
        quote = None
        quoted = False
        while i < n:
            c = text[i]
            if quote is None and c in " \t<>":
                break
            if quote is None and c in "'\"":
                quote = c
                quoted = True
                i += 1
                continue
            if c == quote:
                quote = None
                i += 1
                continue
            if interpolate and c == "{":
                if text.startswith("{{", i):
                    current.append("{")
                    i += 2
                    continue
                expr, i = _expression(text, i + 1)
                if current:
                    pieces.append("".join(current))
                    current = []
                pieces.append(("expr", expr))
                continue
            if interpolate and c == "}":
                if text.startswith("}}", i):
                    current.append("}")
                    i += 2
                    continue
                raise CommandError("a } without its {")
            current.append(c)
            i += 1
        if quote is not None:
            raise CommandError("a quote without its end")
        if current or not pieces:
            pieces.append("".join(current))
        out.append(("word", pieces, quoted))
    return out


def _suffix(token):
    """'&', the core of '@N', or None, for an unquoted plain word."""
    if token[0] != "word" or token[2] or len(token[1]) != 1 or isinstance(token[1][0], tuple):
        return None
    word = token[1][0]
    if word == "&":
        return "&"
    if len(word) > 1 and word[0] == "@" and word[1:].isdigit():
        return int(word[1:])
    return None


def parse(text, interpolate=True):
    """A command line: a dict with words (each a list of pieces), stdin and
    stdout (a word's pieces, or None), append, background and core."""
    toks = tokens(text, interpolate)
    background = False
    core = None
    for _ in range(2):
        if not toks:
            break
        s = _suffix(toks[-1])
        if s == "&" and not background:
            background = True
        elif isinstance(s, int) and core is None:
            core = s
        else:
            break
        toks.pop()
    words = []
    stdin = stdout = None
    append = False
    i = 0
    while i < len(toks):
        t = toks[i]
        if t[0] == "op":
            if i + 1 >= len(toks) or toks[i + 1][0] != "word":
                raise CommandError("%s needs a file" % t[1])
            target = toks[i + 1][1]
            if t[1] == "<":
                stdin = target
            else:
                stdout = target
                append = t[1] == ">>"
            i += 2
            continue
        words.append(t[1])
        i += 1
    if not words:
        raise CommandError("no command")
    return {"words": words, "stdin": stdin, "stdout": stdout, "append": append,
            "background": background, "core": core}


def _pieces_code(pieces):
    parts = []
    for p in pieces:
        if isinstance(p, tuple):
            parts.append("(%s,)" % p[1])
        else:
            parts.append(repr(p))
    return "(%s,)" % ", ".join(parts)


def command_code(text, capturing):
    """The Python call a command line becomes."""
    c = parse(text)
    words = "[%s]" % ", ".join(_pieces_code(w) for w in c["words"])
    args = [repr(capturing), words]
    if c["stdin"] is not None:
        args.append("stdin=" + _pieces_code(c["stdin"]))
    if c["stdout"] is not None:
        args.append("stdout=" + _pieces_code(c["stdout"]))
    if c["append"]:
        args.append("append=True")
    if c["background"]:
        args.append("background=True")
    if c["core"] is not None:
        args.append("core=%d" % c["core"])
    return "kora._bang(%s)" % ", ".join(args)


def _assignment(line):
    """(name, command) for `name = !command`, or None."""
    i = 0
    while i < len(line) and (line[i].isalpha() or line[i].isdigit() or line[i] in "_."):
        i += 1
    if i == 0 or line[0].isdigit():
        return None
    name = line[:i]
    rest = line[i:].lstrip()
    if not rest.startswith("=") or rest.startswith("=="):
        return None
    rest = rest[1:].lstrip()
    if not rest.startswith("!"):
        return None
    return name, rest[1:]


def translate(text):
    """A block with its `!` lines as Python."""
    out = []
    so_far = ""
    for line in text.split("\n"):
        if not editor.open_brackets(so_far):
            stripped = line.lstrip()
            indent = line[:len(line) - len(stripped)]
            if stripped.startswith("!"):
                line = indent + command_code(stripped[1:], False)
            else:
                assigned = _assignment(stripped)
                if assigned is not None:
                    line = indent + assigned[0] + " = " + command_code(assigned[1], True)
        out.append(line)
        so_far += line + "\n"
    return "\n".join(out)


def os_line(text):
    """An OS mode line: the words, without {expr}, for kora._bang."""
    c = parse(text, interpolate=False)
    return c
