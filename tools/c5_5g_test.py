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


def phone_status():
    out = adb(["cmd", "wifi", "status"])
    conn = re.search(r'Wifi is (connected to "[^"]+"|not connected|disabled[^\n]*)', out)
    freq = re.search(r"Frequency: (\d+)MHz", out)
    sup = re.search(r"Supplicant state: (\w+)", out)
    rssi = re.search(r"RSSI: (-?\d+)", out)
    return (f"{conn.group(1) if conn else '?'} "
            f"freq={freq.group(1) if freq else '-'}MHz "
            f"state={sup.group(1) if sup else '-'} "
            f"rssi={rssi.group(1) if rssi else '-'}")


def main():
    ser = serial.Serial(PORT, 115200, timeout=0.2)
    time.sleep(0.5)
    print("BASELINE", phone_status(), flush=True)
    ser.write(b"start 36 10\r\n")
    t0 = time.time()
    next_poll = 0.0
    while time.time() - t0 < 13:
        line = ser.readline()
        if line:
            print(f"[c5 {time.time() - t0:5.1f}] {line.decode('utf-8', 'replace').strip()}", flush=True)
        if time.time() - t0 >= next_poll:
            print(f"[phone {time.time() - t0:5.1f}] {phone_status()}", flush=True)
            next_poll += 2
        time.sleep(0.05)
    time.sleep(2)
    print("AFTER-WAIT", phone_status(), flush=True)
    time.sleep(8)
    print("RECOVERED", phone_status(), flush=True)
    log = adb(["logcat", "-d", "-t", "600"])
    hits = [l for l in log.splitlines()
            if re.search(r"wpa_supplicant|CTRL-EVENT|DISCONNECT|reason=", l, re.I)]
    print("LOGCAT (last 15):", flush=True)
    for l in hits[-15:]:
        print(l, flush=True)
    ser.close()


if __name__ == "__main__":
    main()
