#!/usr/bin/env python3
"""
CraftThingy Digital Innovation - Water Dispenser Controller CLI & Telemetry Tool
Dedicated tool for controlling the smart water dispenser, checking pour status,
initiating 40s/80s/continuous dispenses, and triggering emergency stop.
"""

import argparse
import hashlib
import hmac
import json
import secrets
import sys
import time
import urllib.request
import urllib.error

DEFAULT_SECRET = "a1b2c3d4e5f60718293a4b5c6d7e8f90112233445566778899aabbccddeeff00"

def send_request(host: str, method: str, path: str, key: str, sequence: int, body_data: dict = None) -> dict:
    url = f"http://{host}{path}"
    body_str = json.dumps(body_data) if body_data else ""
    timestamp = str(int(time.time()))
    nonce = secrets.token_hex(8)

    # Compute HMAC-SHA256 signature
    payload = f"{method}\n{path}\n{timestamp}\n{nonce}\n{body_str}".encode("utf-8")
    sig = hmac.new(key.encode("utf-8"), payload, hashlib.sha256).hexdigest()

    headers = {
        "Content-Type": "application/json",
        "X-Signature": sig,
        "X-Timestamp": timestamp,
        "X-Nonce": nonce,
        "X-Sequence": str(sequence),
        "X-Master-Key": key
    }

    req = urllib.request.Request(
        url,
        data=body_str.encode("utf-8") if body_str else None,
        headers=headers,
        method=method
    )

    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            resp_body = resp.read().decode("utf-8")
            try:
                return json.loads(resp_body)
            except json.JSONDecodeError:
                return {"raw": resp_body}
    except urllib.error.HTTPError as e:
        err_msg = e.read().decode("utf-8")
        print(f"[-] HTTP Error {e.code}: {err_msg}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"[-] Connection Error: {e.reason}", file=sys.stderr)
        sys.exit(1)

def print_telemetry(data: dict):
    dev = data.get("data", data)
    if isinstance(dev, dict) and "device" in dev:
        dev = dev["device"]

    running = dev.get("pumpRunning", False)
    mode = dev.get("currentMode", "IDLE")
    elapsed_ms = dev.get("elapsedMs", 0)
    target_ms = dev.get("targetDurationMs", 0)
    duty = dev.get("dutyCycle", 0)
    safe_ota = dev.get("safeForOTA", True)

    elapsed_s = elapsed_ms // 1000
    target_s = target_ms // 1000
    rem_s = max(0, target_s - elapsed_s) if target_ms > 0 else 0

    print("=" * 60)
    print(" 💧 CraftThingy Water Dispenser Telemetry")
    print("=" * 60)

    if running:
        duty_pct = int((duty / 255.0) * 100)
        state_str = f"\033[92m[DISPENSING - {mode}]\033[0m (PWM: {duty_pct}%)"
    else:
        state_str = "\033[90m[STANDBY / READY]\033[0m"

    print(f" Pump Status    : {state_str}")
    print(f" Active Mode    : {mode}")
    if running:
        if target_ms > 0:
            pct = min(100, int((elapsed_ms / target_ms) * 100))
            bar_fill = "#" * (pct // 5)
            bar_empty = "-" * (20 - (pct // 5))
            print(f" Progress       : [{bar_fill}{bar_empty}] {elapsed_s}s / {target_s}s ({rem_s}s remaining)")
        else:
            print(f" Progress       : Continuous Pour ({elapsed_s}s elapsed, 15m safety max)")
    print(f" OTA Safety     : {'\033[92mSAFE (Pump Idle)\033[0m' if safe_ota else '\033[91mLOCKED (Active Dispense)\033[0m'}")
    print("=" * 60)

def main():
    parser = argparse.ArgumentParser(description="CraftThingy Water Dispenser Controller CLI")
    parser.add_argument("--host", default="192.168.4.1", help="Device IP address (default: 192.168.4.1)")
    parser.add_argument("--key", default=DEFAULT_SECRET, help="Master Secret Key / Bearer Token")
    parser.add_argument("--seq", type=int, default=1, help="Rolling sequence counter (default: 1)")

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("status", help="Get live water dispenser telemetry")
    subparsers.add_parser("dispense-40s", help="▶️ Dispense water for 40 seconds")
    subparsers.add_parser("dispense-80s", help="▶️ Dispense water for 80 seconds")
    subparsers.add_parser("dispense-continuous", help="▶️ Dispense continuously (15m safety limit)")
    subparsers.add_parser("stop", help="🚨 Stop water dispensing immediately")

    args = parser.parse_args()

    if args.command == "status":
        res = send_request(args.host, "GET", "/api/v1/device/status", args.key, args.seq)
        print_telemetry(res)

    elif args.command == "dispense-40s":
        res = send_request(args.host, "POST", "/api/v1/device/command", args.key, args.seq, {"command": "dispense_40s"})
        print("[+] 40s Dispense Started:")
        print(json.dumps(res, indent=2))

    elif args.command == "dispense-80s":
        res = send_request(args.host, "POST", "/api/v1/device/command", args.key, args.seq, {"command": "dispense_80s"})
        print("[+] 80s Dispense Started:")
        print(json.dumps(res, indent=2))

    elif args.command == "dispense-continuous":
        res = send_request(args.host, "POST", "/api/v1/device/command", args.key, args.seq, {"command": "dispense_continuous"})
        print("[+] Continuous Dispense Started:")
        print(json.dumps(res, indent=2))

    elif args.command == "stop":
        res = send_request(args.host, "POST", "/api/v1/device/command", args.key, args.seq, {"command": "stop"})
        print("[!] 🚨 Pump Stopped:")
        print(json.dumps(res, indent=2))

if __name__ == "__main__":
    main()
