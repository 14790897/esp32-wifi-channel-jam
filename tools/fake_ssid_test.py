import re
import serial
import threading
import time
import sys

VICTIM = "COM6"
SCAN = "COM19"
SPAM = "COM5"
JAM = "COM4"
BASE_S = 6
JAM_S = 10
TAIL_S = 8


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


def vals(lines, key):
    out = []
    for l in lines:
        m = re.search(key + r"=(\d+)", l)
        if m:
            out.append(int(m.group(1)))
    return out


def mean(v):
    return round(sum(v) / len(v), 1) if v else None


def main():
    scan = Reader("scan", SCAN)
    victim = Reader("victim", VICTIM)
    spam = Reader("spam", SPAM)
    time.sleep(1)
    print("baseline: fake SSIDs on, PHY jam off...", flush=True)
    time.sleep(BASE_S)
    base_lines = [l for t, l in scan.lines]
    print(f"baseline uniq={vals(base_lines, 'uniq')} jam13={vals(base_lines, 'jam13')}", flush=True)

    jam = Reader("jam", JAM)
    time.sleep(0.3)
    jam.send(f"start 13 {JAM_S}")
    t0 = time.time()
    print("=== PHY JAM START ===", flush=True)
    time.sleep(JAM_S + TAIL_S)

    during = [l for t, l in scan.lines if t0 <= t < t0 + JAM_S]
    after = [l for t, l in scan.lines if t >= t0 + JAM_S]
    lost = [l for t, l in victim.lines if t0 <= t and "wch=-1" in l]
    disc = [l for t, l in victim.lines if t0 <= t and "wifi disconnected" in l]
    recov = [l for t, l in victim.lines if t0 <= t and "wch=13" in l]

    print("=== result ===", flush=True)
    print(f"scan DURING jam: uniq mean={mean(vals(during, 'uniq'))} max={max(vals(during, 'uniq') or [0])} "
          f"jam13 mean={mean(vals(during, 'jam13'))}", flush=True)
    print(f"scan AFTER jam:  uniq mean={mean(vals(after, 'uniq'))} max={max(vals(after, 'uniq') or [0])} "
          f"jam13 mean={mean(vals(after, 'jam13'))}", flush=True)
    print(f"victim lost={len(lost)} disconnects={len(disc)} recovered_wch13={len(recov)}", flush=True)
    for l in disc[:5]:
        print(f"  {l}", flush=True)
    print("--- scanner timeline ---", flush=True)
    for t, l in [(t, l) for t, l in scan.lines if t0 - 2 <= t <= t0 + JAM_S + 3]:
        if "bcn/s=" in l:
            print(f"  {t - t0:6.1f}s {l}", flush=True)
    print(f"spam last: {[l for _, l in spam.lines[-4:]]}", flush=True)
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    rc = main()
    sys.stdout.flush()
    import os
    os._exit(rc)
