#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""QEMU smoke test: boot the QEMU kernel on the raspi3b, raspi4b or virt
machine, drive the shell on its screen from the serial line (the debug
console's type, key and conlog), and check the output and the framebuffer.
Without a display there is no shell: only the kernel checks run. The shell
is KoraCLI, whose OS mode the steps drive (F4, then F2 after each start).

Waits for expected output (with timeouts) rather than sleeping, so it is not
sensitive to how fast the machine runs QEMU. Standard library only.

Usage: tests/run-qemu-smoke.py --target qemu_raspi3b|qemu_raspi4b|qemu_virt [--out DIR]
Build the kernel first with: ./build.sh --target qemu_raspi3b

Artifacts in --out (default build/qemu-smoke): serial.log and one PNG
screenshot per screen check.
"""

import argparse
import importlib.util
import os
import re
import select
import socket
import struct
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Kernel fault banners (src/arch/exception.c) start like this.
FAULT = re.compile(rb"\n\*\*\* ")


class Failure(Exception):
    pass


class Qemu:
    def __init__(self, qemu, kernel, outdir, machine="raspi3b", ram="256M", extra=(), el2=False,
                 smp=1, gic=2, accel="tcg"):
        self.outdir = outdir
        self.log = open(os.path.join(outdir, "serial.log"), "wb")
        self.pending = b""  # output not yet consumed by expect()
        # The monitor socket path is relative to QEMU's working directory:
        # UNIX socket paths are limited to ~104 bytes and temp dirs are long.
        machine_args = ["-M", machine] if machine.startswith("raspi") else [
            "-M", "virt,gic-version=%d,highmem=off" % gic + (",virtualization=on" if el2 else ""),
            *(["-accel", "hvf", "-cpu", "host"] if accel == "hvf" else ["-cpu", "cortex-a72"]),
            "-smp", str(smp), "-nic", "none", "-global", "virtio-mmio.force-legacy=false", "-m", ram]
        self.proc = subprocess.Popen(
            [qemu, *machine_args, *extra, "-kernel", kernel,
             "-serial", "stdio", "-display", "none",
             "-monitor", "unix:monitor.sock,server,nowait"],
            cwd=outdir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)

    def expect(self, pattern, timeout=30):
        """Wait until `pattern` (bytes regex) appears in the output; consume
        the output up to the end of the match."""
        regex = re.compile(pattern, re.MULTILINE)
        deadline = time.monotonic() + timeout
        while True:
            match = regex.search(self.pending)
            if match:
                self.pending = self.pending[match.end():]
                return match
            if DROPPED.search(self.pending):
                raise Failure("typing through the debug console failed:\n" + self.tail())
            if FAULT.search(self.pending):
                self.drain(0.5)  # the rest of the fault report
                raise Failure("kernel fault:\n" + self.tail())
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise Failure("timed out waiting for %r; last output:\n%s"
                              % (pattern, self.tail()))
            ready, _, _ = select.select([self.proc.stdout], [], [], remaining)
            if ready:
                data = os.read(self.proc.stdout.fileno(), 4096)
                if not data:
                    raise Failure("QEMU exited; last output:\n" + self.tail())
                self.log.write(data)
                self.log.flush()
                self.pending += data

    def drain(self, seconds):
        """Collect output for `seconds` more (after a fault, for its report)."""
        deadline = time.monotonic() + seconds
        while (remaining := deadline - time.monotonic()) > 0:
            ready, _, _ = select.select([self.proc.stdout], [], [], remaining)
            if not ready:
                break
            data = os.read(self.proc.stdout.fileno(), 4096)
            if not data:
                break
            self.log.write(data)
            self.log.flush()
            self.pending += data

    def send(self, text):
        """Type `text` on the keyboard, through the debug console's `type`
        (the serial line is the debug console): it goes to whoever takes the
        keys, the shell. '\\r' is Enter. Long text goes in a few commands, a
        little apart, so the input queue never fills."""
        chunks = [text[i:i + TYPE_CHUNK] for i in range(0, len(text), TYPE_CHUNK)]
        for n, chunk in enumerate(chunks):
            if n:
                time.sleep(0.05)
            self.debug("type " + type_escape(chunk))

    def key(self, *names):
        """Press keys by name (QEMU's sendkey names) through the debug console."""
        self.debug("key " + " ".join(names))

    def debug(self, command):
        """A command for the kernel debug console, on the serial line. With
        conlog on (the default under QEMU) it neither echoes nor prompts."""
        self.proc.stdin.write((command + "\r").encode())
        self.proc.stdin.flush()

    def tail(self, n=1500):
        return self.pending[-n:].decode(errors="replace")

    def monitor(self, command):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        cwd = os.getcwd()
        os.chdir(self.outdir)
        try:
            sock.connect("monitor.sock")
        finally:
            os.chdir(cwd)
        sock.settimeout(30)

        def until_prompt():
            data = b""
            while not data.endswith(b"(qemu) "):
                chunk = sock.recv(4096)
                if not chunk:
                    break
                data += chunk
            return data

        until_prompt()  # banner
        sock.sendall(command.encode() + b"\n")
        reply = until_prompt()
        sock.close()
        return reply

    def screendump(self, name):
        """Capture the framebuffer; save <name>.png; return (w, h, rgb)."""
        ppm = os.path.join(self.outdir, name + ".ppm")
        if os.path.exists(ppm):
            os.remove(ppm)
        self.monitor("screendump " + name + ".ppm")
        width, height, rgb = read_ppm(ppm)
        write_png(os.path.join(self.outdir, name + ".png"), width, height, rgb)
        os.remove(ppm)
        return width, height, rgb

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()
        self.log.close()
        sock = os.path.join(self.outdir, "monitor.sock")
        if os.path.exists(sock):
            os.remove(sock)


def read_ppm(path):
    for _ in range(100):  # wait for QEMU to finish writing it
        if os.path.exists(path) and os.path.getsize(path) > 0:
            break
        time.sleep(0.1)
    with open(path, "rb") as f:
        data = f.read()
    tokens = data.split(maxsplit=4)  # P6, width, height, maxval, pixels
    if tokens[0] != b"P6":
        raise Failure("unexpected screendump format")
    width, height = int(tokens[1]), int(tokens[2])
    rgb = data[len(data) - width * height * 3:]
    return width, height, rgb


def write_png(path, width, height, rgb):
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride]
                   for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def pixel(shot, x, y):
    width, _, rgb = shot
    i = (y * width + x) * 3
    return tuple(rgb[i:i + 3])


def near(actual, expected, tolerance=8):
    return all(abs(a - e) <= tolerance for a, e in zip(actual, expected))


PROMPT = rb"^\$ "
# The shell is KoraCLI: its console task `koracli` runs the interpreter,
# which waits for input and runs the commands.
SHELL_TASK = "micropython"


def koracli_os_mode(q, timeout=60):
    """KoraCLI has started (in Python mode): to its OS mode, where the steps
    type their commands, and its prompt. F4 goes through BASIC mode on the
    way, which says READY., and F2 from there to OS mode."""
    q.expect(rb"KoraCLI on MicroPython [0-9.]+\. F1 for help\.", timeout=timeout)
    q.expect(rb">>> ")
    time.sleep(0.3)
    q.key("f4")
    q.expect(rb"READY\.\r?\n")
    q.key("f2")
    q.expect(rb"\x1b\[J\$ ")  # the prompt redrawn in place (a new one starts a line)
TYPE_CHUNK = 32  # characters per `type` command: 64 events of the queue's 256
DROPPED = re.compile(rb"^(?:type|key|mouse): the input queue dropped \d+ events|"
                     rb"^(?:type|key): (?:no key|unknown|a broken)", re.M)


def type_escape(text):
    """`text` for the debug console's `type`: Enter, backslashes, control
    bytes and a leading space escaped."""
    out = []
    for i, c in enumerate(text):
        if c in "\r\n":
            out.append("\\n")
        elif c == "\\":
            out.append("\\\\")
        elif c == " " and i == 0:
            out.append("\\s")
        elif ord(c) < 0x20 or ord(c) >= 0x7f:
            out.append("\\x%02x" % ord(c))
        else:
            out.append(c)
    return "".join(out)


def ps_table(q, tids=False):
    """Run `ps` in the shell: [(pid, ppid, core, state, name)], one row per
    thread, prompt consumed; with tids, [(pid, tid, ppid, core, state, name)]."""
    q.send("ps\r")
    q.expect(rb"^\s+PID\s+TID\s+PPID\s+CPU\s+STATE\s+NAME")
    rows = []
    while True:
        m = q.expect(rb"^\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\w+)\s+(\S+)\r?\n|"
                     rb"^\[\d+\] exited with 0")
        if m.group(1) is None:
            break
        pid, tid, ppid, core = (int(g) for g in m.groups()[:4])
        state, name = m.group(5).decode(), m.group(6).decode()
        rows.append((pid, tid, ppid, core, state, name) if tids else
                    (pid, ppid, core, state, name))
    q.expect(PROMPT)
    return rows


def core_of(q, name):
    for _, _, core, _, task in ps_table(q):
        if task == name:
            return core
    raise Failure("ps does not list %s" % name)


def sd_fixture():
    """tests/create-sd-card-image.py, for the expected card contents."""
    spec = importlib.util.spec_from_file_location("sd_card", os.path.join(ROOT, "tests/create-sd-card-image.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def storage_steps(q, what, label, writable, cores):
    """Steps for a scratch drive from tests/create-sd-card-image.py (an SD card or
    a USB drive) whose FAT32 volume is labelled `label`."""
    # FAT32 writes go one sector per request, and in QEMU a USB command takes
    # several full-speed frames: copying BIG.BIN to a USB drive takes a minute.
    timeout = 300 if what.startswith("USB") else 30
    card = sd_fixture()
    big = card.big_content()
    sums = {"README.TXT": (len(card.MARKER), card.fnv1a64(card.MARKER.encode())),
            "BIG.BIN": (len(big), card.fnv1a64(big))}
    device = []

    def check_sums(names, core=None):
        """sumprobe each drive file in `names` (path: expected file name), on
        `core` if given."""
        q.send("sumprobe %s%s\r" % (" ".join("%s:/%s" % (device[0], path) for path in names),
                                    "" if core is None else " @%d" % core))
        for path, expected in names.items():
            m = q.expect(rb"^sumprobe: \S+ (?:(\d+) ([0-9a-f]{16})|unreadable)\r?\n", timeout)
            if m.group(1) is None:
                raise Failure("cannot read %s:/%s from the %s" % (device[0], path, what))
            got = (int(m.group(1)), int(m.group(2), 16))
            if got != sums[expected]:
                raise Failure("%s:/%s from the %s: size/checksum %s, want %s"
                              % (device[0], path, what, got, sums[expected]))
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)

    def volume():
        q.send("volumes\r")
        # The boot volume, without a ramdisk, may be this drive's.
        m = q.expect(rb"^(df\d+): " + label.lower().encode() + rb"(?: \[boot\])?( \[read-only\])?\r?\n")
        q.expect(PROMPT)
        device.append(m.group(1).decode())
        if (m.group(2) is None) != writable:
            raise Failure("%s volume is %s" % (what, "writable" if m.group(2) is None else "read-only"))
        # Multi-block reads of an odd-sized file, compared with the host's bytes:
        # from core 0, where USB interrupts arrive, and from the last core.
        check_sums({"README.TXT": "README.TXT", "BIG.BIN": "BIG.BIN"}, 0)
        if cores > 1:
            check_sums({"BIG.BIN": "BIG.BIN"}, cores - 1)
        # A program loaded straight off the drive.
        q.send("%s:/bin/hello\r" % device[0])
        q.expect(rb"^Hello from userland \(ELF\)")
        q.expect(PROMPT)
    yield "%s: its FAT32 volume, multi-block reads and a program from it" % what, volume

    def writes():
        if not writable:
            q.send("mkdir %s:/NOPE\r" % device[0])
            q.expect(rb"mkdir: cannot create")
            q.expect(PROMPT)
            return
        for command in ("cp %s:/BIG.BIN %s:/COPY.BIN", "mkdir %s:/NEWDIR",
                        "cp %s:/README.TXT %s:/NEWDIR/NOTE.TXT", "sync"):
            q.send((command % ((device[0],) * command.count("%s"))) + "\r")
            if command != "sync":
                q.expect(rb"\] exited with 0", timeout)
            q.expect(PROMPT, timeout)
        check_sums({"COPY.BIN": "BIG.BIN", "NEWDIR/NOTE.TXT": "README.TXT"})
    yield ("%s: writes, verified on the host afterwards" % what if writable else
           "%s: read-only unless built --storage-writable" % what), writes


def kernel_steps(q, cores):
    """The steps that need only the kernel and its debug console, not a shell."""

    def debug_console():
        # The serial line is the debug console; conlog is on, so it answers
        # without echo or prompt, and the consoles' text comes along.
        q.debug("version")
        q.expect(rb"^KoraOS version \d")
        q.debug("conlog")
        q.expect(rb"^conlog: on")
    yield "the serial line is the kernel debug console, with conlog on", debug_console

    def irqs():
        deadline = time.monotonic() + 5
        while True:
            q.debug("irqs")
            q.expect(rb"^controller: (?:BCM2835 legacy|GIC-400|GICv[23] \(virt\))")
            m = q.expect(rb"^systick: (\d+) ticks, uptime (\d+)\.(\d+) s")
            ticks = int(m.group(1))
            uptime = int(m.group(2)) + int(m.group(3)) / 1000
            if ticks > uptime * 100 + 1:
                raise Failure("systick %d ticks after %.3f s uptime" % (ticks, uptime))
            m = q.expect(rb"^\s+\d+\s+(\d+)\s+uart \(PL011\)")
            if int(m.group(1)) == 0:
                raise Failure("the UART receive interrupt never fired")
            if ticks >= 5:
                break
            if time.monotonic() >= deadline:
                raise Failure("system tick never advanced to five interrupts")
            time.sleep(0.05)
    yield "irqs shows the system tick and the UART interrupt", irqs

    def per_core_ticks():
        def read_cpus():
            q.debug("cpus")
            ticks = {}
            while True:
                m = q.expect(rb"^\s+(\d+)\s+(online|offline)(?:\s+(\d+)\s+(\S+))?\r?\n|"
                             rb"^cpus: (\d+) online, this is cpu (\d+)")
                if m.group(5):
                    if int(m.group(5)) != cores or m.group(6) != b"0":
                        raise Failure("cpus reports %s online on cpu %s, want %d on cpu 0"
                                      % (m.group(5).decode(), m.group(6).decode(), cores))
                    break
                if m.group(2) == b"online":
                    ticks[int(m.group(1))] = (int(m.group(3)), m.group(4).decode())
            return ticks
        first = read_cpus()
        time.sleep(0.3)
        second = read_cpus()
        if sorted(second) != list(range(cores)):
            raise Failure("online cores %s, want 0..%d" % (sorted(second), cores - 1))
        for core in range(cores):
            if second[core][0] <= first[core][0]:
                raise Failure("cpu %d's own timer did not tick (%d -> %d)"
                              % (core, first[core][0], second[core][0]))
            if core and second[core][1] != "idle%d" % core:
                raise Failure("cpu %d runs %s, want its idle task" % (core, second[core][1]))
    yield "every core is online with its own timer ticking", per_core_ticks

    def kernel_heap():
        def heap_stats():
            q.debug("heap")
            m = q.expect(rb"^heap: (\d+) live, \d+ allocated, (\d+) failed, (\d+) bad frees")
            live, failed, bad = (int(g) for g in m.groups())
            m = q.expect(rb"^pages: (\d+) of (\d+) free")
            return live, failed, bad, int(m.group(1))

        live, failed, bad, pages = heap_stats()
        for _ in range(4):  # each run seeds from the uptime
            q.debug("heaptest 3000")
            m = q.expect(rb"^heaptest: (?:3000 rounds ok|FAILED check (\d+))", timeout=60)
            if m.group(1):
                raise Failure("heaptest failed check %s" % m.group(1).decode())
        live2, failed2, bad2, pages2 = heap_stats()
        if (live2, failed2, bad2) != (live, failed, bad):
            raise Failure("heap changed across heaptest: live/failed/bad %s -> %s"
                          % ((live, failed, bad), (live2, failed2, bad2)))
        if bad:
            raise Failure("%d bad kfree calls since boot" % bad)
        # At most one cached empty slab per size class may stay behind.
        if pages2 < pages - 9:
            raise Failure("heaptest leaked pages: %d -> %d free" % (pages, pages2))
    yield "kernel heap stress leaves no leaks", kernel_heap


def run(q, graphics=True, keyboard=None, repeat=0, multi_volume=False, write_test=False, cores=1,
        stress=0, media=(), mouse=None):
    """Yield (name, check) steps; each check raises Failure on error. `keyboard`
    names the keyboard to type on ("VirtIO" or "USB"), if any; `mouse` the
    mouse ("VirtIO", with a tablet too, or "USB"). `media` lists
    the attached scratch drives as (what, volume label, writable) tuples."""

    if not graphics:
        def no_shell():
            q.expect(rb"^init: no display, so no shell; the serial line is the debug console", timeout=90)
            q.expect(rb"^init \(pid \d+\) exited with code 0")
        yield "without a display, init starts no shell", no_shell
        yield from kernel_steps(q, cores)
        return

    def boot():
        # init runs the shell, KoraCLI, under the screen manager: its console
        # task opens a screen and a console there, and runs the interpreter
        # on it. The steps type in its OS mode.
        q.expect(rb"^init: starting the shell \(screens koracli\)", timeout=90)
        q.expect(rb"^screens: \d+x\d+, Meta\+M brings the next screen to front")
        q.expect(rb"^screens: screen \d+ in front \(koracli\)")
        koracli_os_mode(q, timeout=90)
    yield "boots to the shell (KoraCLI), on a screen of its own", boot

    for what, label, writable in media:
        yield from storage_steps(q, what, label, writable, cores)

    def ls():
        q.send("ls /bin\r")
        for name in (b"hello", b"koracli", b"termdemo"):
            q.expect(rb"^" + name + rb"\r?$")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield "ls /bin lists the programs", ls

    def hello():
        q.send("hello\r")
        q.expect(rb"^Hello from userland \(ELF\)")
        q.expect(PROMPT)
    yield "hello runs", hello

    def echo():
        q.send("echo alpha beta\r")
        q.expect(rb"^alpha beta\r?$")
        q.expect(rb"exited with 2")  # echo exits with its argument count
        q.expect(PROMPT)
    yield "echo gets argv and exit code", echo

    def cat():
        q.send("cat /docs/a-long-file-name.txt\r")
        q.expect(rb"long-filename \(LFN\)")
        q.expect(PROMPT)
        # With no path it copies the console's lines until Ctrl+D: the line
        # typed comes back once as typed (conlog) and once as cat's.
        q.send("cat\r")
        time.sleep(0.2)
        q.send("copied line\r")
        q.expect(rb"^copied line\r?\n(?:[^\r\n]*\r?\n)*?copied line\r?$")
        q.key("ctrl-d")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield "cat reads a long-filename file, and alone copies the console until Ctrl+D", cat

    def namespace():
        q.send("nsprobe%s\r" % (" extras" if multi_volume else ""))
        q.expect(rb"nsprobe: child cwd checked")
        if multi_volume:
            q.expect(rb"nsprobe: multi-volume checked")
        q.expect(rb"nsprobe: namespace checked")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield "volume paths, cwd inheritance and retained file descriptors", namespace

    def shell_navigation():
        def command(text):
            q.send(text + "\r")
            q.expect(PROMPT)

        def pwd(expected):
            q.send("pwd\r")
            q.expect(rb"^" + re.escape(expected) + rb"\r?$")
            q.expect(PROMPT)

        q.send("pwd\r")
        root = q.expect(rb"^([a-zA-Z0-9_-]+:)\r?$").group(1)
        q.expect(PROMPT)
        q.send("echo" + " a" * 16 + "\r")
        q.expect(rb"koracli: too many arguments")
        q.expect(PROMPT)
        q.send("echo accepted\r")
        q.expect(rb"^accepted\r?$")
        q.expect(rb"exited with 1")
        q.expect(PROMPT)
        command("cd docs")
        pwd(root + b"docs")
        q.send("ls\r")
        q.expect(rb"^a-long-file-name\.txt\r?$")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
        command("cd missing/..")
        pwd(root + b"docs")
        command("cd ..")
        pwd(root)
        command("cd /docs")
        pwd(root + b"docs")
        command(root.decode())
        pwd(root)
        q.send("volumes\r")
        q.expect(rb"^df[0-9]+: [^\r\n]*\[boot\]")
        if multi_volume:
            q.expect(rb"^df[0-9]+: extras[^\r\n]*\[read-only\]")
        q.expect(PROMPT)
        q.send("assign\r")
        q.expect(rb"^sys: = [^\r\n]*\[fixed\]")
        q.expect(rb"^c: = [^\r\n]*:bin")
        q.expect(PROMPT)
        command("SYS:")
        pwd(root)
        if multi_volume:
            command("ExTrAs:")
            pwd(b"extras:")
            command("cd docs")
            pwd(b"extras:docs")
            command("cd /")
            pwd(b"extras:")
            command("cd sys:docs")
            pwd(root + b"docs")
            command("assign c extras:bin")
            q.send("extrahello\r")
            q.expect(rb"^Hello from userland \(ELF\)")
            q.expect(rb"exited with 0")
            q.expect(PROMPT)
            q.send("c:/hello\r")
            q.expect(rb"^Hello from userland \(ELF\)")
            q.expect(rb"exited with 0")
            q.expect(PROMPT)
            command("assign c sys:docs")
            q.send("hello\r")
            q.expect(rb"koracli: no such program: hello")
            q.expect(PROMPT)
            command("assign c")
            q.send("hello\r")
            q.expect(rb"koracli: no such program: hello")
            q.expect(PROMPT)
            command("assign c sys:bin")
            command("sys:")
            pwd(root)
    yield "shell navigation, volume listing and single-target assigns", shell_navigation

    if write_test:
        def writable_filesystem():
            def tool(command, status=0):
                q.send(command + "\r")
                q.expect(rb"exited with " + str(status).encode() + rb"\r?$")
                q.expect(PROMPT)

            q.send("writeprobe\r")
            q.expect(rb"writeprobe: data, namespace, UTF-8 and sync checked", timeout=60)
            q.expect(rb"exited with 0")
            q.expect(PROMPT)
            tool('mkdir "write cli"')
            tool('cp sys:README.TXT "write cli/copy with spaces.txt"')
            tool("mv 'write cli/copy with spaces.txt' \"write cli/renamed copy.txt\"")
            tool('cp "write cli/renamed copy.txt" "quoted copy.txt"')
            tool('cp sys:README.TXT sys:README.TXT', 1)
            tool('mv "quoted copy.txt" sys:README.TXT', 1)
            tool('rm write\\ cli/renamed\\ copy.txt')
            tool('rmdir "write cli"')
            q.send("sync\r")
            result = q.expect(rb"(^sync: failed|^\$ )")
            if result.group(1) == b"sync: failed":
                raise Failure("explicit sync failed")
        yield "EL0 writes, UTF-8 paths, quoted file tools and sync", writable_filesystem

        def libc_files():
            q.send("libcprobe files\r")
            m = q.expect(rb"^libcprobe: ([^\r\n]*)\r?\n", timeout=60)
            if m.group(1) != b"files checked":
                raise Failure("libcprobe files: %s" % m.group(1).decode(errors="replace"))
            q.expect(rb"exited with 0")
            q.expect(PROMPT)
        yield "libc file streams: write, read back, append, update, seek, rename, remove", libc_files

        def redirection():
            # The shell gives a program files for its stdin and stdout; the
            # program's libc finds out at startup (fully buffered, not a tty).
            def run(command, status=0):
                q.send(command + "\r")
                q.expect(rb"exited with %d\r?$" % status)
                q.expect(PROMPT)

            def file_is(path, lines):
                q.send("cat %s\r" % path)
                for line in lines:
                    q.expect(rb"^" + re.escape(line) + rb"\r?$")
                q.expect(rb"exited with 0\r?$")
                q.expect(PROMPT)

            run("echo redirected words > RD.TXT", 2)  # echo exits with its argument count
            file_is("RD.TXT", [b"redirected words"])
            run("echo appended >>RD.TXT", 1)
            file_is("RD.TXT", [b"redirected words", b"appended"])
            q.send("cat < RD.TXT\r")  # with no path: stdin, here a file
            q.expect(rb"^redirected words\r?$")
            q.expect(rb"^appended\r?$")
            q.expect(rb"exited with 0\r?$")
            q.expect(PROMPT)
            q.send("libcprobe handles\r")
            q.expect(rb"^libcprobe: isatty 1 1 1, stdin EOF\r?$")
            q.expect(rb"^inherited\r?$")
            q.expect(rb"exited with 0\r?$")
            q.expect(PROMPT)
            # Its child echo inherits the file, after the parent's line.
            run("libcprobe handles < RD.TXT > HANDLES.TXT")
            file_is("HANDLES.TXT", [b"libcprobe: isatty 0 0 1, stdin redirected words", b"inherited"])
            run("rm RD.TXT HANDLES.TXT")
        yield ("shell redirection: > and >> to a file, < from one (cat too), isatty, inherited by a "
               "child"), redirection

    yield from kernel_steps(q, cores)

    def fp_context():
        q.send("fpprobe\r")
        m = q.expect(rb"^fpprobe: ([^\r\n]*)\r?\n")
        if m.group(1) != b"registers preserved":
            raise Failure("fpprobe: %s" % m.group(1).decode(errors="replace"))
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield "FP/SIMD registers are per task (lazy switching)", fp_context

    def libc_on_target():
        q.send("libcprobe\r")
        m = q.expect(rb"^libcprobe: ([^\r\n]*)\r?\n")
        if m.group(1) != b"checks passed":
            raise Failure("libcprobe: %s" % m.group(1).decode(errors="replace"))
        q.expect(rb"^libcprobe: atexit ran\r?\n")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield "libc on the target (floats, strtod, snprintf va_list, stdio flushed at exit)", libc_on_target

    def stack_sizes():
        # stackprobe declares 64 KB at link time; the kernel says what it gave.
        q.send("stackprobe\r")
        q.expect(rb"run 'stackprobe' on cpu \d+: [^\r\n]*\(\d+ image pages, 16 stack\)")
        m = q.expect(rb"^stackprobe: ([^\r\n]*)\r?\n")
        if m.group(1) != b"main 64 KB, thread 200 KB, default 16 KB":
            raise Failure("stackprobe: %s" % m.group(1).decode(errors="replace"))
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
        # More than the kernel allows: the spawn fails, and says why.
        q.send("stackbig\r")
        q.expect(rb"spawn: 'stackbig' asks for a 2048 KB stack, more than 1024 KB")
        q.expect(rb"koracli: no such program: stackbig")
        q.expect(PROMPT)
        # A program that declares nothing keeps its one page.
        q.send("hello\r")
        q.expect(rb"run 'hello' on cpu \d+: [^\r\n]*\(\d+ image pages, 1 stack\)")
        q.expect(PROMPT)
    yield "stacks declared at link time, for a program and its threads", stack_sizes

    def read_tasks():
        """Run `tasks` on the debug console: ({name: (state, stack)}, peak, size)."""
        listed, peak, size, _ = read_tasks_bkl()
        return {name: info[:2] for name, info in listed.items()}, peak, size

    def read_tasks_bkl():
        """`tasks` (tid, pid, cpu, state, stack, name) with the forbid flag and the
        BKL holder: ({name: (state, stack, forbidden)}, peak, size, holder pid
        or None)."""
        # The shell's prompt, which ends in no newline, may come from another
        # core just before one of the table's lines.
        q.debug("tasks")
        listed = {}
        while True:
            m = q.expect(rb"^(?:\$ )*(?:\s+(\d+)\s+\d+\s+(\d+)\s+(\w+)\s+(\d+)\s+(\S+)( \(forbid\))?"
                         rb"(?: \(killed\))?|kernel stack peak: (\d+) of (\d+) bytes)\r?\n")
            if m.group(7):
                peak, size = int(m.group(7)), int(m.group(8))
                break
            listed[m.group(5).decode()] = (m.group(3).decode(), int(m.group(4)), m.group(6) is not None)
        m = q.expect(rb"^(?:\$ )*big kernel lock: (free|pid (\d+))\r?\n")
        holder = int(m.group(2)) if m.group(2) else None
        return listed, peak, size, holder

    def tasks():
        # The shell sleeps in read() until input arrives; it does not spin. (A
        # few retries cover the moment between printing its prompt and reading.)
        # With everyone asleep, nobody holds the big kernel lock.
        for _ in range(5):
            listed, peak, size, holder = read_tasks_bkl()
            if (listed.get("init", ("",))[0] == "blocked" and
                    listed.get(SHELL_TASK, ("",))[0] == "blocked" and holder is None):
                break
            time.sleep(0.2)
        else:
            raise Failure("expected init and the shell blocked and the BKL free, got %s, "
                          "holder %s" % (listed, holder))
        # Every task so far, the probes included: keep a quarter of the stack spare.
        if peak == 0 or peak > size * 3 // 4:
            raise Failure("kernel stack peak %d of %d bytes" % (peak, size))
    yield "tasks: init and the shell sleep; kernel stacks have headroom", tasks

    def scheduling():
        q.send("schedprobe sleep 300\r")
        m = q.expect(rb"^schedprobe: slept (\d+) ms")
        slept = int(m.group(1))
        if not 300 <= slept <= 1300:
            raise Failure("msleep(300) took %d ms" % slept)
        q.expect(rb"exited with 0")
        q.expect(PROMPT)

        # A background job that never makes a syscall, on the shell's own core:
        # the shell only answers while it runs if the timer tick preempts it.
        shell_core = core_of(q, SHELL_TASK)
        q.send("schedprobe spin 5000 @%d &\r" % shell_core)
        m = q.expect(rb"^\[(\d+)\] started")
        pid = m.group(1)
        q.expect(PROMPT)
        q.send("echo alive\r")
        # "alive" anchored (the echoed command line contains it too); the
        # background job's line may follow a prompt.
        m = q.expect(rb"^alive|schedprobe: spun")
        if m.group(0) != b"alive":
            raise Failure("the shell did not run while a background job was spinning")
        q.expect(PROMPT)
        # The shell may be asleep in read() or, preempted between its prompt and
        # that read, waiting for its next slice; the spinner is always runnable.
        listed, _, _ = read_tasks()
        if listed.get("schedprobe", ("",))[0] != "runnable" or SHELL_TASK not in listed:
            raise Failure("expected a runnable schedprobe next to the shell, got %s" % listed)
        q.expect(rb"schedprobe: spun \d+ ms", timeout=60)
        # The job exits just after printing; the first prompt after that
        # reports it.
        for _ in range(50):
            q.send("\r")
            m = q.expect(rb"^(?:\[" + pid + rb"\] done, exit (\d+)\r?\n)?\$ ")
            if m.group(1) is not None:
                if m.group(1) != b"0":
                    raise Failure("background job exited with %s" % m.group(1).decode())
                break
            time.sleep(0.1)
        else:
            raise Failure("the shell never reported the finished background job")
    yield "msleep, background jobs and preemption of a spinning task", scheduling

    def forbid_permit():
        # With a spinner competing, a normal task loses the CPU for whole time
        # slices (10 ms); while forbid()den it must not lose it at all. Host
        # scheduling noise can stretch a gap, so allow a few attempts.
        # Both on the shell's core, so they compete for it.
        core = core_of(q, SHELL_TASK)
        q.send("schedprobe spin 8000 @%d &\r" % core)
        m = q.expect(rb"^\[(\d+)\] started")
        spinner = m.group(1)
        q.expect(PROMPT)
        results = []
        for _ in range(3):
            q.send("forbidprobe @%d\r" % core)
            m = q.expect(rb"^forbidprobe: (?:longest gap free (\d+) us, forbidden (\d+) us|([^\r\n]*))\r?\n",
                         timeout=60)
            if m.group(3) is not None:
                raise Failure("forbidprobe: %s" % m.group(3).decode(errors="replace"))
            q.expect(rb"exited with 0")
            q.expect(PROMPT)
            free_gap, forbidden_gap = int(m.group(1)), int(m.group(2))
            results.append((free_gap, forbidden_gap))
            if free_gap >= 5000 and forbidden_gap < 5000:
                break
        else:
            raise Failure("forbid() did not keep the CPU (free, forbidden gaps in us): %s" % results)
        q.expect(rb"schedprobe: spun \d+ ms", timeout=60)

        # Background output may follow the shell's prompt on the same line, so
        # the background jobs' lines are matched unanchored.
        # While a task holds forbid(), `tasks` (in the UART interrupt) shows it
        # holding the BKL; the shell cannot run until it permits.
        # Its line goes to the console as a message, and the probe waits for
        # the reply with its forbid broken: it holds the BKL again only once
        # it runs on.
        q.send("forbidprobe hold 3000 &\r")
        q.expect(rb"forbidprobe: holding")
        for _ in range(20):
            listed, _, _, holder = read_tasks_bkl()
            if listed.get("forbidprobe", ("", 0, False))[2] and holder is not None:
                break
            time.sleep(0.05)
        else:
            raise Failure("forbidden task or BKL holder not shown: %s, holder %s" % (listed, holder))
        q.expect(rb"forbidprobe: released", timeout=60)
        for _ in range(50):
            q.send("\r")
            m = q.expect(rb"^(?:\[(\d+)\] done, exit (\d+)\r?\n)?\$ ")
            if m.group(1) is not None and m.group(1) != spinner:
                break
            time.sleep(0.1)
        # Nobody keeps the BKL once the system is idle. (The shell, on its own
        # core, may be caught in a syscall: look again.)
        for _ in range(10):
            _, _, _, holder = read_tasks_bkl()
            if holder is None:
                break
            time.sleep(0.2)
        else:
            raise Failure("the BKL is still held (by pid %s) with the system idle" % holder)
    yield "forbid()/permit(): nesting, no preemption, BKL held across EL0", forbid_permit

    def serial_lines_whole():
        # Threads on every core print a line per tick while the debug console
        # prints tables on core 0: whole writes to the serial line are atomic,
        # so no line may come out split by another core's output.
        start = os.path.getsize(q.log.name)
        q.send("threadprobe chatter 3000 &\r")
        q.expect(rb"threadprobe: chatter \d+ line \d+ end")
        for _ in range(6):
            read_tasks_bkl()
        q.expect(rb"threadprobe: chatter done", timeout=60)
        for _ in range(50):  # reap it, so later steps see a quiet system
            q.send("\r")
            m = q.expect(rb"^(?:\[\d+\] done, exit (\d+)\r?\n)?\$ ")
            if m.group(1) is not None:
                break
            time.sleep(0.1)
        with open(q.log.name, "rb") as log:
            log.seek(start)
            text = log.read()
        # A prompt prints no newline, so a whole line may follow one; so may
        # the `tasks` command being typed, which the console echoes keystroke
        # by keystroke as it arrives. A line from another core can land
        # anywhere in that echo, also after one that already took the
        # prompt's line, so a whole line may follow any piece of "tasks".
        typed = b"tasks"
        pieces = sorted({typed[i:j] for i in range(len(typed)) for j in range(i + 1, len(typed) + 1)},
                        key=len, reverse=True)
        prompts = rb"^(?:\$ |koraos> |" + b"|".join(pieces) + rb")*"
        chatter = re.compile(prompts + rb"threadprobe: chatter (?:\d+ line \d+ end|done)\r?$")
        console = re.compile(prompts + rb"(?:big kernel lock: (?:free|pid \d+)|"
                             rb"kernel stack peak: \d+ of \d+ bytes)\r?$")
        lines = text.split(b"\n")
        broken = [line for line in lines
                  if (b"threadprobe: chatter" in line and not chatter.match(line)) or
                  ((b"big kernel lock" in line or b"kernel stack peak" in line) and not console.match(line))]
        if broken or sum(1 for line in lines if chatter.match(line)) < 20:
            raise Failure("serial lines split by another core's output: %r" % broken[:5])
    yield "serial output from several cores stays whole, line by line", serial_lines_whole

    def reap(count):
        """Wait until the shell has reaped every finished schedprobe job (each
        prompt reports and reaps finished jobs; `ps` shows any left, zombies
        included). `count` is only for the error message."""
        for _ in range(100):
            if not any(name == "schedprobe" for *_, name in ps_table(q)):
                return
            time.sleep(0.1)
        raise Failure("%d background jobs were never reaped: %s" % (count, ps_table(q)))

    def spin_gaps(placements, ms=1500, after=1500):
        """Run one spinner per @core (None: unpinned), wait, return their
        longest gaps in us. They spin `after` ms after they start, once every
        command has been typed: the shell, its console and the screen
        manager, which handle the typing, run on cores of their own."""
        for core in placements:
            q.send("schedprobe spin %d %d%s &\r" % (ms, after, "" if core is None else " @%d" % core))
            q.expect(rb"^\[\d+\] started")
            q.expect(PROMPT)
        gaps = [int(q.expect(rb"schedprobe: spun \d+ ms, longest gap (\d+) us", timeout=60).group(1))
                for _ in placements]
        reap(len(placements))
        return gaps

    def placement_and_pinning():
        rows = ps_table(q)
        names = {name: core for _, _, core, _, name in rows}
        if not {"init", "koracli", SHELL_TASK, "ps"} <= set(names):
            raise Failure("ps lists %s" % rows)
        if any(core >= cores for core in names.values()):
            raise Failure("ps shows a core outside 0..%d: %s" % (cores - 1, rows))
        if cores == 1:
            q.send("echo hi @1\r")
            q.expect(rb"koracli: cannot run echo on cpu 1")
            q.expect(PROMPT)
            return
        # Pinned: the task runs where it was put.
        last = cores - 1
        q.send("schedprobe spin 2000 @%d &\r" % last)
        q.expect(rb"^\[\d+\] started")
        q.expect(PROMPT)
        if core_of(q, "schedprobe") != last:
            raise Failure("schedprobe @%d runs elsewhere: %s" % (last, ps_table(q)))
        q.expect(rb"schedprobe: spun \d+ ms", timeout=60)
        reap(1)
        # Unpinned: two jobs go to different (least-loaded) cores.
        for _ in range(2):
            q.send("schedprobe spin 2000 &\r")
            q.expect(rb"^\[\d+\] started")
            q.expect(PROMPT)
        spinners = [core for _, _, core, _, name in ps_table(q) if name == "schedprobe"]
        if len(spinners) != 2 or spinners[0] == spinners[1]:
            raise Failure("two unpinned jobs landed on cores %s" % spinners)
        for _ in spinners:
            q.expect(rb"schedprobe: spun \d+ ms", timeout=60)
        reap(2)
        # Sharing a core, two spinners lose whole slices to each other; on
        # separate cores they run side by side. (Host scheduling noise can
        # stretch a gap on a separate core, hence a few attempts.)
        shared = spin_gaps([last, last])
        if min(shared) < 5000:
            raise Failure("two spinners on one core did not time-slice: gaps %s us" % shared)
        for _ in range(3):
            apart = spin_gaps([last - 1, last])
            if max(apart) < 5000:
                break
        else:
            raise Failure("spinners on cores %d and %d still lost slices: gaps %s us"
                          % (last - 1, last, apart))
    yield "ps, placement on the least-loaded core, @N pinning and parallel cores", placement_and_pinning

    def kernel_counts():
        """(live kernel heap allocations, free pages) from the debug console."""
        q.debug("heap")
        m = q.expect(rb"^heap: (\d+) live")
        live = int(m.group(1))
        m = q.expect(rb"^pages: (\d+) of \d+ free")
        return live, int(m.group(1))

    def probe(command, ok, code=0, timeout=60):
        """Run a threadprobe mode in the foreground: its result line must match
        `ok` (else its FAILED line is reported), then it exits with `code`."""
        q.send("threadprobe %s\r" % command)
        m = q.expect(rb"^threadprobe: (?:(" + ok + rb")|FAILED ([^\r\n]*))\r?\n", timeout=timeout)
        if m.group(2) is not None:
            raise Failure("threadprobe %s: %s" % (command, m.group(2).decode(errors="replace")))
        m = q.expect(rb"\] exited with (-?\d+)", timeout=timeout)
        if int(m.group(1)) != code:
            raise Failure("threadprobe %s exited with %s, want %d" % (command, m.group(1).decode(), code))
        q.expect(PROMPT)

    def threads():
        probe("join %d" % cores, rb"joined %d threads on %d cores, results ok" % (2 * cores, cores))
        probe("jobs %d" % cores, rb"8 jobs on %d cores, results ok" % cores)
        probe("malloc %d" % cores, rb"malloc from %d threads ok" % cores, timeout=120)
    yield "threads on every core: create/join, libk jobs, one malloc heap", threads

    def thread_lifetime():
        def both():
            # exit() from main ends threads that spin on every core or block
            # in msleep, a console read and a join.
            probe("exit %d" % cores, rb"exiting with 7", code=7)
            # The program lives on until its last thread is done.
            q.send("threadprobe last\r")
            q.expect(rb"threadprobe: main thread exits")
            q.expect(rb"threadprobe: last thread done")
            m = q.expect(rb"\] exited with (-?\d+)")
            if m.group(1) != b"0":
                raise Failure("threadprobe last exited with %s, want 0" % m.group(1).decode())
            q.expect(PROMPT)
            left = [row for row in ps_table(q) if row[-1] == "threadprobe"]
            if left:
                raise Failure("threadprobe tasks outlived their program: %s" % left)
        both()  # warms every cache
        before = kernel_counts()
        both()
        after = kernel_counts()
        if after != before:
            raise Failure("kernel heap/pages (live, free) %s before, %s after the threaded "
                          "programs ended" % (before, after))
    yield "process lifetime: exit() kills every thread, the last thread ends it, nothing leaks", thread_lifetime

    def kill_job():
        q.send("kill 99999\r")
        q.expect(rb"kill: no such program: 99999")
        q.expect(PROMPT)
        q.send("threadprobe hang %d &\r" % cores)
        # The job's own line can come before or after the shell's.
        pid = None
        for _ in range(2):
            m = q.expect(rb"^\[(\d+)\] started|threadprobe: hanging")
            if m.group(1) is not None:
                pid = int(m.group(1))
        rows = [row for row in ps_table(q, tids=True) if row[-1] == "threadprobe"]
        tids = {row[1] for row in rows}
        if (len(rows) != cores + 1 or {row[0] for row in rows} != {pid} or len(tids) != len(rows)
                or pid not in tids or not set(range(cores)) <= {row[3] for row in rows}):
            raise Failure("ps shows threadprobe as %s, want %d threads of pid %d on cores 0..%d"
                          % (rows, cores + 1, pid, cores - 1))
        # Its threads die on their own cores within a tick or so, and a prompt
        # then reports the job: maybe the one right after kill (on one core
        # the woken threads run before the shell does), maybe a later one.
        q.send("kill %d\r" % pid)
        for _ in range(50):
            m = q.expect(rb"^\[%d\] done, exit (-?\d+)|^\$ " % pid)
            if m.group(1) is not None:
                break
            time.sleep(0.1)
            q.send("\r")
        else:
            raise Failure("killed job %d never finished: %s" % (pid, ps_table(q, tids=True)))
        if m.group(1) != b"-9":
            raise Failure("killed job %d exited with %s, want -9" % (pid, m.group(1).decode()))
        q.expect(PROMPT)
        left = [row for row in ps_table(q) if row[-1] == "threadprobe"]
        if left:
            raise Failure("threadprobe tasks outlived kill: %s" % left)
    yield "kill ends a multi-threaded background job on every core", kill_job

    def message_ports():
        # portprobe runs children of its own and passes messages between them
        # (ipc/port.h); each case prints a line, any failure a FAIL line.
        q.send("portprobe\r")
        for case in (rb"\d+ round trips and \d+ pipelined requests",
                     rb"an ended sender lasts until its held message is replied",
                     rb"a receiver that ends returns what it held",
                     rb"a waiter can be killed",
                     rb"all passed"):
            m = q.expect(rb"^portprobe: (?:(" + case + rb")|FAIL ([^\r\n]*))\r?\n", timeout=60)
            if m.group(2) is not None:
                raise Failure("portprobe: %s" % m.group(2).decode(errors="replace"))
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield "message ports between processes: round trips, ended senders and receivers, kill", message_ports

    def signals():
        # sigprobe: signal bits alone, between threads, from a port and a
        # child's end (one wait for both, as a console handler waits), kill.
        q.send("sigprobe\r")
        for case in (rb"allocation, set, clear and wait",
                     rb"a thread wakes another",
                     rb"\d+ messages and a child's end on two signals",
                     rb"a waiter can be killed",
                     rb"all passed"):
            m = q.expect(rb"^sigprobe: (?:(" + case + rb")|FAIL ([^\r\n]*))\r?\n", timeout=60)
            if m.group(2) is not None:
                raise Failure("sigprobe: %s" % m.group(2).decode(errors="replace"))
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield "signals: bits, threads, a port and a child's end in one wait, kill", signals

    def screen_manager_claims():
        # inputprobe: what a screen manager takes from the kernel. A signal at
        # any process's end, the input queue and the screen, each claimed,
        # refused to a second program and taken back at its holder's end. It
        # borrows the display and the keyboard from the screen manager first,
        # and returns them at the end.
        start = os.path.getsize(q.log.name)
        q.send("inputprobe\r")
        cases = [rb"a signal at any process's end",
                 rb"the input queue, claimed and given back"]
        cases += [rb"printed while the screen is claimed",
                  rb"the screen, claimed and given back"]
        for case in cases + [rb"all passed"]:
            m = q.expect(rb"^inputprobe: (?:(" + case + rb")|FAIL ([^\r\n]*))\r?\n", timeout=60)
            if m.group(2) is not None:
                raise Failure("inputprobe: %s" % m.group(2).decode(errors="replace"))
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
        with open(q.log.name, "rb") as log:
            log.seek(start)
            text = log.read()
        for line in (rb"^screens: the display and the keyboard lent to pid \d+",
                     rb"^screens: the display and the keyboard back, returned"):
            if not re.search(line, text, re.M):
                raise Failure("the screen manager did not say %r" % line)
        # The probe painted the screen red while it held it: the shell's
        # screen is drawn again when the manager takes the display back.
        width, height, rgb = q.screendump("inputprobe")
        red = sum(1 for i in range(0, len(rgb), 3) if rgb[i:i + 3] == b"\xff\x00\x00")
        if red > width * height // 100:
            raise Failure("the shell's screen was not drawn again (%d red pixels)" % red)
    yield ("screen manager claims: an end signal, the input queue and the screen, lent by "
           "the manager"), screen_manager_claims

    def jobdemo():
        # One job, then one per core: the renderings must agree (the demo
        # checks), and with several cores the jobs must run in parallel. The
        # thread start and join each wait up to a tick (no IPIs), and the host
        # adds noise, hence a modest bar and a few attempts.
        for attempt in range(3):
            q.send("jobdemo 256\r")
            m = q.expect(rb"^jobdemo: (?:(\d+)x(\d+), 256 iterations: 1 core (\d+) ms, %d cores? "
                         rb"(\d+) ms, speedup (\d+)\.(\d\d)x(, press Enter)?|([^\r\n]*))\r?\n"
                         % cores, timeout=120)
            if m.group(8) is not None:
                raise Failure("jobdemo: %s" % m.group(8).decode(errors="replace"))
            if graphics != (m.group(7) is not None):
                raise Failure("jobdemo %s the picture with graphics %s"
                              % ("kept" if m.group(7) else "did not keep", "on" if graphics else "off"))
            if graphics:
                # The line reaches the serial port before the screen terminal
                # has redrawn its text, and jobdemo puts the picture back only
                # after that: wait for the whole picture, the top-left corner
                # (far outside the set: dark blue, not text) included.
                for _ in range(20):
                    shot = q.screendump("jobdemo")
                    colours = {shot[2][i:i + 3] for i in range(0, len(shot[2]), 3)}
                    r, g, b = pixel(shot, 2, 2)
                    if len(colours) >= 40 and b >= 64 and r < 32 and g < 32:
                        break
                    time.sleep(0.2)
                else:
                    raise Failure("expected the Mandelbrot set on screen: %d colours, top left %s"
                                  % (len(colours), pixel(shot, 2, 2)))
                q.send("\r")
            q.expect(rb"\] exited with 0")
            q.expect(PROMPT)
            speedup = int(m.group(5)) + int(m.group(6)) / 100
            if cores == 1 or speedup >= 1.4:
                break
        else:
            raise Failure("jobdemo on %d cores: speedup only %.2fx" % (cores, speedup))
    yield "jobdemo: Mandelbrot as one job, then one job per core", jobdemo

    def pindemo():
        specs = (["6@1", "2@2", "2@2", "2@2"] if cores >= 3 else
                 ["2@1", "1@0", "1@1"] if cores == 2 else ["2", "1", "1"])
        q.send("pindemo %s\r" % " ".join(specs))
        for _ in specs:
            m = q.expect(rb"^pindemo: (?:worker (\d+) \((\d+) units?, cpu (\d+|\?)( pinned)?\) done|"
                         rb"([^\r\n]*)\r?\n)", timeout=120)
            if m.group(5) is not None:
                raise Failure("pindemo: %s" % m.group(5).decode(errors="replace"))
            spec = specs[int(m.group(1)) - 1]
            if "@" in spec and m.group(3).decode() != spec.split("@")[1]:
                raise Failure("pindemo worker %s ran on cpu %s" % (spec, m.group(3).decode()))
        q.expect(rb"^pindemo: %d workers done in \d+ ms" % len(specs))
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield "pindemo: unbalanced sub-processes pinned by hand", pindemo

    if stress:
        def stress_rounds():
            for _ in range(stress):
                for check in (threads, thread_lifetime, kill_job, message_ports, signals,
                              screen_manager_claims, jobdemo, pindemo):
                    check()
        yield ("stress: %d rounds of threads, process lifetime, kill, message ports, signals, "
               "screen manager claims and both demos"
               % stress), stress_rounds

    if repeat:
        def allocation_lifetime():
            baseline = None
            for run in range(repeat):
                q.send("allocprobe\r")
                q.expect(rb"allocprobe: heap checked", timeout=60)
                q.expect(rb"exited with 0")
                q.expect(PROMPT)
                # After the first run every cache is warm: from then on each
                # reaped probe, including what it leaked on purpose, must give
                # back exactly what it took.
                if run == 0 or run == repeat - 1:
                    counts = kernel_counts()
                    if baseline is None:
                        baseline = counts
                    elif counts != baseline:
                        raise Failure("kernel heap/pages (live, free) %s after the first run, "
                                      "%s after %d runs" % (baseline, counts, repeat))
        yield "repeated EL0 page runs, malloc heap and nested-process lifetime (%d runs)" % repeat, allocation_lifetime

    if keyboard:
        def keyboard_input():
            # Typed entirely through the virtual device, including Shift,
            # release transitions, editing and Enter. UART is only the observer.
            # Every key travels as events through the kernel's input queue and
            # the screen manager to the shell's console, which edits the line:
            # Left, then c and Backspace before the b.
            # A USB keyboard is polled, and QEMU queues only a few transitions
            # for it: leave it time to fetch each key.
            gap = 0.1 if keyboard == "USB" else 0.02
            for key in ("e", "c", "h", "o", "spc", "shift-a", "b", "left", "c", "backspace", "ret"):
                reply = q.monitor("sendkey " + key + " 10")
                if b"Error" in reply or b"unknown" in reply.lower():
                    raise Failure("QEMU key injection failed: %r" % reply)
                time.sleep(gap)
            q.expect(rb"^Ab\r?$")
            q.expect(rb"exited with 1")
            q.expect(PROMPT)
            q.monitor("sendkey shift-pgup 10")
            time.sleep(gap)
            q.monitor("sendkey shift-pgdn 10")
        yield "%s keyboard handles Shift, release, an arrow, editing and Enter" % keyboard, keyboard_input

        def keylog():
            # The debug console's keylog traces each event as it is taken: the
            # device, the reader (the screen manager, with the shell's screen
            # in front) and the key. Then inputprobe borrows the keyboard and
            # claims the queue: Meta+M goes to it (front: itself), not to the
            # manager.
            gap = 0.1 if keyboard == "USB" else 0.02
            device = rb"(?:virtio|usb)-keyboard"
            q.debug("keylog on")
            q.expect(rb"^keylog: on, events go to thread \d+")
            q.monitor("sendkey shift-k 10")
            q.expect(rb"^KeyDown from " + device + rb" to screens \(pid \d+\) -> front: koracli \(pid \d+\)\r?\n"
                     rb"    time \d+ ms, keycode 0x0e \(K\), modifiers 0x0002 \(LShift\)\r?\n"
                     rb"    char U\+004B \"K\", repeat no")
            time.sleep(gap)
            q.monitor("sendkey backspace 10")  # leave the shell's line empty
            q.expect(rb"keycode 0x2a \(Backspace\)")
            time.sleep(gap)
            q.send("inputprobe keys 4\r")
            q.expect(rb"^inputprobe: ready for 4 keys")
            time.sleep(gap)
            q.monitor("sendkey meta_l-m 10")
            m = q.expect(rb"^KeyDown from " + device + rb" to inputprobe \(pid (\d+)\) -> "
                         rb"front: inputprobe \(pid (\d+)\)\r?\n"
                         rb"    time \d+ ms, keycode 0x10 \(M\), modifiers 0x0008 \(LGUI\)", timeout=20)
            if m.group(1) != m.group(2):
                raise Failure("keylog names another front process: %r" % m.group(0))
            q.expect(rb"^inputprobe: key down 0x10 mods 0x0008 char 0x6d", timeout=20)
            m = q.expect(rb"^inputprobe: (keys done|FAIL [^\r\n]*)", timeout=30)
            if m.group(1) != b"keys done":
                raise Failure(m.group(1).decode(errors="replace"))
            q.expect(rb"\] exited with 0")
            q.expect(PROMPT)
            q.debug("keylog off")
            q.expect(rb"^keylog: off")
        yield "keylog traces %s keys to the console and to a claimer (Meta+M)" % keyboard, keylog

    MOUSE_TO = rb"^Mouse from %s to screens \(pid \d+\) -> front: koracli \(pid \d+\)\r?\n    time \d+ ms, "

    def debug_mouse():
        # The debug console is a mouse too: its events go to the screen
        # manager like any mouse's, and keylog traces them as it takes them.
        # (It may not hold the queue yet, just after a program borrowed it:
        # the events wait for it.)
        q.debug("keylog on")
        q.expect(rb"^keylog: on")
        source = rb"debug-console"
        q.debug("mouse 5 -3")
        q.expect(MOUSE_TO % source + rb"move 5,-3, buttons 0x00 \(none\), wheel 0, modifiers 0x0000 \(none\)")
        q.debug("mouse click left")
        q.expect(MOUSE_TO % source + rb"move 0,0, buttons 0x01 \(Left\)")
        q.expect(MOUSE_TO % source + rb"move 0,0, buttons 0x00 \(none\)")
        q.debug("mouse down right")
        q.debug("mouse -2 7")
        q.expect(MOUSE_TO % source + rb"move 0,0, buttons 0x02 \(Right\)")
        q.expect(MOUSE_TO % source + rb"move -2,7, buttons 0x02 \(Right\)")
        q.debug("mouse up right")
        q.expect(MOUSE_TO % source + rb"move 0,0, buttons 0x00")
        q.debug("mouse to 16384 0")
        q.expect(MOUSE_TO % source + rb"at 16384,0, buttons 0x00")
        q.debug("mouse wheel -2")
        q.expect(MOUSE_TO % source + rb"move 0,0, buttons 0x00 \(none\), wheel -2")
        q.debug("keylog off")
        q.expect(rb"^keylog: off")
    yield "the debug console's mouse: moves, clicks, a drag, a position and the wheel", debug_mouse

    if mouse:
        def device_mouse():
            # QEMU's monitor moves and clicks the mouse: a VirtIO one on virt,
            # the USB one (Circle's driver) on raspi3b. (The tablet beside it
            # on virt, found at boot, takes positions, which the monitor only
            # sends with a display window: the host tests cover it.)
            gap = 0.1 if mouse == "USB" else 0.02
            source = rb"virtio-mouse" if mouse == "VirtIO" else rb"usb-mouse"
            q.debug("keylog on")
            q.expect(rb"^keylog: on")
            if mouse == "VirtIO":
                mice = q.monitor("info mice").decode(errors="replace")
                relative = re.search(r"Mouse #(\d+): [^\n]*Virtio Mouse", mice, re.I)
                if relative is None:
                    raise Failure("QEMU lists no VirtIO mouse: %r" % mice)
                q.monitor("mouse_set " + relative.group(1))
            q.monitor("mouse_move 7 4")
            q.expect(MOUSE_TO % source + rb"move 7,4, buttons 0x00", timeout=20)
            time.sleep(gap)
            q.monitor("mouse_button 1")
            q.expect(MOUSE_TO % source + rb"move 0,0, buttons 0x01 \(Left\)", timeout=20)
            time.sleep(gap)
            q.monitor("mouse_button 0")
            q.expect(MOUSE_TO % source + rb"move 0,0, buttons 0x00", timeout=20)
            q.debug("keylog off")
            q.expect(rb"^keylog: off")
        yield "%s mouse: moves and a click" % mouse, device_mouse

    def termdemo():
        # Its escape sequences go to the shell's console, which draws them.
        q.send("termdemo\r")
        q.expect(rb"Shift\+PgUp")
        q.expect(PROMPT)
        for _ in range(20):  # the serial copy comes before the screen's
            shot = q.screendump("termdemo")
            colours = {shot[2][i:i + 3] for i in range(0, len(shot[2]), 3)}
            if len(colours) >= 200:
                break
            time.sleep(0.2)
        else:
            raise Failure("expected the 256/24-bit colour tables on screen, "
                          "found only %d distinct colours" % len(colours))
    yield "termdemo renders colours on the shell's console", termdemo

    def condemo():
        # Two consoles of the console library, run by a user program in
        # windows on a screen of its own: the left one blue with a
        # line-drawing box, the right one red, larger than its window and
        # clipped to it. Escape closes it.
        q.send("condemo\r")
        # Up to the end of the line: the last number may still be on its way.
        m = q.expect(rb"condemo: windows (\d+)x(\d+) at (\d+),(\d+) and (\d+),(\d+); "
                     rb"left console cursor at (\d+);(\d+)\r?\n")
        width, height, left, top, right, _, row, col = (int(g) for g in m.groups())
        time.sleep(0.2)
        shot = q.screendump("condemo")
        q.key("esc")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
        if (row, col) != (9, 21):
            raise Failure("the left console reported its cursor at %d;%d, want 9;21" % (row, col))
        checks = (
            ((left + width - 1, top + height - 1), (0, 0, 0xee), "left console background"),
            ((left + 5 * 8 + 2, top + 2 * 8 + 3), (0xff, 0xff, 0xff), "left console's box line"),
            ((left - 1, top), (0x80, 0x80, 0x80), "left window frame"),
            ((right + width, top + height), (0x80, 0x80, 0x80), "right window frame"),
        )
        for (x, y), expected, what in checks:
            if not near(pixel(shot, x, y), expected):
                raise Failure("%s: pixel (%d,%d) = %s, expected %s"
                              % (what, x, y, pixel(shot, x, y), expected))
        red = sum(near(pixel(shot, x, y), (0xcd, 0, 0))
                  for y in range(top, top + height, 2) for x in range(right, right + width, 2))
        if red < width * height // 8:
            raise Failure("the right console's window is not red (%d of %d samples)"
                          % (red, width * height // 4))
        # Unclipped, the right console would run 64 pixels past its window.
        spill = sum(near(pixel(shot, x, y), (0xcd, 0, 0))
                    for y in range(top, top + height) for x in range(right + width + 4, right + width + 64))
        if spill:
            raise Failure("%d red pixels right of the right window: not clipped" % spill)
    yield "condemo: two library consoles in clipped windows", condemo

    def console_handler():
        # condemo runs echo with its stdout in a library console: the words
        # go to condemo's window through message ports, not to the kernel's
        # console (and so not to the serial line).
        q.send("condemo echo hello from a handler\r")
        # echo exits with its argument count.
        m = q.expect(rb"^condemo: echo exited with 4; (\d+) bytes in its window\r?$")
        time.sleep(0.2)
        if m.group(1) != b"21":
            raise Failure("condemo showed %s bytes of echo's output, want 21" % m.group(1).decode())
        with open(os.path.join(q.outdir, "serial.log"), "rb") as log:
            serial = log.read()
        if re.search(rb"^hello from a handler\r?$", serial, re.M):
            raise Failure("echo's output reached the kernel console")
        shot = q.screendump("condemo-handler")
        width, height, _ = shot
        left, top = (width - 640) // 2, height - 128 - 16
        if not near(pixel(shot, left + 639, top + 127), (0, 0, 0xee)):
            raise Failure("condemo's window is not blue: %s" % (pixel(shot, left + 639, top + 127),))
        white = sum(near(pixel(shot, x, y), (0xff, 0xff, 0xff))
                    for y in range(top, top + 8) for x in range(left, left + 20 * 8))
        if white < 40:
            raise Failure("no text in condemo's window (%d white pixels in its first line)" % white)
        q.key("esc")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield "condemo as a console handler: a program's stdout in a library console", console_handler

    def gfxdemo():
        q.send("gfxdemo\r")
        q.expect(rb"gfxdemo: painted (\d+)x(\d+) on screen \d+, Escape closes it")
        time.sleep(0.2)
        shot = q.screendump("gfxdemo")
        width, height, _ = shot
        # gfxdemo paints red rising with x and green rising with y, all over
        # its screen.
        for x, y in ((8, 8), (8, height - 8), (width - 8, height - 8), (width - 8, 300)):
            expected = ((x * 255) // width, (y * 255) // height, 0)
            if not near(pixel(shot, x, y), expected):
                raise Failure("pixel (%d,%d) = %s, expected about %s"
                              % (x, y, pixel(shot, x, y), expected))
        q.key("esc")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield "gfxdemo paints a screen of its own, Escape closes it", gfxdemo

    COLOURS = {b"red": (255, 0, 0), b"green": (0, 255, 0), b"blue": (0, 0, 255)}

    def uniform(name, colour):
        """The whole display is one colour (a screen in front); its size."""
        width, height, rgb = q.screendump(name)
        want = bytes(colour)
        wrong = sum(1 for i in range(0, len(rgb), 3 * 97) if rgb[i:i + 3] != want)
        if wrong:
            raise Failure("%s: %d sampled pixels are not %s" % (name, wrong, colour))
        return width, height

    def shell_back(name):
        """The shell's screen is in front: text on black, no screen's colour."""
        width, height, rgb = q.screendump(name)
        colours = {rgb[i:i + 3] for i in range(0, len(rgb), 3 * 7)}
        if len(colours) < 2 or any(bytes(c) in colours and rgb.count(bytes(c)) > len(rgb) // 6
                                   for c in COLOURS.values()):
            raise Failure("%s: the shell's screen is not in front" % name)

    def opened(count):
        """The screenprobes' screens, as {screen id: (colour, pid)}."""
        screens = {}
        while len(screens) < count:
            m = q.expect(rb"(?:^|\$ )screenprobe: (?:(\w+) on screen (\d+), pid (\d+)|([^\r\n]*))\r?\n",
                         timeout=30)
            if m.group(4) is not None:
                raise Failure("screenprobe: %s" % m.group(4).decode(errors="replace"))
            screens[int(m.group(2))] = (m.group(1), m.group(3).decode())
        return screens

    # Lines of the manager and the probes may follow the shell's prompt.
    LINE = rb"(?:^|\$ )"

    def expect_both(first, second, timeout=30):
        """Two lines from different programs, in either order."""
        m = q.expect(first + b"|" + second, timeout=timeout)
        q.expect(second if re.fullmatch(first, m.group(0)) else first, timeout=timeout)
    SHELL_FRONT = LINE + rb"screens: screen \d+ in front \(koracli\)"

    def screen_manager():
        # A second manager is refused.
        q.send("screens hello\r")
        q.expect(rb"^screens: a screen manager runs already")
        q.expect(rb"exited with 1")
        q.expect(PROMPT)
        # Two screenprobes in the background, each on a screen of its own
        # beside the shell's. Keys typed through the debug console go to the
        # front screen's program; Meta+M brings the next screen to front, in
        # the order they were opened, the shell's first; x ends a probe
        # without closing its screen, as a crash would, and the manager
        # closes it at once and brings the next one to front.
        q.send("screenprobe red &\r")
        (red_id, (red, _)), = opened(1).items()
        time.sleep(0.2)
        uniform("screens-red", COLOURS[red])
        q.key("a")
        q.expect(LINE + rb"screenprobe red: key down 0x04 mods 0x0000 char 0x61")
        q.key("meta_l-m")
        q.expect(SHELL_FRONT)
        time.sleep(0.2)
        shell_back("screens-shell")
        q.send("screenprobe green &\r")
        (green_id, (green, _)), = opened(1).items()
        time.sleep(0.2)
        uniform("screens-green", COLOURS[green])
        q.key("meta_l-m")
        q.expect(SHELL_FRONT)
        q.key("meta_l-m")
        q.expect(LINE + rb"screens: screen %d in front" % red_id)
        time.sleep(0.2)
        uniform("screens-red-again", COLOURS[red])
        q.key("x")
        q.expect(LINE + rb"screenprobe: red ends, screen open")
        q.expect(LINE + rb"screens: screen %d closed \(screenprobe red\), its program ended" % red_id)
        q.expect(LINE + rb"screens: screen %d in front" % green_id)
        time.sleep(0.2)
        uniform("screens-green-again", COLOURS[green])
        q.key("x")
        q.expect(LINE + rb"screens: screen %d closed \(screenprobe green\), its program ended" % green_id)
        q.expect(SHELL_FRONT)
        q.send("\r")
        q.expect(PROMPT)
        time.sleep(0.2)
        shell_back("screens-done")
    yield ("screens: a second manager refused, keys to the front screen, Meta+M, each screen "
           "closed as its program ends, the shell's in front again"), screen_manager

    def pointer():
        # The screen manager draws the pointer over the front screen and
        # gives its program the mouse's events, at the pointer's position in
        # the screen's pixels; typing hides the pointer (the command typed
        # here did), and moving the mouse shows it again.
        q.send("screenprobe red &\r")
        opened(1)
        time.sleep(0.2)
        width, height = uniform("pointer-hidden", COLOURS[b"red"])
        x, y = width // 2, height // 2
        q.debug("mouse to 16384 16384")
        q.expect(LINE + rb"screenprobe red: mouse at %d,%d buttons 0x00 wheel 0" % (x, y))
        time.sleep(0.2)
        shot = q.screendump("pointer-shown")
        if (pixel(shot, x, y) != (0, 0, 0) or pixel(shot, x + 1, y + 2) != (255, 255, 255) or
                pixel(shot, x + 20, y) != COLOURS[b"red"] or pixel(shot, x - 1, y) != COLOURS[b"red"]):
            raise Failure("no pointer drawn at %d,%d: %s %s" % (x, y, pixel(shot, x, y),
                                                               pixel(shot, x + 1, y + 2)))
        q.debug("mouse click left")
        q.expect(LINE + rb"screenprobe red: mouse at %d,%d buttons 0x01 wheel 0" % (x, y))
        q.expect(LINE + rb"screenprobe red: mouse at %d,%d buttons 0x00 wheel 0" % (x, y))
        q.debug("mouse 10 -4")
        q.expect(LINE + rb"screenprobe red: mouse at %d,%d buttons 0x00 wheel 0" % (x + 10, y - 4))
        q.debug("mouse wheel 1")
        q.expect(LINE + rb"screenprobe red: mouse at %d,%d buttons 0x00 wheel 1" % (x + 10, y - 4))
        q.key("a")
        q.expect(LINE + rb"screenprobe red: key down 0x04 mods 0x0000 char 0x61")
        time.sleep(0.2)
        uniform("pointer-typed", COLOURS[b"red"])
        q.key("esc")
        expect_both(SHELL_FRONT, LINE + rb"screenprobe: red closed")
        q.send("\r")
        q.expect(PROMPT)
    yield ("screens: the pointer drawn over the front screen, mouse events at its position, "
           "hidden by typing"), pointer

    def shell_break():
        # Ctrl+C on the shell's console ends the program in front; at the
        # prompt it drops the line, and is not kept for the next program.
        q.send("conprobe hang\r")
        q.expect(rb"^conprobe: hanging", timeout=30)
        q.key("ctrl-c")
        q.expect(rb"\] exited with -9")
        q.expect(PROMPT)
        q.send("echo dropped")
        time.sleep(0.2)
        q.key("ctrl-c")
        q.expect(rb"\^C")
        q.expect(PROMPT)  # a new one: the line read ended
        # The shell handles breaks: more of them at the prompt do not end it.
        for _ in range(2):
            time.sleep(0.2)
            q.key("ctrl-c")
            q.expect(rb"\^C")
            q.expect(PROMPT)
        q.send("echo kept\r")
        q.expect(rb"^kept\r?$")
        q.expect(rb"\] exited with 1")  # echo exits with its argument count
        q.expect(PROMPT)
    yield ("Ctrl+C ends the shell's program in front, and drops a line at the prompt "
           "for a new one"), shell_break

    def break_caught():
        # A program that allocated SIG_BREAK gets the break instead of being
        # ended; once it is stuck, the second break ends it anyway.
        q.send("breakprobe\r")
        q.expect(rb"^breakprobe: waiting for a break", timeout=30)
        time.sleep(0.2)
        q.key("ctrl-c")
        q.expect(rb"^breakprobe: caught a break")
        time.sleep(0.2)
        q.key("ctrl-c")  # left set: the program never clears it
        time.sleep(0.5)
        listed, _, _, _ = read_tasks_bkl()
        if "breakprobe" not in listed:
            raise Failure("one break after the caught one ended breakprobe")
        q.key("ctrl-c")
        q.expect(rb"\] exited with -9")
        q.expect(PROMPT)
    yield "a program catches Ctrl+C, and a second pending break ends it", break_caught

    def console_events():
        # A console's new parts (KoraCLI 1a), in a console of keyprobe's own:
        # bars that take a row each from the grid, the key-event mode, and ESC
        # as a break for the program that asks.
        q.send("newcon keyprobe\r")
        # Its size questions (CSI 18 t) come first on the serial copy's lines.
        m = q.expect(rb"keyprobe: size (\d+)x(\d+)\r?\n", timeout=30)
        cols, rows = int(m.group(1)), int(m.group(2))
        q.expect(rb"keyprobe: bars, size %dx%d\r?\n" % (cols, rows - 2))
        q.expect(rb"^keyprobe: events")
        time.sleep(0.3)
        width, height, rgb = q.screendump("keyprobe-bars")
        top = rgb[:width * 3]  # the first row of pixels: the top bar, in reverse video
        lit = sum(1 for i in range(0, len(top), 3 * 4) if top[i:i + 3] != b"\x00\x00\x00")
        if lit < width // 8:
            raise Failure("the top bar is not drawn (%d of %d pixels sampled lit)" % (lit, width // 4))
        for name, line in (("f1", rb"key 3a mods 0000 char 0"),
                           ("ctrl-f5", rb"key 3e mods 0001 char 0"),
                           ("shift-a", rb"key 04 mods 0002 char 65"),
                           ("esc", rb"key 29 mods 0000 char 0")):
            q.key(name)
            q.expect(rb"^keyprobe: " + line)
        q.key("shift-pgup")  # the console's: scrolls, no event
        q.key("f12")
        q.expect(rb"^keyprobe: key 45 mods 0000 char 0")
        q.key("f10")
        q.expect(rb"^keyprobe: ESC is a break")
        time.sleep(0.2)
        q.key("esc")
        q.expect(rb"^keyprobe: break")
        q.expect(rb"^keyprobe: ESC is a key")
        time.sleep(0.2)
        q.key("esc")
        q.expect(rb"^keyprobe: ESC as a key")
        q.expect(rb"keyprobe: done %dx%d\r?\n" % (cols, rows))
        q.expect(rb"newcon: keyprobe exited with 0")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield "console: bars, the key-event mode and ESC as a break (keyprobe)", console_events

    def koracli():
        # KoraCLI (step 1b): its own screen, the Python editor on key events,
        # a block that ends at the second blank line, ESC stopping code, the mode
        # keys through BASIC and OS mode, and Ctrl+D to end.
        q.send("koracli\r")
        q.expect(rb"KoraCLI on MicroPython 1\.29\.0\. F1 for help\.", timeout=30)
        q.expect(rb">>> ")
        time.sleep(0.3)
        q.send("def f(x):\r")      # the editor indents the next line itself
        q.send("return x * 2\r")
        q.send("\r")                # one blank line is kept,
        q.send("\r")                # the second ends the block
        time.sleep(0.3)
        q.send("f(21)\r")
        q.expect(rb"^42\r?$")
        time.sleep(0.3)
        width, height, rgb = q.screendump("koracli")
        for name, y in (("top", 0), ("bottom", height - 1)):  # the bars, in reverse video
            row = rgb[y * width * 3:(y + 1) * width * 3]
            lit = sum(1 for i in range(0, len(row), 3 * 4) if row[i:i + 3] != b"\x00\x00\x00")
            if lit < width // 8:
                raise Failure("koracli's %s bar is not drawn" % name)
        # The spec's example (step 2b): a program line reads Python's msg.
        q.send('msg = "hello world"\r')
        q.send("10 PRINT msg$\r")   # a program line: stored, nothing printed
        q.send("%RUN\r")
        q.expect(rb"^hello world\r?$")
        q.send("[n for n in range(3)]\r")
        q.expect(rb"^\[0, 1, 2\]\r?$")
        q.send("while True: pass\r\r\r")
        # ESC is a break only once the code runs (before, it is a key the
        # editor drops): pressed again until it stops the loop.
        for attempt in range(5):
            time.sleep(0.5)
            q.key("esc")
            try:
                q.expect(rb"^KeyboardInterrupt\r?$", timeout=3)
                break
            except Failure:
                if attempt == 4:
                    raise
        q.send("x = 1 / 0\r")  # ("1 / 0" would be a program line: it starts with a digit)
        q.expect(rb"^ZeroDivisionError: ")
        # OS commands from Python (step 1c): run live, captured, with {expr},
        # redirected, in the background, a wrapper, Ctrl+C passed on.
        q.send("!echo hello there\r")
        q.expect(rb"^hello there\r?$")
        q.send("kora.status\r")
        q.expect(rb"^2\r?$")  # echo's status: how many words it echoed
        q.send("out = !echo captured {6 * 7}\r")
        q.send("out.lines, out.status\r")
        q.expect(rb"^\(\['captured 42'\], 2\)\r?$")
        q.send("files = ['a b', 'c']\r")
        q.send("!echo {files} x{len(files)}y\r")
        q.expect(rb"^a b c x2y\r?$")
        q.send("writable = ['%s:kcli.txt' % v[0] for v in kora.volumes() if not v[3]][:1]\r")
        q.send("r = 'read-only'\r")
        q.send("for f in writable:\r")
        q.send("!echo to a file > {f}\r")
        q.send("r = !cat < {f}\r")
        q.send("\r")
        q.send("\r")
        time.sleep(0.5)
        q.send("print('redirect:', repr(str(r)))\r")
        q.expect(rb"^redirect: '(to a file\\n|read-only)'\r?$")
        q.send("@kora.cmd.register\r")
        q.send("def greet(*args):\r")
        q.send("return 'hi ' + ' '.join(args)\r")
        q.send("\r")
        q.send("\r")
        time.sleep(0.5)
        q.send("!greet you\r")
        q.expect(rb"^hi you\r?$")
        q.send("!hello &\r")
        q.expect(rb"^\[\d+\] started\r?$")
        time.sleep(1)
        q.send("\r")  # reported at a prompt after its end
        q.expect(rb"^\[\d+\] done, exit 0\r?$")
        q.send("!conprobe hang\r")
        q.expect(rb"^conprobe: hanging", timeout=30)
        time.sleep(0.3)
        q.key("ctrl-c")  # passed on: conprobe does not catch it
        time.sleep(0.3)
        q.send("kora.status\r")
        q.expect(rb"^-9\r?$")
        # BASIC (step 2a): a line of it from Python mode, then BASIC mode
        # with READY., the program line typed in Python mode, keywords
        # without spaces, ESC as a break and % for a line of Python, which
        # sees BASIC's variables (step 2b).
        q.send("%PRINT 6*7\r")
        q.expect(rb"^ 42 \r?$")
        q.key("f4")
        q.expect(rb"READY\.\r?\n")
        q.send("LIST\r")
        q.expect(rb"^10 PRINT msg\$\r?$")
        q.expect(rb"^READY\.\r?$")
        q.send('20 FORI=1TO3:PRINTI;:NEXT:PRINT "DONE"\r')
        q.send("30 N = N + 1: GOTO 30\r")
        q.send("RUN\r")
        q.expect(rb"^hello world\r?$")
        q.expect(rb"^ 1  2  3 DONE\r?$")
        for attempt in range(5):  # ESC, once the program runs
            time.sleep(0.5)
            q.key("esc")
            try:
                q.expect(rb"^BREAK IN 30\r?$", timeout=3)
                break
            except Failure:
                if attempt == 4:
                    raise
        q.expect(rb"^READY\.\r?$")
        q.send("PRINT N > 0\r")
        q.expect(rb"^-1 \r?$")
        q.send("%print('python', N > 0, msg)\r")  # BASIC's N, shared
        q.expect(rb"^python True hello world\r?$")
        q.expect(rb"^READY\.\r?$")
        # Step 2c: F8 lists, RENUM follows the GOTO, and on a writable volume
        # SAVE, NEW and LOAD bring the program back.
        q.key("f8")
        q.expect(rb"^30 N = N \+ 1: GOTO 30\r?$")
        q.expect(rb"^READY\.\r?$")
        q.send("RENUM 100, 10\r")
        q.expect(rb"^READY\.\r?$")
        q.send("LIST 120\r")
        q.expect(rb"^120 N = N \+ 1: GOTO 120\r?$")
        q.expect(rb"^READY\.\r?$")
        q.send("%print('writable:', bool(writable))\r")
        if q.expect(rb"^writable: (True|False)\r?$").group(1) == b"True":
            q.send("%prog = writable[0].replace('kcli.txt', 'kcli')\r")
            q.expect(rb"^READY\.\r?$")
            q.send("SAVE PROG$\r")
            q.expect(rb"^SAVING \S+:kcli\r?$")
            q.expect(rb"^READY\.\r?$")
            q.send("NEW\r")
            q.expect(rb"^READY\.\r?$")
            q.send("LOAD PROG$\r")
            q.expect(rb"^LOADING\r?$")
            q.expect(rb"^READY\.\r?$")
            q.send("LIST 100\r")
            q.expect(rb"^100 PRINT msg\$\r?$")
            q.expect(rb"^READY\.\r?$")
        q.send("NEW\r")
        q.expect(rb"^READY\.\r?$")
        # Step 3b: F4 in BASIC mode switches to BBC BASIC while no program
        # is in memory. Its PRINT right-aligns numbers in fields of ten,
        # abbreviations are stored in full, and with a program in memory F4
        # stays; after NEW it goes back to Classic BASIC.
        q.key("f4")
        q.expect(rb"^BBC BASIC\r?$")
        q.send('PRINT 6*7;"!"\r')
        q.expect(rb"^        42!\r?$")
        q.send("10 F.I%=1 TO 3: P. I%;: N.: P.\r")
        q.send("LIST\r")
        q.expect(rb"^   10 FORI%=1 TO 3: PRINT I%;: NEXT: PRINT\r?$")
        q.send("RUN\r")
        q.expect(rb"^         1         2         3\r?$")
        # Step 3c: an IF over several lines, its false part skipped.
        for line in ("20 IF I%=4 THEN", '30 PRINT "block"', "40 ELSE",
                     '50 PRINT "no"', "60 ENDIF"):
            q.send(line + "\r")
        q.send("RUN\r")
        q.expect(rb"^         1         2         3\r?$")
        q.expect(rb"^block\r?$")
        # Step 3d: an FN, defined after END, and a PROC that is Python's.
        for line in ("70 PRINT FNsq(12): PROCshout(\"proc\")", "80 END",
                     "90 DEF FNsq(x) = x * x"):
            q.send(line + "\r")
        q.send("%shout = lambda t: print(t.upper())\r")
        q.send("RUN\r")
        q.expect(rb"^block\r?$")
        q.expect(rb"^       144\r?$")
        q.expect(rb"^PROC\r?$")
        q.key("f4")
        q.expect(rb"^koracli: a program is in memory")
        q.send("NEW\r")
        time.sleep(0.3)
        q.key("f4")
        q.expect(rb"^Classic BASIC\r?$")
        q.expect(rb"^READY\.\r?$")
        # OS mode runs every line as a command, with no {expr}; F2 goes
        # back to BASIC (READY.), F3 to Python.
        q.key("f2")
        time.sleep(0.2)
        q.send("echo os mode {x}\r")
        q.expect(rb"^os mode \{x\}\r?$")
        q.expect(rb"exited with \d+\r?\n\$ ")  # keys go to echo until it ends
        time.sleep(0.3)
        q.key("f2")
        q.expect(rb"READY\.\r?\n")
        q.key("f3")
        q.expect(rb"\x1b\[J>>> ")
        q.key("ctrl-d")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield "koracli: Python at its prompt, blocks, ESC, OS commands, BASIC, the mode keys and Ctrl+D", koracli

    def micropython():
        # MicroPython's REPL on the shell's console, with its own line editor
        # (the console in raw mode); Ctrl+D on an empty line ends it, and the
        # shell's console is cooked again.
        q.send("micropython\r")
        q.expect(rb"^MicroPython v1\.29\.0 on 2026-08-24; KoraOS with AArch64", timeout=30)
        q.expect(rb">>> ")
        q.send("print(1+1)\r")
        q.expect(rb"^2\r?$")
        q.expect(rb">>> ")
        q.send("[x * x for x in range(5)], 2 ** 70, 1 / 4\r")
        q.expect(rb"^\(\[0, 1, 4, 9, 16\], 1180591620717411303424, 0\.25\)\r?$")
        q.expect(rb">>> ")
        time.sleep(0.2)
        q.key("ctrl-d")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
        q.send("echo cooked\r")
        q.expect(rb"^cooked\r?$")
        q.expect(PROMPT)
    yield "micropython: a REPL on the console, and the console cooked after it", micropython

    def micropython_break():
        # Ctrl+C is KeyboardInterrupt: at the prompt a new line, in running
        # code an exception, and the REPL goes on.
        q.send("micropython\r")
        q.expect(rb">>> ", timeout=30)
        time.sleep(0.2)
        for _ in range(2):  # more than one: none is left pending at the prompt
            q.key("ctrl-c")
            q.expect(rb">>> ")
            time.sleep(0.2)
        q.send("while True: pass\r")
        q.expect(rb"\.\.\. ")
        q.send("\r")
        time.sleep(0.5)
        q.key("ctrl-c")
        q.expect(rb"^KeyboardInterrupt")
        q.expect(rb">>> ")
        q.send("6 * 7\r")
        q.expect(rb"^42\r?$")
        q.expect(rb">>> ")
        time.sleep(0.2)
        q.key("ctrl-d")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
        # A script catches it in a loop and in a sleep; one it does not catch
        # ends it with 130.
        q.send("micropython sys:tests/mpprobe.py break\r")
        q.expect(rb"^mpprobe: looping", timeout=30)
        time.sleep(0.3)
        q.key("ctrl-c")
        q.expect(rb"^mpprobe: KeyboardInterrupt in a loop")
        q.expect(rb"^mpprobe: sleeping")
        time.sleep(0.3)
        q.key("ctrl-c")
        q.expect(rb"^mpprobe: KeyboardInterrupt in a sleep")
        q.expect(rb"^mpprobe: looping, not catching it")
        time.sleep(0.3)
        q.key("ctrl-c")
        q.expect(rb"^KeyboardInterrupt")
        q.expect(rb"\] exited with 130")
        q.expect(PROMPT)
        # kora.repl(), KoraCLI's fallback: MicroPython's REPL inside a script.
        q.send("micropython sys:tests/mpprobe.py repl\r")
        q.expect(rb">>> ", timeout=30)
        q.send("6 * 7\r")
        q.expect(rb"^42\r?$")
        q.expect(rb">>> ")
        time.sleep(0.2)
        q.key("ctrl-d")
        q.expect(rb"^mpprobe: after the REPL")
        q.expect(rb"\] exited with 0")
        q.expect(PROMPT)
    yield ("micropython: Ctrl+C is KeyboardInterrupt, at the prompt and in code; "
           "kora.repl()"), micropython_break

    def micropython_files():
        # A script from the userfs: its arguments, a module beside it, os over
        # volumes and assigns, kora, time, a heap that grows, the stack
        # check, and files written where the boot volume is writable.
        q.send("micropython sys:tests/mpprobe.py files \"A B\"\r")
        m = q.expect(rb"^mpprobe: (files written, read, renamed and removed|read-only boot volume"
                     rb"|FAILED[^\r\n]*)", timeout=30)
        if m.group(1).startswith(b"FAILED"):
            raise Failure("mpprobe: %s" % m.group(1).decode(errors="replace"))
        q.expect(rb"^mpprobe: ok")
        q.expect(rb"\] exited with 3")
        q.expect(PROMPT)
    yield "micropython: a script with os, files, imports, kora and time", micropython_files

    def fonts():
        # `font` lists sys:fonts and switches the shell's console to another
        # font: the console resizes, and a program asking for its size gets
        # the new one. The display is 1024x768; KoraCLI's bars take a row
        # each.
        q.send("font\r")
        for name in (rb"spleen-5x8", rb"spleen-6x12", rb"spleen-8x16", rb"spleen-12x24",
                     rb"spleen-16x32", rb"builtin"):
            q.expect(rb"^" + name + rb"\r?$")
        q.expect(PROMPT)

        def size(cols, rows):
            rows -= 2  # the bars'
            q.send("conprobe size\r")
            q.expect(rb"conprobe: size %dx%d\r?\n" % (cols, rows))  # after the request, copied by conlog
            q.expect(PROMPT)
        size(128, 96)
        q.send("font spleen-8x16\r")
        q.expect(PROMPT)
        size(128, 48)
        # Characters past ASCII in the font's own glyphs, pixel for pixel as
        # Spleen's BDF draws them: the cells hold code points.
        q.send("conprobe utf8\r")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
        time.sleep(0.3)
        shot = q.screendump("font-utf8")
        sys.path.insert(0, os.path.join(ROOT, "scripts"))
        import bdf2psf
        with open(os.path.join(ROOT, "third_party", "spleen", "spleen-8x16.bdf")) as f:
            _, _, glyphs = bdf2psf.parse_bdf(f.read())
        rows = dict(glyphs)
        top = 16  # the grid's first row, below KoraCLI's top bar
        for col, cp in enumerate((0xE9, 0x2500, 0x20AC, 0xFC)):
            for y in range(16):
                for x in range(8):
                    want = (255, 255, 255) if rows[cp][y][x] else (0, 0, 0)
                    if not near(pixel(shot, col * 8 + x, top + y), want):
                        raise Failure("U+%04X: pixel %d,%d of its cell is %s, not %s"
                                      % (cp, x, y, pixel(shot, col * 8 + x, top + y), want))
        q.send("font spleen-16x32\r")
        q.expect(PROMPT)
        size(64, 24)
        time.sleep(0.2)
        q.screendump("font-16x32")
        q.send("font nosuch\r")
        q.expect(rb"^font: cannot read sys:fonts/nosuch.psf")
        q.expect(PROMPT)
        q.send("font sys:bin/ls\r")
        q.expect(rb"^font: cannot use sys:bin/ls here")
        q.expect(PROMPT)
        size(64, 24)
        q.send("font builtin\r")
        q.expect(PROMPT)
        size(128, 96)
    yield "font: the fonts listed, the shell's console resized to each, a bad font refused", fonts

    if keyboard:
        gap = 0.1 if keyboard == "USB" else 0.02

        def press(key):
            q.monitor("sendkey %s 10" % key)
            time.sleep(gap)

        def screen_keys():
            # Keys go to the front screen's program; Meta+M is the manager's
            # and brings the next screen to front; Escape closes a probe's.
            q.send("screenprobe red &\r")
            (red_id, _), = opened(1).items()
            press("meta_l-m")
            q.expect(SHELL_FRONT)
            q.send("screenprobe blue &\r")
            (blue_id, _), = opened(1).items()
            press("a")
            q.expect(LINE + rb"screenprobe blue: key down 0x04 mods 0x0000 char 0x61")
            press("meta_l-m")
            q.expect(SHELL_FRONT)
            press("meta_l-m")
            q.expect(LINE + rb"screens: screen %d in front" % red_id)
            time.sleep(0.2)
            uniform("screens-meta-m", COLOURS[b"red"])
            press("a")
            m = q.expect(LINE + rb"screenprobe (\w+): key down 0x04 mods 0x0000 char 0x61")
            if m.group(1) != b"red":
                raise Failure("after Meta+M the key went to %s" % m.group(1).decode())
            press("esc")
            expect_both(LINE + rb"screens: screen %d in front" % blue_id, LINE + rb"screenprobe: red closed")
            press("esc")
            expect_both(SHELL_FRONT, LINE + rb"screenprobe: blue closed")
            q.send("\r")
            q.expect(PROMPT)
        yield "screens: keys to the front screen, Meta+M switches, Escape closes (%s)" % keyboard, \
            screen_keys

        def demos_on_screens():
            # condemo and gfxdemo in the background, on screens of their own
            # beside the shell's: Meta+M goes round the three.
            q.send("condemo &\r")
            q.expect(rb"condemo: windows \d+x\d+", timeout=30)
            press("meta_l-m")
            q.expect(SHELL_FRONT)
            q.send("gfxdemo &\r")
            q.expect(rb"gfxdemo: painted \d+x\d+ on screen \d", timeout=30)
            time.sleep(0.3)
            first = q.screendump("screens-demo-front")
            press("meta_l-m")
            q.expect(SHELL_FRONT)
            press("meta_l-m")
            q.expect(LINE + rb"screens: screen \d+ in front \(condemo\)")
            time.sleep(0.3)
            second = q.screendump("screens-demo-back")
            # One is gfxdemo's gradient, the other condemo's windows on black.
            shots = {}
            for shot in (first, second):
                width, height, rgb = shot
                near_black = sum(1 for i in range(0, len(rgb), 3 * 101) if max(rgb[i:i + 3]) < 16)
                shots["condemo" if near_black * 101 * 3 > len(rgb) // 2 else "gfxdemo"] = shot
            if len(shots) != 2:
                raise Failure("the two screens did not show condemo and gfxdemo")
            shot = shots["gfxdemo"]
            x, y = shot[0] - 8, shot[1] - 8
            expected = ((x * 255) // shot[0], (y * 255) // shot[1], 0)
            if not near(pixel(shot, x, y), expected):
                raise Failure("gfxdemo's screen: pixel (%d,%d) = %s" % (x, y, pixel(shot, x, y)))
            press("esc")  # condemo's, then gfxdemo's, in front after it
            q.expect(LINE + rb"screens: screen \d+ in front \(gfxdemo\)")
            press("esc")
            q.expect(SHELL_FRONT, timeout=30)
            q.send("\r")
            q.expect(PROMPT)
        yield "screens: condemo and gfxdemo on screens of their own (%s)" % keyboard, demos_on_screens

        def console_lines():
            # A console on a screen (newcon) edits the lines conprobe reads:
            # a cursor move, the history, Backspace and Home, then Ctrl+D ends
            # the input, and raw mode hands over each key's bytes. conprobe
            # reports on the serial line; the console shows its text.
            q.send("newcon conprobe\r")
            q.expect(rb"conprobe: isatty 1 1 1", timeout=30)
            time.sleep(0.3)
            for k in ("h", "i", "left", "x", "ret"):
                press(k)
            q.expect(rb"^conprobe: line <hxi>")
            for k in ("up", "ret"):
                press(k)
            q.expect(rb"^conprobe: line <hxi>")
            for k in ("a", "b", "c", "backspace", "home", "z", "ret"):
                press(k)
            q.expect(rb"^conprobe: line <zab>")
            width, height, rgb = q.screendump("newcon")
            lit = sum(1 for i in range(0, len(rgb), 3 * 5) if rgb[i:i + 3] != b"\x00\x00\x00")
            if lit < 50:
                raise Failure("the console shows no text (%d lit pixels sampled)" % lit)
            press("ctrl-d")
            q.expect(rb"^conprobe: end of input")
            time.sleep(0.2)
            for k in ("q", "up", "ret", "backspace"):
                press(k)
            q.expect(rb"^conprobe: raw 71 1b 5b 41 0a 7f")
            q.expect(rb"^conprobe: done")
            q.expect(rb"newcon: conprobe exited with 0")
            q.expect(rb"\] exited with 0")
            q.expect(PROMPT)
        yield "console: line editing, history, end of input and raw mode (%s)" % keyboard, \
            console_lines

        def console_break():
            # Ctrl+C in newcon's console kills its program.
            q.send("newcon conprobe hang\r")
            q.expect(rb"conprobe: hanging", timeout=30)
            time.sleep(0.2)
            press("ctrl-c")
            q.expect(rb"newcon: conprobe exited with -9")
            q.expect(rb"\] exited with 0")
            q.expect(PROMPT)
        yield "console: Ctrl+C kills the program (%s)" % keyboard, console_break

        def console_cat():
            # cat with no path in a console window copies the lines typed
            # until Ctrl+D.
            q.send("newcon cat\r")
            q.expect(rb"run 'cat'", timeout=30)
            time.sleep(0.3)
            for k in ("h", "i", "ret"):
                press(k)
            time.sleep(0.2)
            width, height, rgb = q.screendump("newcon-cat")
            lit = sum(1 for i in range(0, len(rgb), 3 * 5) if rgb[i:i + 3] != b"\x00\x00\x00")
            if lit < 10:
                raise Failure("cat's window shows nothing (%d lit pixels sampled)" % lit)
            press("ctrl-d")
            q.expect(rb"newcon: cat exited with 0")
            q.expect(rb"\] exited with 0")
            q.expect(PROMPT)
        yield "console: cat copies typed lines until Ctrl+D (%s)" % keyboard, console_cat

    def manager_crash():
        # The screen manager killed, as a crash would. Its programs' screens
        # are gone: the shell kills the program on its console and ends, and
        # gfxdemo ends; nothing of theirs is left drawing into memory the
        # manager no longer has. init starts screens koracli again.
        q.send("gfxdemo &\r")
        q.expect(LINE + rb"gfxdemo: painted \d+x\d+ on screen \d+", timeout=30)
        q.key("meta_l-m")
        q.expect(SHELL_FRONT)
        manager = [pid for pid, _, _, _, name in ps_table(q) if name == "screens"]
        if len(manager) != 1:
            raise Failure("ps shows screens as %s" % manager)
        q.send("kill %d\r" % manager[0])
        q.expect(LINE + rb"init: the shell ended \(exit -9\); starting it again", timeout=30)
        koracli_os_mode(q)
        for _ in range(20):
            names = [name for *_, name in ps_table(q)]
            if names.count("koracli") == 1 and names.count(SHELL_TASK) == 1 and \
                    names.count("screens") == 1 and "gfxdemo" not in names:
                break
            time.sleep(0.2)
        else:
            raise Failure("after the manager's end, ps lists %s" % names)
        q.send("hello\r")
        q.expect(rb"^Hello from userland \(ELF\)")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield ("a killed screen manager: its programs end, and init starts the shell again"), \
        manager_crash

    def shell_restart():
        # The last step: exit ends the shell and its screen, so screens ends
        # and the kernel's console has the display back; init starts them
        # again, and the new shell runs programs.
        q.send("exit\r")
        q.expect(rb"^bye")
        q.expect(rb"screens: screen \d+ closed \(koracli\), closed by its program")
        q.expect(LINE + rb"screens: done")
        q.expect(LINE + rb"init: the shell ended \(exit 0\); starting it again")
        q.expect(LINE + rb"init: starting the shell \(screens koracli\)")
        koracli_os_mode(q)
        q.send("hello\r")
        q.expect(rb"^Hello from userland \(ELF\)")
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield "exit ends the shell, and init starts it again", shell_restart


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--target", choices=("qemu_raspi3b", "qemu_raspi4b", "qemu_virt"))
    parser.add_argument("--machine", choices=("raspi3b", "virt"), help="deprecated target alias")
    parser.add_argument("--build-dir", default=os.environ.get("BUILD_DIR", os.path.join(ROOT, "build")))
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--release", action="store_true")
    mode.add_argument("--debug", action="store_true")
    parser.add_argument("--ram", default="256M")
    parser.add_argument("--el2", action="store_true", help="enter virt via EL2 before dropping to EL1")
    parser.add_argument("--gic", type=int, choices=(2, 3), default=2,
                        help="virt interrupt controller version (hvf needs 3)")
    parser.add_argument("--accel", choices=("tcg", "hvf"), default="tcg",
                        help="virt on an Apple Silicon Mac: hvf runs on the host's cores")
    parser.add_argument("--smp", type=int, default=1,
                        help="virt cores (raspi3b and raspi4b always have 4); all must come online")
    parser.add_argument("--repeat", type=int, default=0, help="repeat the EL0 page/malloc/nested-process probe")
    parser.add_argument("--stress", type=int, default=0,
                        help="rounds of the thread, kill and demo steps to repeat at the end")
    parser.add_argument("--expect-root-failure", action="store_true",
                        help="require a boot failure: a configured disk that does not mount, or (without "
                             "--disk) a kernel without a ramdisk finding no volume with /bin/init")
    parser.add_argument("--keyboard", action="store_true",
                        help="type on a keyboard: VirtIO on virt, USB on raspi3b")
    parser.add_argument("--mouse", action="store_true",
                        help="a mouse: VirtIO (and a tablet) on virt, USB on raspi3b")
    parser.add_argument("--disk", help="external bare FAT32 or MBR FAT32 disk for virt")
    parser.add_argument("--multi-volume", action="store_true", help="require scratch BOOT/EXTRAS partitions")
    parser.add_argument("--write-test", action="store_true", help="mutate a disposable writable FAT32 root")
    parser.add_argument("--disk-writable", action="store_true", help="enable raw writes to the supplied test disk")
    parser.add_argument("--no-graphics", action="store_true")
    parser.add_argument("--sd", help="raspi3b/raspi4b: SD card image (tests/create-sd-card-image.py)")
    parser.add_argument("--sd-writable", action="store_true",
                        help="attach the card writable; the kernel must be built --storage-writable")
    parser.add_argument("--ramdisk", type=int, metavar="MB",
                        help="the kernel was built --ramdisk with this many MB: check it, and write to it "
                             "when it is the root")
    parser.add_argument("--usb-stick", help="raspi3b: USB drive image (tests/create-sd-card-image.py --label USBSTICK)")
    parser.add_argument("--usb-stick-writable", action="store_true",
                        help="attach the USB drive writable; the kernel must be built --storage-writable")
    parser.add_argument("--kernel")
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--out")
    args = parser.parse_args()
    if args.machine:
        alias = "qemu_virt" if args.machine == "virt" else "qemu_raspi3b"
        if args.target and args.target != alias:
            parser.error("--target conflicts with --machine")
        args.target = alias
        print("warning: --machine is deprecated; use --target " + alias, file=sys.stderr)
    args.target = args.target or "qemu_raspi3b"
    args.machine = args.target[len("qemu_"):]
    kernel_dir = os.path.join(os.path.abspath(args.build_dir),
                              "release" if args.release else "debug", args.target)
    args.out = args.out or os.path.join(kernel_dir, "qemu-smoke")
    if args.stress < 0 or args.stress > 100:
        parser.error("--stress must be between 0 and 100")
    if args.repeat < 0 or args.repeat > 2000:
        parser.error("--repeat must be between 0 and 2000")
    if args.el2 and args.machine != "virt":
        parser.error("--el2 requires virt")
    if args.write_test and (args.machine != "virt" or not args.disk or not args.disk_writable):
        parser.error("--write-test requires virt, --disk and --disk-writable")
    if args.multi_volume and not args.disk:
        parser.error("--multi-volume requires --disk")

    if args.kernel is None:
        args.kernel = os.path.join(kernel_dir, "kernel.img")
    if not os.path.exists(args.kernel):
        sys.exit("no kernel at %s -- build it with: ./build.sh --target %s%s"
                 % (args.kernel, args.target, " --release" if args.release else ""))
    os.makedirs(args.out, exist_ok=True)

    extra = ["-device", "ramfb"] if args.machine == "virt" and not args.no_graphics else []
    if args.sd:
        if not args.machine.startswith("raspi") or not os.path.isfile(args.sd):
            parser.error("--sd requires raspi3b or raspi4b and an existing card image")
        # Read-only runs never touch the image (QEMU's SD card cannot be read-only).
        drive = "format=raw,file=%s%s" % (os.path.abspath(args.sd).replace(",", ",,"),
                                          "" if args.sd_writable else ",snapshot=on")
        if args.machine == "raspi3b":
            extra += ["-drive", "if=sd," + drive]
        else:
            # The Pi 4's slot is on EMMC2, but QEMU puts if=sd cards on the
            # Arasan controller: attach it to EMMC2's bus, the first SDHCI.
            extra += ["-drive", "if=none,id=sdcard," + drive,
                      "-device", "sd-card,drive=sdcard,bus=main-system-bus/generic-sdhci/sd-bus"]
    elif args.sd_writable:
        parser.error("--sd-writable requires --sd")
    if args.usb_stick:
        # QEMU's raspi4b has no PCIe, so no xHCI: USB is raspi3b only.
        if args.machine != "raspi3b" or not os.path.isfile(args.usb_stick):
            parser.error("--usb-stick requires raspi3b and an existing drive image")
        extra += ["-drive", "if=none,id=stick,format=raw,file=%s%s" %
                  (os.path.abspath(args.usb_stick).replace(",", ",,"),
                   "" if args.usb_stick_writable else ",snapshot=on"),
                  "-device", "usb-storage,drive=stick",
                  # A keyboard too, so its interrupt transfers (polled every
                  # frame on core 0) run beside the drive's bulk ones.
                  "-device", "usb-kbd"]
    elif args.usb_stick_writable:
        parser.error("--usb-stick-writable requires --usb-stick")
    if args.keyboard:
        # QEMU's raspi4b has no PCIe, so no xHCI and no USB keyboard.
        if args.machine == "raspi4b":
            parser.error("--keyboard requires virt or raspi3b")
        if args.machine == "virt":
            extra += ["-device", "virtio-keyboard-device"]
        elif not args.usb_stick:  # which brings a keyboard with it
            extra += ["-device", "usb-kbd"]
    if args.mouse:
        # QEMU's raspi4b has no PCIe, so no xHCI and no USB mouse.
        if args.machine == "raspi4b":
            parser.error("--mouse requires virt or raspi3b")
        if args.machine == "virt":
            extra += ["-device", "virtio-mouse-device", "-device", "virtio-tablet-device"]
        else:
            extra += ["-device", "usb-mouse"]
    if args.disk:
        if args.machine != "virt" or not os.path.isfile(args.disk):
            parser.error("--disk requires virt and an existing image")
        extra += ["-drive", "if=none,id=root,format=raw,file=%s,readonly=%s" %
                  (os.path.abspath(args.disk), "off" if args.disk_writable else "on"),
                  "-device", "virtio-blk-device,drive=root"]
    if args.smp < 1 or args.smp > 4:
        parser.error("--smp must be between 1 and 4 (KoraOS manages up to 4 cores)")
    if args.smp != 1 and args.machine != "virt":
        parser.error("--smp applies to virt (raspi3b and raspi4b always have 4 cores)")
    if (args.gic != 2 or args.accel != "tcg") and args.machine != "virt":
        parser.error("--gic and --accel apply to virt")
    if args.accel == "hvf" and (args.gic != 3 or args.el2):
        parser.error("--accel hvf needs --gic 3 and no --el2 (hvf has no EL2)")
    cores = 4 if args.machine.startswith("raspi") else args.smp
    q = Qemu(args.qemu, os.path.abspath(args.kernel), os.path.abspath(args.out), args.machine, args.ram,
             extra, args.el2, args.smp, args.gic, args.accel)
    failed = False
    try:
        try:
            # The first line names the build (git commit, "-dirty" for local
            # changes), so a log always shows which kernel ran.
            m = q.expect(rb"^KoraOS \d+\.\d+ \(([0-9a-zA-Z.+-]+)\)\r?\n", timeout=90)
            build = m.group(1).decode()
            if build == "unknown" and os.path.isdir(os.path.join(ROOT, ".git")):
                raise Failure("boot line has no build identifier in a git checkout")
            print("ok   - boot line names the build (%s)" % build)
            # The kernel heap checks itself before any later bring-up step
            # uses it; it is also the first sign the kernel booted at all.
            q.expect(rb"^\[heap\] self-test ok, \d+ of \d+ pages free", timeout=90)
            m = q.expect(rb"^smp: (\d+) cores? online", timeout=30)
            if int(m.group(1)) != cores:
                raise Failure("%s of %d cores came online" % (m.group(1).decode(), cores))
            print("ok   - %d core%s online" % (cores, "s" if cores > 1 else ""))
        except Failure as e:
            print("FAIL - kernel boots (heap self-test): %s" % e)
            print("qemu smoke: FAILED (artifacts in %s)" % args.out)
            sys.exit(1)  # QEMU is still stopped by the finally below
        if args.usb_stick:
            # Circle enumerates before the block layer comes up.
            m = q.expect(rb"^\[usb\] drive umsd1: (\d+) MB, (read-only|writable)")
            if args.mouse:
                q.expect(rb"^circle: USB mouse ready")
            q.expect(rb"^circle: USB keyboard ready")
            want = (str(os.path.getsize(args.usb_stick) // (1024 * 1024)).encode(),
                    b"writable" if args.usb_stick_writable else b"read-only")
            if m.groups() != want:
                raise Failure("USB drive line %r, want %s" % (m.group(0), want))
            print("ok   - USB drive: %s MB, %s" % (m.group(1).decode(), m.group(2).decode()))
        elif args.machine == "raspi3b" and (args.keyboard or args.mouse):
            if args.mouse:
                q.expect(rb"^circle: USB mouse ready")
                print("ok   - USB mouse ready")
            if args.keyboard:
                q.expect(rb"^circle: USB keyboard ready")
        if args.ramdisk:
            # After Circle's USB lines, before the SD card and VirtIO probes.
            m = q.expect(rb"^\[ramdisk\] (\d+) MB at 0x[0-9a-f]+, read-write")
            if int(m.group(1)) != args.ramdisk:
                raise Failure("a %s MB ramdisk, want %d MB" % (m.group(1).decode(), args.ramdisk))
            print("ok   - %d MB ramdisk laid out, read-write" % args.ramdisk)
        if args.disk:
            q.expect(rb"\[blkdev\] using VirtIO disk")
        if args.machine.startswith("raspi"):
            # The SD slot: a card announces itself, an empty slot says so.
            m = q.expect(rb"^\[sd\] (no card|(SDSC|SDHC/SDXC) card, (\d+) MB, (read-only|writable))")
            if args.sd is None and m.group(1) != b"no card":
                raise Failure("an SD card showed up without one attached: %s" % m.group(1).decode())
            if args.sd is not None:
                mb = os.path.getsize(args.sd) // (1024 * 1024)
                want = (b"SDHC/SDXC" if mb > 2048 else b"SDSC", str(mb).encode(),
                        b"writable" if args.sd_writable else b"read-only")
                if (m.group(2), m.group(3), m.group(4)) != want:
                    raise Failure("SD card line %r, want %s" % (m.group(1), want))
            print("ok   - SD slot: %s" % m.group(1).decode())
        if args.expect_root_failure and not args.disk:
            q.expect(rb"fat32: no volume has /bin/init")
            q.expect(rb"kernel: failed to load /bin/init")
            print("ok   - without a ramdisk or a volume with /bin/init, boot stops with a message")
            return
        if args.expect_root_failure:
            q.expect(rb"fat32: mount failed:")
            q.expect(rb"kernel: failed to load /bin/init")
            print("ok   - configured invalid disk fails without ramdisk fallback")
            return
        if args.keyboard and args.machine == "virt":
            q.expect(rb"\[virtio-input\] keyboard ready")
        if args.mouse and args.machine == "virt":
            q.expect(rb"\[virtio-input\] mouse ready")
            q.expect(rb"\[virtio-input\] tablet ready")
            print("ok   - VirtIO mouse and tablet ready")
        media = ([("SD card", "SDCARD", args.sd_writable)] if args.sd else []) + \
            ([("USB drive", "USBSTICK", args.usb_stick_writable)] if args.usb_stick else [])
        keyboard = ("VirtIO" if args.machine == "virt" else "USB") if args.keyboard else None
        mouse = ("VirtIO" if args.machine == "virt" else "USB") if args.mouse else None
        for name, check in run(q, not args.no_graphics, keyboard, args.repeat, args.multi_volume,
                               args.write_test or (args.ramdisk and not args.disk), cores, args.stress,
                               media, mouse):
            try:
                check()
                print("ok   - " + name)
            except Failure as e:
                print("FAIL - %s: %s" % (name, e))
                failed = True
                break  # later steps depend on the shell's state
    finally:
        q.close()
    print("qemu smoke: %s (artifacts in %s)" % ("FAILED" if failed else "all passed", args.out))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
