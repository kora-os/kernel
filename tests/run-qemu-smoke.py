#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""QEMU smoke test: boot the QEMU kernel on the raspi3b machine, drive the
shell over the serial line, and check the output and the framebuffer.

Waits for expected output (with timeouts) rather than sleeping, so it is not
sensitive to how fast the machine runs QEMU. Standard library only.

Usage: tests/run-qemu-smoke.py --target qemu_raspi3b|qemu_virt [--out DIR]
Build the kernel first with: ./build.sh --target qemu_raspi3b

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
    def __init__(self, qemu, kernel, outdir, machine="raspi3b", ram="256M", extra=(), el2=False):
        self.outdir = outdir
        self.log = open(os.path.join(outdir, "serial.log"), "wb")
        self.pending = b""  # output not yet consumed by expect()
        # The monitor socket path is relative to QEMU's working directory:
        # UNIX socket paths are limited to ~104 bytes and temp dirs are long.
        machine_args = ["-M", "raspi3b"] if machine == "raspi3b" else [
            "-M", "virt,gic-version=2,highmem=off" + (",virtualization=on" if el2 else ""), "-cpu", "cortex-a72",
            "-smp", "1", "-nic", "none", "-global", "virtio-mmio.force-legacy=false", "-m", ram]
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


def run(q, graphics=True, keyboard=False, repeat=0, multi_volume=False):
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
        q.expect(rb"shell: too many arguments")
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
            q.expect(rb"shell: no such program: hello")
            q.expect(PROMPT)
            command("assign c")
            q.send("hello\r")
            q.expect(rb"shell: no such program: hello")
            q.expect(PROMPT)
            command("assign c sys:bin")
            command("sys:")
            pwd(root)
    yield "shell navigation, volume listing and single-target assigns", shell_navigation

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

    def kernel_heap():
        def heap_stats():
            q.send("heap\r")
            m = q.expect(rb"^heap: (\d+) live, \d+ allocated, (\d+) failed, (\d+) bad frees")
            live, failed, bad = (int(g) for g in m.groups())
            m = q.expect(rb"^pages: (\d+) of (\d+) free")
            q.expect(rb"koraos> ")
            return live, failed, bad, int(m.group(1))

        q.send("\x14")
        q.expect(rb"koraos> ")
        live, failed, bad, pages = heap_stats()
        for _ in range(4):  # each run seeds from the uptime
            q.send("heaptest 3000\r")
            m = q.expect(rb"^heaptest: (?:3000 rounds ok|FAILED check (\d+))", timeout=60)
            if m.group(1):
                raise Failure("heaptest failed check %s" % m.group(1).decode())
            q.expect(rb"koraos> ")
        live2, failed2, bad2, pages2 = heap_stats()
        if (live2, failed2, bad2) != (live, failed, bad):
            raise Failure("heap changed across heaptest: live/failed/bad %s -> %s"
                          % ((live, failed, bad), (live2, failed2, bad2)))
        if bad:
            raise Failure("%d bad kfree calls since boot" % bad)
        # At most one cached empty slab per size class may stay behind.
        if pages2 < pages - 9:
            raise Failure("heaptest leaked pages: %d -> %d free" % (pages, pages2))
        q.send("\x14")
        q.expect(rb"\[serial -> screen terminal")
    yield "kernel heap stress leaves no leaks", kernel_heap

    def fp_context():
        q.send("fpprobe\r")
        m = q.expect(rb"^fpprobe: ([^\r\n]*)\r?\n")
        if m.group(1) != b"registers preserved":
            raise Failure("fpprobe: %s" % m.group(1).decode(errors="replace"))
        q.expect(rb"exited with 0")
        q.expect(PROMPT)
    yield "FP/SIMD registers are per task (lazy switching)", fp_context

    def tasks():
        q.send("\x14")
        q.expect(rb"koraos> ")
        q.send("tasks\r")
        listed = {}
        while True:
            m = q.expect(rb"^(?:\s+(\d+)\s+(\w+)\s+(\d+)\s+(\S+)|kernel stack peak: (\d+) of (\d+) bytes)\r?\n")
            if m.group(5):
                peak, size = int(m.group(5)), int(m.group(6))
                break
            listed[m.group(4).decode()] = (m.group(2).decode(), int(m.group(3)))
        q.expect(rb"koraos> ")
        q.send("\x14")
        q.expect(rb"\[serial -> screen terminal")
        if listed.get("init", ("",))[0] != "blocked" or listed.get("shell", ("",))[0] != "runnable":
            raise Failure("expected blocked init and runnable shell, got %s" % listed)
        # Every task so far, the probes included: keep a quarter of the stack spare.
        if peak == 0 or peak > size * 3 // 4:
            raise Failure("kernel stack peak %d of %d bytes" % (peak, size))
    yield "tasks lists init and the shell; kernel stacks have headroom", tasks

    if repeat:
        def kernel_counts():
            q.send("\x14")
            q.expect(rb"koraos> ")
            q.send("heap\r")
            m = q.expect(rb"^heap: (\d+) live")
            live = int(m.group(1))
            m = q.expect(rb"^pages: (\d+) of \d+ free")
            q.expect(rb"koraos> ")
            q.send("\x14")
            q.expect(rb"\[serial -> screen terminal")
            return live, int(m.group(1))

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
            for key in ("e", "c", "h", "o", "spc", "shift-a", "b", "c", "backspace", "ret"):
                reply = q.monitor("sendkey " + key + " 10")
                if b"Error" in reply or b"unknown" in reply.lower():
                    raise Failure("QEMU key injection failed: %r" % reply)
                time.sleep(0.02)
            q.expect(rb"^Ab\r?$")
            q.expect(rb"exited with 1")
            q.expect(PROMPT)
            q.monitor("sendkey shift-pgup 10")
            time.sleep(0.02)
            q.monitor("sendkey shift-pgdn 10")
        yield "VirtIO keyboard handles Shift, release, editing and Enter", keyboard_input

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
    parser.add_argument("--target", choices=("qemu_raspi3b", "qemu_virt"))
    parser.add_argument("--machine", choices=("raspi3b", "virt"), help="deprecated target alias")
    parser.add_argument("--build-dir", default=os.environ.get("BUILD_DIR", os.path.join(ROOT, "build")))
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--release", action="store_true")
    mode.add_argument("--debug", action="store_true")
    parser.add_argument("--ram", default="256M")
    parser.add_argument("--el2", action="store_true", help="enter virt via EL2 before dropping to EL1")
    parser.add_argument("--repeat", type=int, default=0, help="repeat the EL0 page/malloc/nested-process probe")
    parser.add_argument("--expect-root-failure", action="store_true", help="require a configured disk mount failure")
    parser.add_argument("--keyboard", action="store_true", help="inject keys through a VirtIO keyboard")
    parser.add_argument("--disk", help="external bare FAT32 or MBR FAT32 disk for virt")
    parser.add_argument("--multi-volume", action="store_true", help="require scratch BOOT/EXTRAS partitions")
    parser.add_argument("--disk-writable", action="store_true", help="enable raw writes to the supplied test disk")
    parser.add_argument("--no-graphics", action="store_true")
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
    args.machine = "virt" if args.target == "qemu_virt" else "raspi3b"
    kernel_dir = os.path.join(os.path.abspath(args.build_dir),
                              "release" if args.release else "debug", args.target)
    args.out = args.out or os.path.join(kernel_dir, "qemu-smoke")
    if args.repeat < 0 or args.repeat > 2000:
        parser.error("--repeat must be between 0 and 2000")
    if args.el2 and args.machine != "virt":
        parser.error("--el2 requires virt")
    if args.multi_volume and not args.disk:
        parser.error("--multi-volume requires --disk")
    if args.expect_root_failure and not args.disk:
        parser.error("--expect-root-failure requires --disk")

    if args.kernel is None:
        args.kernel = os.path.join(kernel_dir, "kernel.img")
    if not os.path.exists(args.kernel):
        sys.exit("no kernel at %s -- build it with: ./build.sh --target %s%s"
                 % (args.kernel, args.target, " --release" if args.release else ""))
    os.makedirs(args.out, exist_ok=True)

    extra = ["-device", "ramfb"] if args.machine == "virt" and not args.no_graphics else []
    if args.keyboard:
        if args.machine != "virt":
            parser.error("--keyboard requires virt")
        extra += ["-device", "virtio-keyboard-device"]
    if args.disk:
        if args.machine != "virt" or not os.path.isfile(args.disk):
            parser.error("--disk requires virt and an existing image")
        extra += ["-drive", "if=none,id=root,format=raw,file=%s,readonly=%s" %
                  (os.path.abspath(args.disk), "off" if args.disk_writable else "on"),
                  "-device", "virtio-blk-device,drive=root"]
    q = Qemu(args.qemu, os.path.abspath(args.kernel), os.path.abspath(args.out), args.machine, args.ram, extra, args.el2)
    failed = False
    try:
        # The kernel heap checks itself before any later bring-up step uses it.
        q.expect(rb"^\[heap\] self-test ok, \d+ of \d+ pages free", timeout=90)
        if args.disk:
            q.expect(rb"\[blkdev\] using VirtIO disk")
        if args.expect_root_failure:
            q.expect(rb"fat32: mount failed:")
            q.expect(rb"kernel: failed to load /bin/init")
            print("ok   - configured invalid disk fails without ramdisk fallback")
            return
        if args.keyboard:
            q.expect(rb"\[virtio-input\] keyboard ready")
        for name, check in run(q, not args.no_graphics, args.keyboard, args.repeat, args.multi_volume):
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
