#!/usr/bin/env python3
"""Smoke-test the terminal backend's LIVE path in a pseudo-terminal.

Spawns `lcl_lk_tui examples/tour-dsl.lcl` on a pty, plays a terminal
well enough to read the screen back (cursor moves, SGR, clear, the
private modes, the kitty keyboard query), and asserts what a user
would see: the alternate screen and the input modes turn on, the
Tour's first page renders, `n` and a mouse click change pages, typed
and bracketed-pasted text land in the text input, a resize repaints,
Ctrl+Q (sent as a kitty CSI-u key) quits with every mode restored.

Usage: tools/tui-pty-smoke.py [path/to/lcl_lk_tui]   (exit 0 = pass)
Headless CI has no controlling terminal, which is exactly why this
uses a pty: it is the only automated run of the tty half of
src/tui/lk-tui.c.  Set PTY_SMOKE_VERBOSE=1 to print every screen.
"""
import fcntl
import base64
import os
import pty
import re
import select
import signal
import struct
import sys
import termios
import time

OSC = re.compile(rb"\x1b\]([^\x07\x1b]*)(?:\x07|\x1b\\)")
CSI = re.compile(rb"\x1b\[([?<>=]?)([0-9;:]*)([\x20-\x2f]*)([\x40-\x7e])")
WIDE_EXEMPT = (range(0x2500, 0x2600), range(0x2800, 0x2900))


class Screen:
    """The few things lk_tui emits: CUP, SGR, ED 2, private modes."""

    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.x = self.y = 0
        self.alt = False
        self.cursor_visible = True
        self.kitty_flags = None
        self.alt_leaves = 0  # how often the alternate screen was left
        self.osc52 = None  # last OSC 52 payload, decoded
        self.modes = set()
        self.pending = b""
        self.clear()

    def clear(self):
        self.cells = [[" "] * self.cols for _ in range(self.rows)]

    def feed(self, data):
        data = self.pending + data
        self.pending = b""
        i = 0

        while i < len(data):
            b = data[i]

            if b == 0x1B:
                o = OSC.match(data, i)

                if o:
                    if o.group(1).startswith(b"52;"):
                        self.osc52 = base64.b64decode(o.group(1).split(b";", 2)[2]).decode()

                    i = o.end()
                    continue

                m = CSI.match(data, i)

                if m:
                    self.csi(m.group(1), m.group(2).decode(), m.group(4))
                    i = m.end()
                    continue

                if len(data) - i < 96:
                    self.pending = data[i:]
                    break

                i += 1
                continue

            if b < 0x20 or b == 0x7F:
                i += 1
                continue

            n = 1 if b < 0x80 else 2 if b & 0xE0 == 0xC0 else 3 if b & 0xF0 == 0xE0 else 4

            if i + n > len(data):
                self.pending = data[i:]
                break

            ch = data[i : i + n].decode("utf-8", "replace")[:1] or " "
            i += n

            if 0 <= self.y < self.rows and 0 <= self.x < self.cols:
                self.cells[self.y][self.x] = ch

            wide = ord(ch) >= 0x1100 and not any(ord(ch) in r for r in WIDE_EXEMPT)
            self.x += 2 if wide else 1

    def csi(self, priv, params, final):
        nums = [int(p) if p else 0 for p in params.split(";")] if params else []

        if priv == b"?":
            if final == b"h":
                self.modes |= set(nums)
            elif final == b"l":
                self.modes -= set(nums)

            if 1049 in nums:
                self.alt = final == b"h"
                self.alt_leaves += final == b"l"

            if 25 in nums:
                self.cursor_visible = final == b"h"

            return

        if priv == b">" and final == b"u":
            self.kitty_flags = nums[0] if nums else 0
            return

        if priv == b"<" and final == b"u":
            self.kitty_flags = None
            return

        if final == b"H":
            self.y = (nums[0] if len(nums) > 0 and nums[0] else 1) - 1
            self.x = (nums[1] if len(nums) > 1 and nums[1] else 1) - 1
        elif final == b"J" and nums and nums[0] == 2:
            self.clear()

    def text(self):
        return "\n".join("".join(r).rstrip() for r in self.cells)


def pump(fd, scr, quiet=0.3, hard=3.0, reply_kitty=False, capture=None):
    """Read until the child has been quiet for `quiet` seconds."""
    deadline = time.time() + quiet
    stop = time.time() + hard
    total = 0

    while time.time() < stop:
        r, _, _ = select.select([fd], [], [], max(0.0, min(deadline, stop) - time.time()))

        if not r:
            break

        try:
            d = os.read(fd, 65536)
        except OSError:
            break

        if not d:
            break

        total += len(d)
        scr.feed(d)

        if capture is not None:
            capture.append(d)
        deadline = time.time() + quiet

        if reply_kitty and b"\x1b[?u" in d:
            os.write(fd, b"\x1b[?0u")  # "I speak the kitty protocol"

        if b"\x1b[c" in d:
            os.write(fd, b"\x1b[?62;22c")  # DA1: every terminal answers this

    return total


failures = []
verbose = os.environ.get("PTY_SMOKE_VERBOSE")


def check(cond, what):
    if not cond:
        failures.append(what)
    print(("ok   " if cond else "FAIL ") + what)


def find(scr, text):
    """1-based (col, row) of the first cell of `text` on the screen, or None."""
    for y, row in enumerate(scr.cells):
        x = "".join(row).find(text)

        if x >= 0:
            return x + 1, y + 1

    return None


def click(fd, at):
    x, y = at
    os.write(fd, b"\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (x, y, x, y))


def spawn(argv, cols, rows, env):
    pid, fd = pty.fork()

    if pid == 0:
        os.environ.update(env)
        os.execvp(argv[0], argv)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    return pid, fd


def wait_exit(pid):
    for _ in range(20):
        wpid, status = os.waitpid(pid, os.WNOHANG)

        if wpid:
            return status

        time.sleep(0.1)

    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)

    return -1


def scenario_picker(binary):
    """The DSL's file-picker fallback: a terminal has no native dialogs,
    so Lk::open_file_dialog / save_file_dialog open the prelude's modal."""
    print("-- file picker fallback (tools/tui-picker-smoke.lcl)")
    scr = Screen(80, 24)
    pid, fd = spawn([binary, "tools/tui-picker-smoke.lcl"], 80, 24, {"COLORTERM": "truecolor"})
    pump(fd, scr, quiet=0.5)
    check("result: none" in scr.text() and "caps: clipboard pointer" in scr.text(),
          "picker app up; caps report no file_dialogs")

    click(fd, find(scr, "Open..."))
    pump(fd, scr)
    check("Open file" in scr.text() and "/tmp/x.txt" in scr.text() and "Text (txt)" in scr.text(),
          "Open... raised the picker modal with the default path and filter hint")
    os.write(fd, b"\x1b[F2\r")  # End, type, Return
    pump(fd, scr)
    check("result: opened /tmp/x.txt2" in scr.text() and "Open file" not in scr.text(),
          "Return completed the handler with the typed path and closed the modal")

    click(fd, find(scr, "Open..."))
    pump(fd, scr)
    os.write(fd, b"\x1b")  # a lone ESC: core pops the modal
    pump(fd, scr, quiet=0.5)
    check("result: cancelled" in scr.text() and "Open file" not in scr.text(),
          "ESC cancelled: handler got an empty list")

    click(fd, find(scr, "Save..."))
    pump(fd, scr)
    check("Save file" in scr.text() and "out.txt" in scr.text(), "Save... raised the save twin")
    click(fd, find(scr, "Cancel"))
    pump(fd, scr)
    check("Save file" not in scr.text() and "result: cancelled" in scr.text(),
          "Cancel button closed it with an empty list")

    os.write(fd, b"\x11")  # legacy ctrl+q
    status = wait_exit(pid)
    check(os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0 if status != -1 else False,
          "picker app quit with status 0")


def scenario_edit_no_blank(binary):
    """A keystroke in an editor must not paint the editor empty for a
    frame.  Input lands after layout; the editor drops its geometry on
    every document change, so the step has to lay out again before it
    renders (the bug: one blank frame, then a full repaint)."""
    print("-- editing never blanks the editor (Tour, Editor page)")
    scr = Screen(100, 37)
    pid, fd = spawn([binary, "examples/tour-dsl.lcl"], 100, 37, {"COLORTERM": "truecolor"})
    pump(fd, scr, quiet=0.5)
    os.write(fd, b"nnn")  # text -> inputs -> layout -> editor (focuses it)
    pump(fd, scr, quiet=0.5)
    check("A second paragraph" in scr.text(), "Editor page up")

    chunks = []
    os.write(fd, b"x")
    pump(fd, scr_after := Screen(100, 37), capture=chunks)
    flushes = b"".join(chunks).split(b"\x1b[?25l")[1:]
    seen = []

    for part in flushes:  # replay the keystroke one flush at a time
        scr.feed(part)
        seen.append("A second paragraph" in scr.text())

    check(len(flushes) > 0 and all(seen),
          f"the paragraph stayed on screen through every flush of one keystroke ({seen})")
    check("xThe editor widget" in scr.text(), "and the typed character landed")
    os.write(fd, b"\x11")
    status = wait_exit(pid)
    check(status != -1 and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
          "quit with status 0")


def scenario_256(binary):
    """LK_TUI_COLOR=256: SGR uses the xterm palette, never 38;2."""
    print("-- 256-colour fallback")
    scr = Screen(80, 24)
    pid, fd = spawn([binary, "examples/tour-dsl.lcl"], 80, 24, {"LK_TUI_COLOR": "256"})
    chunks = []
    pump(fd, scr, quiet=0.5, capture=chunks)
    raw = b"".join(chunks)
    check(b";38;5;" in raw and b";38;2;" not in raw and b";48;5;" in raw,
          "first frame painted with 256-colour SGR only")
    check("Labels and buttons" in scr.text(), "Tour rendered under the 256-colour palette")
    os.write(fd, b"\x11")
    status = wait_exit(pid)
    check(status != -1 and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
          "quit with status 0")


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/lcl_lk_tui"
    cols, rows = 100, 37

    def show(label):
        if verbose:
            print(f"---- {label}\n{scr.text()}\n----")

    scr = Screen(cols, rows)
    pid, fd = spawn([binary, "examples/tour-dsl.lcl"], cols, rows, {"COLORTERM": "truecolor"})
    pump(fd, scr, quiet=0.5, reply_kitty=True)
    show("first frame")
    check(scr.alt, "alternate screen on")
    check(not scr.cursor_visible, "cursor hidden while nothing is focused")
    check({1003, 1006, 2004} <= scr.modes, "mouse (any-event, SGR) and bracketed paste on")
    check(scr.kitty_flags == 3, "kitty keyboard protocol pushed after the query reply")
    check("Labels and buttons" in scr.text(), "first Tour page rendered")
    check("│ Plain │" in scr.text(), "framed button with its label on its own row")

    os.write(fd, b"n")  # next page
    pump(fd, scr)
    show("after n")
    check("checkbox [] alpha" in scr.text(), "'n' key switched to the Inputs page")
    check(scr.cursor_visible, "hardware cursor shown once the text input has focus")

    # click the text input (row 6, col 14), type, then a bracketed paste
    os.write(fd, b"\x1b[<0;14;7M\x1b[<0;14;7m")
    pump(fd, scr)
    os.write(fd, b"hi")
    pump(fd, scr)
    os.write(fd, b"\x1b[200~one\rtwo\x1b[201~")
    pump(fd, scr)
    show("after typing and paste")
    check("typed: hione·two" in scr.text(), "typed text and ONE paste event reached the field")

    # select "two" (shift+left x3), copy: the in-process clipboard takes
    # it and OSC 52 hands it to the terminal; End, paste: back in
    os.write(fd, b"\x1b[1;2D\x1b[1;2D\x1b[1;2D\x03")
    pump(fd, scr)
    check(scr.osc52 == "two", f"ctrl+c wrote the selection to the terminal clipboard via OSC 52 (got {scr.osc52!r})")
    os.write(fd, b"\x1b[F\x16")
    pump(fd, scr)
    show("after copy/paste")
    check("typed: hione·twotwo" in scr.text(), "ctrl+v pasted from the in-process clipboard")

    # click the Layout tab header (row 2, col 20)
    os.write(fd, b"\x1b[<0;20;3M\x1b[<0;20;3m")
    pump(fd, scr)
    show("after clicking Layout")
    check("grid, 3 columns" in scr.text(), "mouse click on a tab header switched pages")

    # resize to 80x24: a full repaint at the new size
    cols, rows = 80, 24
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    scr.cols, scr.rows = cols, rows
    scr.clear()
    os.kill(pid, signal.SIGWINCH)
    n = pump(fd, scr)
    show("after resize")
    check(n > 1000 and "grid, 3 columns" in scr.text(), "SIGWINCH repainted at the new size")

    # an unhandled ctrl+z (legacy byte 0x1A; the Layout page has no
    # editor to claim it for undo) suspends: the screen is restored and
    # SIGTSTP raised.  Our process group is orphaned (the harness is in
    # another session), so the kernel discards the stop and the backend
    # re-enters its modes at once -- which is exactly the SIGCONT path.
    os.write(fd, b"\x1a")
    pump(fd, scr)
    show("after ctrl+z")
    check(scr.alt_leaves == 1 and scr.alt and scr.kitty_flags == 3
          and {1003, 1006, 2004} <= scr.modes,
          "unhandled ctrl+z left the screen for SIGTSTP and re-entered on CONT")
    check("grid, 3 columns" in scr.text(), "full repaint after CONT")

    # a kitty key RELEASE of ctrl+q must not quit; the press must
    os.write(fd, b"\x1b[113;5:3u")
    pump(fd, scr)
    _, status = os.waitpid(pid, os.WNOHANG)
    check(_ == 0, "ctrl+q key release (kitty) did not quit")
    os.write(fd, b"\x1b[113;5u")
    pump(fd, scr, quiet=0.5)

    for _ in range(20):
        wpid, status = os.waitpid(pid, os.WNOHANG)

        if wpid:
            break

        time.sleep(0.1)
    else:
        os.kill(pid, signal.SIGKILL)
        wpid, status = 0, -1

    check(wpid == pid and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
          f"ctrl+q (kitty press) quit with status 0 (got {status})")
    check(not scr.alt, "main screen restored on exit")
    check(scr.cursor_visible, "cursor visible on exit")
    check(not ({1003, 1006, 2004} & scr.modes), "mouse and paste modes off on exit")
    check(scr.kitty_flags is None, "kitty flags popped on exit")

    scenario_picker(binary)
    scenario_edit_no_blank(binary)
    scenario_256(binary)

    if failures:
        print(f"\n{len(failures)} FAILED")
        return 1

    print("\nall passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
