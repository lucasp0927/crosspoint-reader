#!/usr/bin/env python3
"""Capture the reader's serial log to a file.

`pio device monitor` cannot be redirected — it calls termios.tcgetattr on stdout
and dies with "Operation not supported by device" when that is not a TTY. This
reads the port directly instead, so it works from a script.

  capture.py <outfile> [seconds] [port]

Writes incrementally and flushes each line, so the file is readable while the
capture is still running.
"""
import sys
import time

import serial

out_path = sys.argv[1] if len(sys.argv) > 1 else "log-flash.txt"
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 900.0
port = sys.argv[3] if len(sys.argv) > 3 else "/dev/cu.usbmodem1101"

deadline = time.time() + duration
# USB CDC ignores baud, but pyserial needs a value. Short timeout so we notice
# the deadline even when the device goes quiet.
with serial.Serial(port, 115200, timeout=1) as ser, open(out_path, "w", encoding="utf-8") as out:
    while time.time() < deadline:
        try:
            chunk = ser.readline()
        except serial.SerialException as exc:  # device unplugged or slept
            out.write(f"[capture] serial error: {exc}\n")
            break
        if not chunk:
            continue
        out.write(chunk.decode("utf-8", "replace"))
        out.flush()
    out.write("[capture] done\n")
