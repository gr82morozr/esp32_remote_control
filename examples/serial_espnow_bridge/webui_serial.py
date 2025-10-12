#!/usr/bin/env python3
"""
Self-balancing robot web UI for serial-to-ESPNOW bridge.

Launches a small HTTP server that exposes a browser UI for tuning PID gains and
other control settings, then sends the resulting packet as CSV to the ESP32
bridge over serial.
"""

import argparse
import json
import math
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Optional

import serial
import serial.tools.list_ports

HTML_PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Self Balancing Robot Tuner</title>
<style>
body { font-family: Arial, sans-serif; margin: 24px; background: #f0f2f5; color: #111; }
main { max-width: 720px; margin: 0 auto; background: #fff; padding: 24px; border-radius: 8px; box-shadow: 0 4px 16px rgba(0,0,0,0.1); }
fieldset { margin-bottom: 16px; border: 1px solid #d0d7de; border-radius: 6px; padding: 12px 16px; }
legend { font-weight: 600; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; }
label { font-size: 0.9rem; margin-bottom: 4px; display: block; }
input, button { font-size: 0.95rem; padding: 6px 8px; border: 1px solid #d0d7de; border-radius: 4px; width: 100%; box-sizing: border-box; }
button { background: #2563eb; color: #fff; border: none; cursor: pointer; margin-top: 12px; }
button:hover { background: #1d4ed8; }
.mode-switch { display: inline-flex; border: 1px solid #d0d7de; border-radius: 6px; overflow: hidden; background: #f8fafc; }
.mode-switch input { position: absolute; opacity: 0; pointer-events: none; width: 0; height: 0; margin: 0; }
.mode-option { flex: 1 1 120px; margin: 0; padding: 8px 12px; font-weight: 600; text-align: center; cursor: pointer; color: #111; background: transparent; transition: background 0.15s ease, color 0.15s ease; user-select: none; }
.mode-option:not(:first-of-type) { border-left: 1px solid #d0d7de; }
.mode-switch input:checked + .mode-option { background: #2563eb; color: #fff; }
.mode-switch input:focus-visible + .mode-option { outline: 2px solid #94a3b8; outline-offset: -2px; }
.mode-option:hover { background: #e2e8f0; }
.send-controls { display: flex; flex-wrap: wrap; gap: 12px; margin-top: 8px; align-items: center; }
.send-controls button { flex: 1 1 180px; }
button:disabled { background: #e2e8f0; color: #64748b; cursor: not-allowed; }
button:disabled:hover { background: #e2e8f0; }
.hint { font-size: 0.85rem; color: #475569; margin: 8px 0 0; }
.toggle { display: flex; align-items: center; gap: 8px; }
#toast { margin-top: 12px; font-weight: 600; }
.status { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; margin-top: 16px; }
.status div { background: #f8fafc; border: 1px solid #d0d7de; border-radius: 6px; padding: 10px; min-height: 70px; }
pre { background: #0f172a; color: #e2e8f0; padding: 12px; border-radius: 6px; max-height: 220px; overflow: auto; font-size: 0.85rem; }
</style>
</head>
<body>
<main>
  <h1>Self Balancing Robot Tuner</h1>
  <p>Send PID parameters and balancing targets to the ESP32 serial bridge.</p>
  <form id="tuning-form">
    <fieldset>
      <legend>PID Gains</legend>
      <div class="grid">
        <label>Kp<input type="number" name="kp" step="0.01" value="35.0" required></label>
        <label>Ki<input type="number" name="ki" step="0.001" value="0.0" required></label>
        <label>Kd<input type="number" name="kd" step="0.01" value="0.85" required></label>
      </div>
    </fieldset>
    <fieldset>
      <legend>Balancing Targets</legend>
      <div class="grid">
        <label>Target Angle (deg)<input type="number" name="target_angle" step="0.1" value="0.0" required></label>
        <label>Max Output (PWM scale)<input type="number" name="max_output" step="0.01" value="1.0" required></label>
      </div>
    </fieldset>
    <fieldset>
      <legend>Packet Metadata</legend>
      <div class="grid">
        <label>Message Type (id1)<input type="number" name="message_type" min="0" max="255" value="177" required></label>
        <label>Robot ID (id2)<input type="number" name="robot_id" min="0" max="255" value="1" required></label>
        <label>Loop Time (ms, id3)<input type="number" name="loop_time_ms" min="0" max="255" value="10" required></label>
        <label>Profile Slot (id4)<input type="number" name="profile_slot" min="0" max="255" value="0" required></label>
      </div>
    </fieldset>
    <fieldset>
      <legend>Runtime Flags</legend>
      <div class="grid">
        <label class="toggle"><input type="checkbox" name="enable_motors" checked>Enable motors (bit 0)</label>
        <label class="toggle"><input type="checkbox" name="persist">Persist to flash (bit 1)</label>
        <label class="toggle"><input type="checkbox" name="trigger_calibration">Run IMU calibration (bit 2)</label>
      </div>
    </fieldset>
    <fieldset>
      <legend>Send Control</legend>
      <div class="send-controls">
        <div class="mode-switch" role="radiogroup" aria-label="Send mode">
          <input type="radio" id="mode-manual" name="send_mode" value="manual" checked>
          <label for="mode-manual" class="mode-option">Manual</label>
          <input type="radio" id="mode-auto" name="send_mode" value="auto">
          <label for="mode-auto" class="mode-option">Auto</label>
        </div>
        <button type="submit" id="send-button">Send Now</button>
      </div>
      <p class="hint" id="mode-hint">Manual mode: click "Send Now" to transmit.</p>
    </fieldset>
    <div id="toast"></div>
  </form>
  <div class="status">
    <div><strong>Serial Port</strong><div id="status-port">--</div></div>
    <div><strong>Last Packet</strong><div id="status-packet">No packets sent yet.</div></div>
    <div><strong>Errors</strong><div id="status-error">None.</div></div>
  </div>
  <h2>Bridge Output</h2>
  <pre id="serial-log">Waiting for data...</pre>
  <p>Packet layout: id1,id2,id3,id4,value1,value2,value3,value4,value5,flags.</p>
</main>
<script>
const form = document.getElementById('tuning-form');
const toast = document.getElementById('toast');
const statusPort = document.getElementById('status-port');
const statusPacket = document.getElementById('status-packet');
const statusError = document.getElementById('status-error');
const serialLog = document.getElementById('serial-log');
const sendButton = document.getElementById('send-button');
const modeHint = document.getElementById('mode-hint');
const modeInputs = Array.from(document.querySelectorAll('input[name="send_mode"]'));

let currentMode = 'manual';
let autoSendTimer = null;
const AUTO_SEND_DELAY_MS = 100;

function showToast(message, isError) {
  toast.textContent = message;
  toast.style.color = isError ? '#b91c1c' : '#166534';
}

function collectPayload() {
  const formData = new FormData(form);
  return {
    kp: parseFloat(formData.get('kp')),
    ki: parseFloat(formData.get('ki')),
    kd: parseFloat(formData.get('kd')),
    target_angle: parseFloat(formData.get('target_angle')),
    max_output: parseFloat(formData.get('max_output')),
    message_type: parseInt(formData.get('message_type'), 10),
    robot_id: parseInt(formData.get('robot_id'), 10),
    loop_time_ms: parseInt(formData.get('loop_time_ms'), 10),
    profile_slot: parseInt(formData.get('profile_slot'), 10),
    enable_motors: formData.get('enable_motors') === 'on',
    persist: formData.get('persist') === 'on',
    trigger_calibration: formData.get('trigger_calibration') === 'on'
  };
}

async function sendPayload(payload, source = 'manual') {
  try {
    const response = await fetch('/api/update', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const result = await response.json();
    if (response.ok) {
      const label = source === 'auto' ? 'Auto-sent' : 'Packet sent';
      showToast(label + ' at ' + new Date(result.sent_at).toLocaleTimeString(), false);
    } else {
      showToast(result.error || 'Packet rejected', true);
    }
  } catch (error) {
    showToast('Network error: ' + error.message, true);
  }
}

function isAutoMode() {
  return currentMode === 'auto';
}

function scheduleAutoSend({ immediate = false } = {}) {
  if (!isAutoMode()) {
    return;
  }
  if (autoSendTimer) {
    clearTimeout(autoSendTimer);
    autoSendTimer = null;
  }
  const executeSend = () => {
    autoSendTimer = null;
    sendPayload(collectPayload(), 'auto');
  };
  if (immediate || AUTO_SEND_DELAY_MS <= 0) {
    executeSend();
  } else {
    autoSendTimer = setTimeout(executeSend, AUTO_SEND_DELAY_MS);
  }
}

function updateModeUI() {
  modeInputs.forEach((input) => {
    const isActive = input.value === currentMode;
    input.checked = isActive;
    input.setAttribute('aria-checked', String(isActive));
  });
  const autoActive = isAutoMode();
  sendButton.disabled = autoActive;
  sendButton.setAttribute('aria-disabled', String(autoActive));
  modeHint.textContent = autoActive
    ? 'Auto mode: changes send within ~100ms.'
    : 'Manual mode: click "Send Now" to transmit.';
}

function setMode(nextMode, { silent = false, force = false } = {}) {
  if (!force && nextMode === currentMode) {
    return;
  }
  currentMode = nextMode;
  if (autoSendTimer) {
    clearTimeout(autoSendTimer);
    autoSendTimer = null;
  }
  updateModeUI();
  if (!silent) {
    showToast(currentMode === 'auto' ? 'Auto send enabled' : 'Manual send mode', false);
  }
}

modeInputs.forEach((input) => {
  input.addEventListener('change', () => {
    if (input.checked) {
      setMode(input.value);
    }
  });
});

form.addEventListener('submit', (event) => {
  event.preventDefault();
  if (autoSendTimer) {
    clearTimeout(autoSendTimer);
    autoSendTimer = null;
  }
  sendPayload(collectPayload(), 'manual');
});

const inputs = form.querySelectorAll('input[name]:not([name="send_mode"])');
inputs.forEach((input) => {
  const eventName = input.type === 'checkbox' ? 'change' : 'input';
  input.addEventListener(eventName, () => {
    if (isAutoMode()) {
      scheduleAutoSend({ immediate: input.type === 'checkbox' });
    }
  });
});

setMode('manual', { silent: true, force: true });

async function refreshStatus() {
  try {
    const response = await fetch('/api/status');
    if (!response.ok) {
      throw new Error('HTTP ' + response.status);
    }
    const data = await response.json();
    statusPort.textContent = data.port || '--';
    statusError.textContent = data.last_error || 'None.';
    if (data.last_packet) {
      statusPacket.textContent = JSON.stringify(data.last_packet);
    } else {
      statusPacket.textContent = 'No packets sent yet.';
    }
    if (data.serial_log && data.serial_log.length > 0) {
      serialLog.textContent = data.serial_log.map((entry) => '[' + entry.timestamp + '] ' + entry.line).join('\n');
    }
  } catch (error) {
    statusError.textContent = 'Status fetch failed: ' + error.message;
  }
}

refreshStatus();
setInterval(refreshStatus, 1500);


</script>
</body>
</html>
"""


def format_float(value: float) -> str:
    text = f"{value:.5f}".rstrip("0").rstrip(".")
    if text == "-0":
        return "0"
    if text == "":
        return "0"
    return text


class AppState:
    def __init__(self, serial_conn: serial.Serial):
        self.serial = serial_conn
        self.lock = threading.Lock()
        self.serial_log = deque(maxlen=200)
        self.last_packet: Optional[Dict[str, Any]] = None
        self.last_error: Optional[str] = None
        self.shutdown = threading.Event()

    def append_log(self, line: str) -> None:
        entry = {"timestamp": datetime.now().strftime("%H:%M:%S"), "line": line}
        with self.lock:
            self.serial_log.append(entry)

    def set_error(self, message: Optional[str]) -> None:
        with self.lock:
            self.last_error = message


def serial_reader(state: AppState) -> None:
    while not state.shutdown.is_set():
        try:
            line = state.serial.readline().decode("utf-8", errors="replace").strip()
            if line:
                state.append_log(line)
        except serial.SerialException as exc:
            state.set_error(f"Serial error: {exc}")
            state.shutdown.set()
            break
        except Exception as exc:  # pragma: no cover - defensive
            state.set_error(f"Reader error: {exc}")
            time.sleep(0.5)


def require_float(data: Dict[str, Any], key: str) -> float:
    if key not in data:
        raise ValueError(f"Missing field '{key}'")
    value = data[key]
    try:
        value = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"Field '{key}' must be a number") from exc
    if not math.isfinite(value):
        raise ValueError(f"Field '{key}' must be finite")
    return value


def require_byte(data: Dict[str, Any], key: str) -> int:
    if key not in data:
        raise ValueError(f"Missing field '{key}'")
    value = data[key]
    try:
        ivalue = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"Field '{key}' must be an integer") from exc
    if not 0 <= ivalue <= 255:
        raise ValueError(f"Field '{key}' must be between 0 and 255")
    return ivalue


class RobotRequestHandler(BaseHTTPRequestHandler):
    server_version = "RobotTuner/1.0"

    def do_GET(self) -> None:  # type: ignore[override]
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            self._send_html(HTML_PAGE.encode("utf-8"))
        elif path == "/api/status":
            self._handle_status()
        else:
            self.send_error(404, "Not Found")

    def do_POST(self) -> None:  # type: ignore[override]
        path = self.path.split("?", 1)[0]
        if path == "/api/update":
            self._handle_update()
        else:
            self.send_error(404, "Not Found")

    def _handle_status(self) -> None:
        state: AppState = self.server.state  # type: ignore[attr-defined]
        with state.lock:
            payload = {
                "port": state.serial.port if state.serial else None,
                "baud": state.serial.baudrate if state.serial else None,
                "last_packet": state.last_packet,
                "last_error": state.last_error,
                "serial_log": list(state.serial_log),
            }
        self._send_json(payload)

    def _handle_update(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            self._send_json({"error": "Empty request body"}, status=400)
            return
        raw = self.rfile.read(length)
        try:
            data = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            self._send_json({"error": "Invalid JSON body"}, status=400)
            return

        try:
            packet = {
                "id1": require_byte(data, "message_type"),
                "id2": require_byte(data, "robot_id"),
                "id3": require_byte(data, "loop_time_ms"),
                "id4": require_byte(data, "profile_slot"),
                "value1": require_float(data, "kp"),
                "value2": require_float(data, "ki"),
                "value3": require_float(data, "kd"),
                "value4": require_float(data, "target_angle"),
                "value5": require_float(data, "max_output"),
            }
            flags = 0
            if data.get("enable_motors"):
                flags |= 0x01
            if data.get("persist"):
                flags |= 0x02
            if data.get("trigger_calibration"):
                flags |= 0x04
            packet["flags"] = flags
        except ValueError as exc:
            self._send_json({"error": str(exc)}, status=400)
            return

        state: AppState = self.server.state  # type: ignore[attr-defined]
        if not state.serial or not state.serial.is_open:
            self._send_json({"error": "Serial port is not open"}, status=503)
            return

        payload_csv = ",".join([
            str(packet["id1"]),
            str(packet["id2"]),
            str(packet["id3"]),
            str(packet["id4"]),
            format_float(packet["value1"]),
            format_float(packet["value2"]),
            format_float(packet["value3"]),
            format_float(packet["value4"]),
            format_float(packet["value5"]),
            str(packet["flags"]),
        ])

        timestamp = datetime.now(timezone.utc).isoformat()

        try:
            with state.lock:
                state.serial.write((payload_csv + "\n").encode("ascii"))
                state.serial.flush()
                state.last_packet = {**packet, "sent_at": timestamp, "raw": payload_csv}
                state.last_error = None
        except serial.SerialException as exc:
            state.set_error(f"Serial write failed: {exc}")
            self._send_json({"error": "Serial write failed"}, status=503)
            return

        self._send_json({"sent_at": timestamp, "raw": payload_csv})

    def _send_html(self, body: bytes, status: int = 200) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, data: Dict[str, Any], status: int = 200) -> None:
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: Any) -> None:  # type: ignore[override]
        return


class RobotHTTPServer(ThreadingHTTPServer):
    def __init__(self, server_address, handler_class, state: AppState):
        super().__init__(server_address, handler_class)
        self.state = state


def select_serial_port(preselected: Optional[str]) -> str:
    if preselected:
        return preselected
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        raise SystemExit("No serial ports detected.")
    print("Available serial ports:")
    for idx, port in enumerate(ports, start=1):
        print(f"  {idx}. {port.device} - {port.description}")
    while True:
        choice = input("Select port by number or enter name: ").strip()
        if not choice:
            continue
        if choice.isdigit():
            idx = int(choice)
            if 1 <= idx <= len(ports):
                return ports[idx - 1].device
            print("Invalid selection.")
        else:
            return choice


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Self balancing robot web UI to send PID packets over serial.")
    parser.add_argument("--serial-port", dest="port", help="Serial port name, e.g. COM5 or /dev/ttyUSB0.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200).")
    parser.add_argument("--host", default="127.0.0.1", help="HTTP host to bind (default: 127.0.0.1).")
    parser.add_argument("--http-port", type=int, default=8000, help="HTTP port to bind (default: 8000).")
    parser.add_argument("--no-browser", action="store_true", help="Do not open the default web browser automatically.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    port = select_serial_port(args.port)
    try:
        serial_conn = serial.Serial(port, args.baud, timeout=0.1)
    except serial.SerialException as exc:
        raise SystemExit(f"Failed to open serial port {port}: {exc}")

    print(f"Connected to {serial_conn.port} at {serial_conn.baudrate} baud.")

    state = AppState(serial_conn)
    reader_thread = threading.Thread(target=serial_reader, args=(state,), daemon=True)
    reader_thread.start()

    server = RobotHTTPServer((args.host, args.http_port), RobotRequestHandler, state)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    url = f"http://{args.host}:{args.http_port}/"
    print(f"Web UI running at {url}")
    if not args.no_browser:
        try:
            import webbrowser  # Lazy import
            webbrowser.open(url, new=2)
        except Exception as exc:  # pragma: no cover - convenience path
            print(f"Unable to open browser automatically: {exc}")

    print("Press Ctrl+C to stop.")

    try:
        while not state.shutdown.is_set():
            time.sleep(0.5)
    except KeyboardInterrupt:
        print()
        print("Shutting down...")
    server.shutdown()
    server.server_close()
    server_thread.join(timeout=2.0)
    reader_thread.join(timeout=2.0)
    if serial_conn.is_open:
        serial_conn.close()
    print("Done.")


if __name__ == "__main__":
    main()
