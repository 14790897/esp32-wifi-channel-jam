import re
import serial
import threading
import time
import sys

VICTIM = "COM6"
SCAN = "COM19"
JAMMERS = [("J4", "COM4"), ("J5", "COM5")]
BASE_S = 5
MODE_S = 20


class Reader:
    def __init__(self, name, port, baud=115200):
        self.name = name
        self.port = port
        self.ser = serial.Serial(port, baud, timeout=0.2)
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
        self.ser.write((cmd + "\r\n").encode())

    def close(self):
        self.stop.set()
        try:
            self.ser.close()
        except Exception:
            pass


def jam13_values(lines):
    out = []
    for l in lines:
        m = re.search(r"jam13=(\d+)", l)
        if m:
            out.append(int(m.group(1)))
    return out


def main():
    scan = Reader("scan", SCAN)
    victim = Reader("victim", VICTIM)
    time.sleep(1)
    print("baseline beacons...", flush=True)
    time.sleep(BASE_S)
    base = [(t, l) for t, l in scan.lines if "bcn/s=" in l]
    print(f"baseline jam13/s={jam13_values([l for _, l in base])}", flush=True)

    jammers = [Reader(n, p) for n, p in JAMMERS]
    time.sleep(0.3)

    for mode in ("bcn", "data"):
        for j in jammers:
            j.send(f"mode {mode}")
        time.sleep(0.4)
        for j in jammers:
            j.send("start")
        t0 = time.time()
        print(f"=== JAM {mode} START ===", flush=True)
        time.sleep(MODE_S)
        t1 = time.time()
        for j in jammers:
            j.send("stop")
        print(f"=== JAM {mode} STOP ===", flush=True)
        time.sleep(2)

        scanlines = [(t, l) for t, l in scan.lines if t0 <= t <= t1 and "bcn/s=" in l]
        vals = jam13_values([l for _, l in scanlines])
        disc = [l for t, l in victim.lines if t0 <= t <= t1 and "wifi disconnected" in l]
        print(f"RESULT {mode}: jam13/s mean={sum(vals)/len(vals) if vals else None} "
              f"min={min(vals) if vals else None} max={max(vals) if vals else None} "
              f"series={vals}", flush=True)
        print(f"RESULT {mode}: victim disconnects={len(disc)}", flush=True)

    scan.close()
    victim.close()
    for j in jammers:
        j.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
