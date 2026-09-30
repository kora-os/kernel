#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""QEMU smoke test: boot the QEMU kernel on the raspi3b machine, drive the
shell over the serial line, and check the output and the framebuffer.

Waits for expected output (with timeouts) rather than sleeping, so it is not
sensitive to how fast the machine runs QEMU. Standard library only.

Usage: tests/run-qemu-smoke.py [--kernel build/kernel8.img] [--out DIR]
Build the kernel first with: RPI_VERSION=3 ./build.sh --qemu

Artifacts in --out (default build/qemu-smoke): serial.log and one PNG
screenshot per screen check.
"""

import argparse
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
    def __init__(self, qemu, kernel, outdir, machine="raspi3b", ram="256M", extra=()):
        self.outdir = outdir
        self.log = open(os.path.join(outdir, "serial.log"), "wb")
        self.pending = b""  # output not yet consumed by expect()
        # The monitor socket path is relative to QEMU's working directory:
        # UNIX socket paths are limited to ~104 bytes and temp dirs are long.
        machine_args = ["-M", "raspi3b"] if machine == "raspi3b" else [
            "-M", "virt,gic-version=2,highmem=off", "-cpu", "cortex-a72",
            "-smp", "1", "-nic", "none", "-m", ram]
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
            if FAULT.search(self.pending):
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

    def send(self, text):
        self.proc.stdin.write(text.encode())
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


def run(q, graphics=True):
    """Yield (name, check) steps; each check raises Failure on error."""

    def boot():
        q.expect(rb"KoraOS shell\. Type 'help'\.", timeout=90)
        q.expect(PROMPT)
    yield "boots to the shell", boot

    def ls():
        q.send("ls /bin\r")
        for name in (b"hello", b"shell", b"termdemo"):
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
    yield "cat reads a long-filename file", cat

    def debug_console():
        q.send("\x14")  # Ctrl-T
        q.expect(rb"\[serial -> kernel console")
        q.expect(rb"koraos> ")
        q.send("version\r")
        q.expect(rb"KoraOS version \d")
        q.send("\x14")
        q.expect(rb"\[serial -> screen terminal")
    yield "Ctrl-T reaches the kernel debug console and back", debug_console

    def irqs():
        q.send("\x14")
        q.expect(rb"koraos> ")
        deadline = time.monotonic() + 5
        while True:
            q.send("irqs\r")
            q.expect(rb"^controller: (?:BCM2835 legacy|GICv2 \(virt\))")
            m = q.expect(rb"^systick: (\d+) ticks, uptime (\d+)\.(\d+) s")
            ticks = int(m.group(1))
            uptime = int(m.group(2)) + int(m.group(3)) / 1000
            if ticks > uptime * 100 + 1:
                raise Failure("systick %d ticks after %.3f s uptime" % (ticks, uptime))
            m = q.expect(rb"^\s+\d+\s+(\d+)\s+uart \(PL011\)")
            if int(m.group(1)) == 0:
                raise Failure("the UART receive interrupt never fired")
            q.expect(rb"koraos> ")
            if ticks >= 5:
                break
            if time.monotonic() >= deadline:
                raise Failure("system tick never advanced to five interrupts")
            time.sleep(0.05)
        q.send("\x14")
        q.expect(rb"\[serial -> screen terminal")
    yield "irqs shows the system tick and the UART interrupt", irqs

    if not graphics:
        return

    def termdemo():
        q.send("termdemo\r")
        q.expect(rb"Shift\+PgUp")
        q.expect(PROMPT)
        shot = q.screendump("termdemo")
        colours = {shot[2][i:i + 3] for i in range(0, len(shot[2]), 3)}
        if len(colours) < 200:
            raise Failure("expected the 256/24-bit colour tables on screen, "
                          "found only %d distinct colours" % len(colours))
    yield "termdemo renders colours on the screen", termdemo

    def gfxdemo():
        q.send("gfxdemo\r")
        q.expect(rb"gfxdemo: painted (\d+)x(\d+)")
        q.expect(PROMPT)
        shot = q.screendump("gfxdemo")
        width, height, _ = shot
        # gfxdemo paints red rising with x and green rising with y. Sample
        # below the rows the shell prints on.
        for x, y in ((8, 700), (width - 8, 700), (width - 8, 300)):
            expected = ((x * 255) // width, (y * 255) // height, 0)
            if not near(pixel(shot, x, y), expected):
                raise Failure("pixel (%d,%d) = %s, expected about %s"
                              % (x, y, pixel(shot, x, y), expected))
    yield "gfxdemo paints the framebuffer", gfxdemo


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--machine", choices=("raspi3b", "virt"), default="raspi3b")
    parser.add_argument("--ram", default="256M")
    parser.add_argument("--no-graphics", action="store_true")
    parser.add_argument("--kernel")
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--out", default=os.path.join(ROOT, "build", "qemu-smoke"))
    args = parser.parse_args()

    if args.kernel is None:
        args.kernel = os.path.join(ROOT, "build", "kernel-virt.img" if args.machine == "virt" else "kernel8.img")
    if not os.path.exists(args.kernel):
        sys.exit("no kernel at %s -- build it with: RPI_VERSION=3 ./build.sh --qemu"
                 % args.kernel)
    os.makedirs(args.out, exist_ok=True)

    extra = ["-device", "ramfb"] if args.machine == "virt" and not args.no_graphics else []
    q = Qemu(args.qemu, os.path.abspath(args.kernel), os.path.abspath(args.out), args.machine, args.ram, extra)
    failed = False
    try:
        for name, check in run(q, not args.no_graphics):
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
