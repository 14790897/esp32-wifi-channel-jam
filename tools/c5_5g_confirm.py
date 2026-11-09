import re
import subprocess
import time

import serial

PORT = "COM12"


def adb(args):
    try:
        return subprocess.run(["adb", "shell"] + args, capture_output=True, text=True,
                              timeout=30).stdout
    except Exception as e:
        return f"adb error: {e}"


def status():
    out = adb(["cmd", "wifi", "status"])
    conn = re.search(r'Wifi is (connected to "[^"]+"|not connected|disabled[^\n]*)', out)
    freq = re.search(r"Frequency: (\d+)MHz", out)
    sup = re.search(r"Supplicant state: (\w+)", out)
    rssi = re.search(r"RSSI: (-?\d+)", out)
    return (f"{conn.group(1) if conn else '?'}|"
            f"{freq.group(1) if freq else '-'}|"
            f"{sup.group(1) if sup else '-'}|"
            f"{rssi.group(1) if rssi else '-'}")


def wifi_toggle():
    adb(["svc", "wifi", "disable"])
    time.sleep(3)
    adb(["svc", "wifi", "enable"])


def main():
    ser = serial.Serial(PORT, 115200, timeout=0.2)
    time.sleep(0.5)
    print("restore wifi:", status(), flush=True)
    wifi_toggle()
    ok = False
    for i in range(25):
        s = status()
        if i % 3 == 0:
            print(f"wait {i}s:", s, flush=True)
        if "|5180|" in s:
            ok = True
            print("phone back on 5GHz:", s, flush=True)
            break
        time.sleep(1)
    if not ok:
        print("phone did not return to 5GHz; last:", status(), flush=True)
        return
    time.sleep(3)
    print("baseline:", status(), flush=True)
    ser.write(b"start 36 10 4\r\n")
    t0 = time.time()
    timeline = []
    while time.time() - t0 < 12:
        line = ser.readline()
        if line:
            print(f"[c5 {time.time() - t0:5.1f}] {line.decode('utf-8', 'replace').strip()}", flush=True)
        s = status()
        timeline.append((round(time.time() - t0, 1), s))
        time.sleep(0.5)
    print("timeline:", flush=True)
    for t, s in timeline:
        print(f"  {t:5.1f}s {s}", flush=True)
    time.sleep(4)
    print("after:", status(), flush=True)
    log = adb(["logcat", "-b", "all", "-d", "-t", "1200"])
    hits = [l for l in log.splitlines()
            if re.search(r"CTRL-EVENT-(DIS)?CONNECTED|NETWORK_SELECTION|disconnected|frequency", l, re.I)]
    print("LOGCAT:", flush=True)
    for l in hits[-20:]:
        print(l, flush=True)
    # leave the phone on 5GHz when possible
    if "|5180|" not in status():
        print("restoring 5GHz...", flush=True)
        wifi_toggle()
        for i in range(20):
            s = status()
            if "|5180|" in s:
                print("restored:", s, flush=True)
                break
            time.sleep(1)
    ser.close()


if __name__ == "__main__":
    main()
