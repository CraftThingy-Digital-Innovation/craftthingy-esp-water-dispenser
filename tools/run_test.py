import serial
import time
import sys

def main():
    try:
        s = serial.Serial()
        s.port = 'COM15'
        s.baudrate = 115200
        s.timeout = 1
        s.dtr = False
        s.rts = False
        s.open()
        
        print("[+] Serial port COM15 opened. Waiting for ESP32 boot...")
        
        # Read lines until ready prompt appears or 3 seconds elapse
        start_wait = time.time()
        while time.time() - start_wait < 3.5:
            line = s.readline()
            if line:
                text = line.decode('utf-8', errors='replace').rstrip()
                if text:
                    print(f"[{time.strftime('%H:%M:%S')}] {text}")
                    if "Ready" in text:
                        break
        
        time.sleep(0.5)
        s.reset_input_buffer()

        print("\n[+] >>> Triggering 40s Motor Dispense Test (Sending '1')... <<<")
        s.write(b'1\n')
        s.flush()

        start_time = time.time()
        # Monitor for up to 45 seconds to observe full 40s run and auto-cutoff
        while time.time() - start_time < 45:
            line = s.readline()
            if line:
                text = line.decode('utf-8', errors='replace').rstrip()
                if text:
                    timestamp = time.strftime("%H:%M:%S")
                    print(f"[{timestamp}] {text}")
                    sys.stdout.flush()
                    if "Motor Gate OFF" in text:
                        print("\n[+] 40s dispense test completed successfully!")
                        break
            time.sleep(0.05)

        s.close()
    except Exception as e:
        print(f"[-] Serial error: {e}")

if __name__ == "__main__":
    main()
