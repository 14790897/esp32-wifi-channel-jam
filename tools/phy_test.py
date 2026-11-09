import serial
import threading
import time
import sys

VICTIM = "COM6"
SCAN = "COM19"
JAMMERS = [("J1", "COM4"), ("J2", "COM5")]
BASE_S = 5
JAM_S = 15
TAIL_S = 8


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


def main():
    scan = Reader("scan", SCAN)
    victim = Reader("victim", VICTIM)
    time.sleep(1)
    print("baseline...", flush=True)
    time.sleep(BASE_S)
    base = [l for t, l in scan.lines if "bcn/s=" in l]
    print(f"baseline scan: {base[-3:]}", flush=True)

    jammers = [Reader(n, p) for n, p in JAMMERS]
    time.sleep(0.3)
    for j in jammers:
        j.send("status")
    time.sleep(0.5)
    for j in jammers:
        j.send("start")
    t0 = time.time()
    print("=== PHY JAM START ===", flush=True)
    time.sleep(JAM_S)
    t1 = time.time()
    for j in jammers:
        j.send("stop")
    print("=== PHY JAM STOP ===", flush=True)
    time.sleep(TAIL_S)

    disc = [(t, l) for t, l in victim.lines if t0 <= t <= t1 and "wifi disconnected" in l]
    disc_after = [(t, l) for t, l in victim.lines if t > t1 and "wifi disconnected" in l]
    gotip = [(t, l) for t, l in victim.lines if t0 <= t <= t1 + TAIL_S and "got ip" in l]
    scanlines = [(t, l) for t, l in scan.lines if t0 <= t <= t1 + TAIL_S and "bcn/s=" in l]

    print("=== result ===", flush=True)
    print(f"victim disconnects during jam={len(disc)} after={len(disc_after)} got_ip={len(gotip)}", flush=True)
    for t, l in (disc + disc_after)[:20]:
        print(f"  {t - t0:6.1f}s {l}", flush=True)
    for t, l in scanlines:
        print(f"  scan {t - t0:6.1f}s {l}", flush=True)

    scan.close()
    victim.close()
    for j in jammers:
        j.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
