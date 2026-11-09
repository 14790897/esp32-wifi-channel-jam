import re
import serial
import threading
import time
import sys

VICTIM = "COM6"
SCAN = "COM19"
JAMMERS = [("J1", "COM4"), ("J2", "COM5")]
BASE_S = 5
JAM_S = 10
TAIL_S = 10


class Reader:
    def __init__(self, name, port):
        self.name = name
        self.port = port
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        self.lines = []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        while not self.stop.is_set():
            try:
                line = self.ser.readline()
            except Exception:
                break
            if line:
                s = line.decode("utf-8", "replace").strip()
                if s:
                    self.lines.append((time.time(), s))
                    print(f"[{self.name}] {s}", flush=True)

    def send(self, cmd):
        try:
            self.ser.write((cmd + "\r\n").encode())
        except Exception as e:
            print(f"[{self.name}] write failed: {e}", flush=True)


def main():
    scan = Reader("scan", SCAN)
    victim = Reader("victim", VICTIM)
    time.sleep(1)
    print("baseline...", flush=True)
    time.sleep(BASE_S)
    base = [l for _, l in scan.lines if "bcn/s=" in l]
    print(f"baseline scanner: {base[-2:]}", flush=True)

    jammers = [Reader(n, p) for n, p in JAMMERS]
    time.sleep(0.5)
    for j in jammers:
        j.send(f"start 13 {JAM_S}")
    t0 = time.time()
    print("=== JAM START ===", flush=True)
    time.sleep(JAM_S + TAIL_S)

    disc = [l for t, l in victim.lines if t0 <= t and "wifi disconnected" in l]
    lost = [l for t, l in victim.lines if t0 <= t and "wch=-1" in l]
    recov = [l for t, l in victim.lines if t0 <= t and "wch=13" in l]
    scanlines = [(t, l) for t, l in scan.lines if t0 <= t and "bcn/s=" in l]

    print("=== result ===", flush=True)
    print(f"victim wch=-1 lines={len(lost)} wch=13 lines={len(recov)} disconnects={len(disc)}", flush=True)
    print(f"victim first lost: {lost[:3]}", flush=True)
    print(f"victim last lines: {[l for _, l in victim.lines[-4:]]}", flush=True)
    for t, l in scanlines:
        if t < t0 + JAM_S + 3:
            print(f"  scan {t - t0:5.1f}s {l}", flush=True)
    print(f"scan last lines: {[l for _, l in scan.lines[-3:]]}", flush=True)
    for j in jammers:
        print(f"{j.name} last: {[l for _, l in j.lines[-4:]]}", flush=True)
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    rc = main()
    sys.stdout.flush()
    import os
    os._exit(rc)
