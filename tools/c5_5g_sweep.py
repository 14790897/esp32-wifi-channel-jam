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


def main():
    ser = serial.Serial(PORT, 115200, timeout=0.2)
    time.sleep(0.5)
    modes = [(1, "6M"), (2, "MCS0"), (3, "11AX"), (4, "CW")]
    for m, name in modes:
        print(f"\n===== mode {name} =====", flush=True)
        print("before:", status(), flush=True)
        ser.write(f"start 36 8 {m}\r\n".encode())
        t0 = time.time()
        states = []
        while time.time() - t0 < 9.5:
            line = ser.readline()
            if line:
                print(f"[c5 {time.time() - t0:4.1f}] {line.decode('utf-8', 'replace').strip()}", flush=True)
            s = status()
            states.append((round(time.time() - t0, 1), s))
            time.sleep(1.0)
        print("during:", states, flush=True)
        time.sleep(5)
        print("after:", status(), flush=True)
        log = adb(["logcat", "-d", "-t", "400"])
        hits = [l for l in log.splitlines()
                if re.search(r"CTRL-EVENT-DISCONNECTED|CTRL-EVENT-CONNECTED|reason=", l, re.I)]
        for l in hits[-6:]:
            print("LOG", l, flush=True)
    ser.close()


if __name__ == "__main__":
    main()
