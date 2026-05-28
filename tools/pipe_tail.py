"""Dump raw NDJSON records from the skygraph pipe to stdout.

Usage:
    python pipe_tail.py [--pipe \\.\pipe\skygraph]

Use this to verify the wire stream is healthy when the viewer is misbehaving.
"""

from __future__ import annotations

import argparse
import sys
import time
import ctypes
import ctypes.wintypes as wt

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
PIPE_READMODE_MESSAGE = 0x00000002
ERROR_PIPE_BUSY = 231
ERROR_MORE_DATA = 234

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


def open_pipe(name: str) -> wt.HANDLE:
    while True:
        h = kernel32.CreateFileA(
            name.encode("ascii"),
            GENERIC_READ | GENERIC_WRITE,
            0,
            None,
            OPEN_EXISTING,
            0,
            None,
        )
        if h != -1 and h != 0:
            mode = wt.DWORD(PIPE_READMODE_MESSAGE)
            kernel32.SetNamedPipeHandleState(h, ctypes.byref(mode), None, None)
            return h
        err = ctypes.get_last_error()
        if err == ERROR_PIPE_BUSY:
            kernel32.WaitNamedPipeA(name.encode("ascii"), 1000)
        else:
            print(f"waiting for pipe (err={err})...", file=sys.stderr)
            time.sleep(1.0)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pipe", default=r"\\.\pipe\skygraph")
    args = ap.parse_args()

    h = open_pipe(args.pipe)
    print(f"connected to {args.pipe}", file=sys.stderr)
    buf = ctypes.create_string_buffer(64 * 1024)
    read = wt.DWORD(0)
    pending = b""
    while True:
        ok = kernel32.ReadFile(h, buf, len(buf), ctypes.byref(read), None)
        if not ok and ctypes.get_last_error() not in (0, ERROR_MORE_DATA):
            print("disconnected", file=sys.stderr)
            return 1
        if read.value == 0:
            continue
        pending += buf.raw[: read.value]
        while b"\n" in pending:
            line, _, pending = pending.partition(b"\n")
            if line:
                sys.stdout.buffer.write(line + b"\n")
                sys.stdout.buffer.flush()


if __name__ == "__main__":
    raise SystemExit(main())
