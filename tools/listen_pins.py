import serial
import time
import sys

def main():
    try:
        s = serial.Serial('COM15', 115200, timeout=1)
        time.sleep(1.0)
        s.reset_input_buffer()
        print("[+] Serial connected. Monitoring pins for 25 seconds.")
        print("[+] PLEASE PRESS AND HOLD EACH BUTTON ONE BY ONE (Left, Middle, Right)...")
        sys.stdout.flush()

        t0 = time.time()
        last_probe = time.time()
        while time.time() - t0 < 25:
            # Send 'p' every 2 seconds to probe exact digitalRead levels
            if time.time() - last_probe > 2.5:
                s.write(b'p\n')
                s.flush()
                last_probe = time.time()

            line = s.readline()
            if line:
                text = line.decode('utf-8', errors='replace').rstrip()
                if text:
                    now_str = time.strftime("%H:%M:%S")
                    print(f"[{now_str}] {text}", flush=True)

        s.close()
    except Exception as e:
        print("Serial error:", e)

if __name__ == "__main__":
    main()

