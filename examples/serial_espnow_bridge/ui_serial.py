#!/usr/bin/env python3
"""
Qt-based PID tuning UI for the ESP32 serial-to-ESPNOW bridge.

Provides a desktop alternative to the web UI so packets can be edited locally
and sent straight to the bridge over a selected serial port.
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from collections import deque
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

try:  # Prefer PyQt5 but fall back to PySide6 if available.
    from PyQt5 import QtCore, QtGui, QtWidgets  # type: ignore

    Signal = QtCore.pyqtSignal  # type: ignore[attr-defined]
    Slot = QtCore.pyqtSlot  # type: ignore[attr-defined]
except ImportError:
    try:
        from PySide6 import QtCore, QtGui, QtWidgets  # type: ignore

        Signal = QtCore.Signal  # type: ignore[attr-defined]
        Slot = QtCore.Slot  # type: ignore[attr-defined]
    except ImportError as exc:  # pragma: no cover - dependency guard
        raise SystemExit(
            "PyQt5 or PySide6 is required for ui_serial.py; install one of them first."
        ) from exc

import serial
import serial.tools.list_ports


def format_float(value: float) -> str:
    text = f"{value:.2f}"
    if text == "-0.00":
        return "0.00"
    return text


class ChannelConfig:
    BYTE_CHANNELS = 4
    FLOAT_CHANNELS = 5
    FLAG_CHANNELS = 8
    BYTE_SLOT_LABELS = ["id1", "id2", "id3", "id4"]
    FLOAT_SLOT_LABELS = ["value1", "value2", "value3", "value4", "value5"]
    FLAG_SLOT_LABELS = [f"Flag bit {idx}" for idx in range(FLAG_CHANNELS)]
    """Load channel metadata from JSON for dynamic UI construction."""

    def __init__(self, data: Dict[str, Any], source: Path) -> None:
        self.source = source
        self.byte_fields = self._normalize_byte_fields(data.get("bytes", []))
        self.float_fields = self._normalize_float_fields(data.get("floats", []))
        self.flag_fields = self._normalize_flag_fields(data.get("flags", []))

    @classmethod
    def from_file(cls, path: Path) -> "ChannelConfig":
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
        except OSError as exc:
            raise SystemExit(f"Unable to read channel config '{path}': {exc}") from exc
        except json.JSONDecodeError as exc:
            raise SystemExit(f"Invalid JSON in channel config '{path}': {exc}") from exc
        return cls(raw, path)

    def default_payload(self) -> Dict[str, Dict[str, Any]]:
        return {
            "bytes": {
                field["key"]: field["default"]
                for field in self.byte_fields
                if field["available"] and field.get("key") is not None
            },
            "floats": {
                field["key"]: field["default"]
                for field in self.float_fields
                if field["available"] and field.get("key") is not None
            },
            "flags": {
                field["key"]: field["default"]
                for field in self.flag_fields
                if field["available"] and field.get("key") is not None
            },
        }

    def build_packet(self, payload: Dict[str, Dict[str, Any]]) -> Dict[str, Any]:
        bytes_payload = payload.get("bytes", {})
        floats_payload = payload.get("floats", {})
        flags_payload = payload.get("flags", {})

        byte_values: List[int] = []
        named_bytes: List[Dict[str, Any]] = []
        for idx, field in enumerate(self.byte_fields):
            if field["available"] and field.get("key") is not None:
                raw_value = bytes_payload.get(field["key"], field["default"])
            else:
                raw_value = field["default"]
            value = int(raw_value)
            value = max(min(value, field["max"]), field["min"])
            byte_values.append(value & 0xFF)
            named_bytes.append(
                {
                    "key": field["key"],
                    "label": field["label"],
                    "value": value,
                    "available": field["available"],
                }
            )

        float_values: List[float] = []
        named_floats: List[Dict[str, Any]] = []
        for field in self.float_fields:
            if field["available"] and field.get("key") is not None:
                raw_value = floats_payload.get(field["key"], field["default"])
            else:
                raw_value = field["default"]
            value = float(raw_value)
            value = max(min(value, field["max"]), field["min"])
            value = round(value, 2)
            float_values.append(value)
            named_floats.append(
                {
                    "key": field["key"],
                    "label": field["label"],
                    "value": value,
                    "available": field["available"],
                }
            )

        flags_value = 0
        named_flags: List[Dict[str, Any]] = []
        for field in self.flag_fields:
            if field["available"] and field.get("key") is not None:
                state = bool(flags_payload.get(field["key"], field["default"]))
            else:
                state = bool(field["default"])
            if state:
                flags_value |= 1 << field["bit"]
            named_flags.append(
                {
                    "key": field["key"],
                    "label": field["label"],
                    "value": state,
                    "bit": field["bit"],
                    "available": field["available"],
                }
            )

        packet = {
            "id1": byte_values[0],
            "id2": byte_values[1],
            "id3": byte_values[2],
            "id4": byte_values[3],
            "flags": flags_value,
            "named_bytes": named_bytes,
            "named_floats": named_floats,
            "named_flags": named_flags,
        }

        for idx in range(5):
            packet[f"value{idx + 1}"] = float_values[idx]

        return packet

    def packet_to_csv(self, packet: Dict[str, Any]) -> str:
        parts = [
            str(int(packet["id1"])),
            str(int(packet["id2"])),
            str(int(packet["id3"])),
            str(int(packet["id4"])),
            format_float(float(packet["value1"])),
            format_float(float(packet["value2"])),
            format_float(float(packet["value3"])),
            format_float(float(packet["value4"])),
            format_float(float(packet["value5"])),
            str(int(packet["flags"])),
        ]
        return ",".join(parts)

    @staticmethod
    def _infer_decimals(step: float) -> int:
        text = f"{step:.8f}".rstrip("0").rstrip(".")
        if "." in text:
            return max(0, min(6, len(text.split(".")[1])))
        return 0

    def _normalize_byte_fields(self, fields: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        if not isinstance(fields, list):
            raise ValueError("'bytes' must be a list of channel definitions.")
        if len(fields) > self.BYTE_CHANNELS:
            raise ValueError(
                f"'bytes' may define at most {self.BYTE_CHANNELS} channels, found {len(fields)}."
            )
        result: List[Dict[str, Any]] = []
        for idx in range(self.BYTE_CHANNELS):
            if idx < len(fields):
                raw = fields[idx]
                if "key" not in raw:
                    raise ValueError(f"Byte channel #{idx + 1} missing 'key'.")
                key = str(raw["key"])
                label = str(raw.get("label", key))
                minimum = int(raw.get("min", 0))
                maximum = int(raw.get("max", 255))
                if minimum > maximum:
                    raise ValueError(f"Byte channel '{key}' has min greater than max.")
                default = int(raw.get("default", minimum))
                default = max(min(default, maximum), minimum)
                result.append(
                    {
                        "key": key,
                        "label": label,
                        "min": minimum,
                        "max": maximum,
                        "default": default,
                        "available": True,
                        "index": idx,
                    }
                )
            else:
                result.append(self._missing_byte_field(idx))
        return result

    def _normalize_float_fields(self, fields: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        if not isinstance(fields, list):
            raise ValueError("'floats' must be a list of channel definitions.")
        if len(fields) > self.FLOAT_CHANNELS:
            raise ValueError(
                f"'floats' supports at most {self.FLOAT_CHANNELS} channels."
            )
        result: List[Dict[str, Any]] = []
        for idx in range(self.FLOAT_CHANNELS):
            if idx < len(fields):
                raw = fields[idx]
                if "key" not in raw:
                    raise ValueError(f"Float channel #{idx + 1} missing 'key'.")
                key = str(raw["key"])
                label = str(raw.get("label", key))
                minimum = float(raw.get("min", -1000.0))
                maximum = float(raw.get("max", 1000.0))
                if minimum > maximum:
                    raise ValueError(f"Float channel '{key}' has min greater than max.")
                default = float(raw.get("default", minimum))
                default = max(min(default, maximum), minimum)
                step = float(raw.get("step", 0.01))
                if step <= 0:
                    step = 0.01
                if step < 0.01:
                    step = 0.01
                decimals = int(raw.get("decimals", self._infer_decimals(step)))
                decimals = max(0, min(decimals, 2))
                result.append(
                    {
                        "key": key,
                        "label": label,
                        "min": minimum,
                        "max": maximum,
                        "default": default,
                        "step": step,
                        "decimals": decimals,
                        "available": True,
                        "index": idx,
                    }
                )
            else:
                result.append(self._missing_float_field(idx))
        return result

    def _normalize_flag_fields(self, fields: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        if not isinstance(fields, list):
            raise ValueError("'flags' must be a list of channel definitions.")
        if len(fields) > self.FLAG_CHANNELS:
            raise ValueError(
                f"'flags' supports at most {self.FLAG_CHANNELS} channels (bits 0-7)."
            )
        slots: List[Dict[str, Any]] = [
            self._missing_flag_field(bit) for bit in range(self.FLAG_CHANNELS)
        ]
        used_bits = set()
        next_bit = 0
        for idx, raw in enumerate(fields):
            if "key" not in raw:
                raise ValueError(f"Flag channel #{idx + 1} missing 'key'.")
            key = str(raw["key"])
            label = str(raw.get("label", key))
            bit_raw = raw.get("bit")
            if bit_raw is None:
                while next_bit in used_bits and next_bit < self.FLAG_CHANNELS:
                    next_bit += 1
                bit = next_bit
            else:
                bit = int(bit_raw)
            if not 0 <= bit < self.FLAG_CHANNELS:
                raise ValueError(f"Flag channel '{key}' assigned invalid bit {bit}.")
            if bit in used_bits:
                raise ValueError(f"Flag bit {bit} is assigned more than once.")
            used_bits.add(bit)
            if next_bit == bit:
                next_bit += 1
            default = bool(raw.get("default", False))
            slots[bit] = {
                "key": key,
                "label": label,
                "bit": bit,
                "default": default,
                "available": True,
            }
        return slots

    def _missing_byte_field(self, index: int) -> Dict[str, Any]:
        return {
            "key": None,
            "label": "N/A",
            "min": 0,
            "max": 255,
            "default": 0,
            "available": False,
            "index": index,
        }

    def _missing_float_field(self, index: int) -> Dict[str, Any]:
        return {
            "key": None,
            "label": "N/A",
            "min": 0.0,
            "max": 0.0,
            "default": 0.0,
            "step": 0.01,
            "decimals": 2,
            "available": False,
            "index": index,
        }

    def _missing_flag_field(self, bit: int) -> Dict[str, Any]:
        return {
            "key": None,
            "label": "N/A",
            "bit": bit,
            "default": False,
            "available": False,
        }


class SerialManager(QtCore.QObject):
    log_received = Signal(str)
    error_occurred = Signal(str)
    status_changed = Signal(object, int)  # emits port (str or None) and baud
    packet_sent = Signal(dict)

    def __init__(self, config: ChannelConfig) -> None:
        super().__init__()
        self.config = config
        self.serial: Optional[serial.Serial] = None
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self.last_packet: Optional[Dict[str, Any]] = None

    def is_open(self) -> bool:
        return bool(self.serial and self.serial.is_open)

    def open(self, port: str, baud: int) -> None:
        self.close()
        try:
            self.serial = serial.Serial(port, baud, timeout=0.1)
        except serial.SerialException as exc:
            raise ValueError(f"Failed to open serial port {port}: {exc}")
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._thread.start()
        self.status_changed.emit(self.serial.port, self.serial.baudrate)

    def close(self) -> None:
        self._stop_event.set()
        thread = self._thread
        if thread and thread.is_alive():
            thread.join(timeout=1.0)
        self._thread = None
        if self.serial and self.serial.is_open:
            try:
                self.serial.close()
            except serial.SerialException:
                pass
        self.serial = None
        self.status_changed.emit(None, 0)

    def send_payload(self, data: Dict[str, Dict[str, Any]]) -> Dict[str, Any]:
        if not self.serial or not self.serial.is_open:
            raise ValueError("Serial port is not open")
        packet = self.config.build_packet(data)
        payload_csv = self.config.packet_to_csv(packet)
        timestamp = datetime.now(timezone.utc).isoformat()
        try:
            with self._lock:
                self.serial.write((payload_csv + "\n").encode("ascii"))
                self.serial.flush()
        except serial.SerialException as exc:
            raise ValueError(f"Serial write failed: {exc}") from exc
        record = {**packet, "raw": payload_csv, "sent_at": timestamp}
        self.last_packet = record
        self.packet_sent.emit(record)
        return record

    def _reader_loop(self) -> None:
        assert self.serial
        while not self._stop_event.is_set():
            try:
                line = self.serial.readline()
            except serial.SerialException as exc:
                self.error_occurred.emit(f"Serial error: {exc}")
                break
            if not line:
                continue
            decoded = line.decode("utf-8", errors="replace").strip()
            if decoded:
                self.log_received.emit(decoded)
        self._stop_event.clear()


class MainWindow(QtWidgets.QMainWindow):
    AUTO_SEND_DELAY_MS = 100

    def __init__(
        self,
        config: ChannelConfig,
        default_port: Optional[str] = None,
        default_baud: int = 115200,
        auto_connect: bool = False,
    ) -> None:
        super().__init__()
        self.config = config
        self._default_port = default_port
        self.setWindowTitle("ESP32 Remote Control - Serial UI")
        self.resize(760, 680)

        self.manager = SerialManager(config)
        self.manager.log_received.connect(self.on_log_received)
        self.manager.error_occurred.connect(self.on_serial_error)
        self.manager.status_changed.connect(self.on_status_changed)
        self.manager.packet_sent.connect(self.on_packet_sent)

        self.log_entries = deque(maxlen=200)

        self.byte_controls: Dict[str, QtWidgets.QSpinBox] = {}
        self.float_controls: Dict[str, QtWidgets.QDoubleSpinBox] = {}
        self.flag_controls: Dict[str, QtWidgets.QCheckBox] = {}

        central = QtWidgets.QWidget(self)
        self.setCentralWidget(central)
        outer_layout = QtWidgets.QVBoxLayout(central)

        connection_group = self._build_connection_group(default_baud)
        outer_layout.addWidget(connection_group)

        self.byte_group = None
        self.float_group = None
        self.flag_group = None
        if self.config.byte_fields:
            self.byte_group = self._build_byte_group()
            outer_layout.addWidget(self.byte_group)
        if self.config.float_fields:
            self.float_group = self._build_float_group()
            outer_layout.addWidget(self.float_group)
        if self.config.flag_fields:
            self.flag_group = self._build_flag_group()
            outer_layout.addWidget(self.flag_group)

        self.send_group = self._build_send_controls()
        outer_layout.addWidget(self.send_group)
        self.status_group = self._build_status_group()
        outer_layout.addWidget(self.status_group)
        self.log_group = self._build_log_view()
        outer_layout.addWidget(self.log_group)

        self.auto_timer = QtCore.QTimer(self)
        self.auto_timer.setSingleShot(True)
        self.auto_timer.timeout.connect(self._auto_send_timeout)

        if default_port:
            self.port_combo.setCurrentText(default_port)
        if auto_connect and default_port:
            QtCore.QTimer.singleShot(200, self.connect_serial)
        else:
            self._set_interaction_enabled(False)

    def _build_connection_group(self, default_baud: int) -> QtWidgets.QGroupBox:
        group = QtWidgets.QGroupBox("Connection", self)
        layout = QtWidgets.QGridLayout(group)

        layout.addWidget(QtWidgets.QLabel("Port:", group), 0, 0)
        self.port_combo = QtWidgets.QComboBox(group)
        self.port_combo.setEditable(True)
        layout.addWidget(self.port_combo, 0, 1)
        refresh_button = QtWidgets.QPushButton("Refresh", group)
        refresh_button.clicked.connect(self.refresh_ports)
        layout.addWidget(refresh_button, 0, 2)

        layout.addWidget(QtWidgets.QLabel("Baud:", group), 1, 0)
        self.baud_combo = QtWidgets.QComboBox(group)
        for value in (115200, 57600, 230400, 460800, 921600):
            self.baud_combo.addItem(str(value))
        idx = self.baud_combo.findText(str(default_baud))
        if idx >= 0:
            self.baud_combo.setCurrentIndex(idx)
        layout.addWidget(self.baud_combo, 1, 1)

        self.connect_button = QtWidgets.QPushButton("Connect", group)
        self.connect_button.clicked.connect(self.connect_serial)
        layout.addWidget(self.connect_button, 1, 2)

        self.refresh_ports()
        if self._default_port:
            self.port_combo.setCurrentText(self._default_port)

        return group

    def _build_byte_group(self) -> QtWidgets.QGroupBox:
        group = QtWidgets.QGroupBox("Int Channels (id1-id4)", self)
        layout = QtWidgets.QGridLayout(group)
        for idx, field in enumerate(self.config.byte_fields):
            row = idx // 2
            col = (idx % 2) * 2
            label_text = f"{field['label']}:"
            label = QtWidgets.QLabel(label_text)
            label.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
            spin = QtWidgets.QSpinBox(group)
            spin.setRange(field["min"], field["max"])
            spin.setValue(field["default"])
            if not field["available"]:
                label.setEnabled(False)
                spin.setEnabled(False)
                spin.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
                spin.setReadOnly(True)
                slot_name = ChannelConfig.BYTE_SLOT_LABELS[field["index"]]
                spin.setToolTip(
                    f"{slot_name} not defined in config; using default value {field['default']}."
                )
                label.setToolTip(spin.toolTip())
            else:
                self.byte_controls[field["key"]] = spin
            layout.addWidget(label, row, col)
            layout.addWidget(spin, row, col + 1)
        return group

    def _build_float_group(self) -> QtWidgets.QGroupBox:
        group = QtWidgets.QGroupBox("Float Channels (value1-value5)", self)
        layout = QtWidgets.QGridLayout(group)
        columns = 3
        for idx, field in enumerate(self.config.float_fields):
            row = idx // columns
            col = (idx % columns) * 2
            label = QtWidgets.QLabel(f"{field['label']}:")
            label.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
            spin = QtWidgets.QDoubleSpinBox(group)
            spin.setRange(field["min"], field["max"])
            spin.setDecimals(field["decimals"])
            spin.setSingleStep(field["step"])
            spin.setValue(field["default"])
            if not field["available"]:
                label.setEnabled(False)
                spin.setEnabled(False)
                spin.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
                spin.setReadOnly(True)
                slot_name = ChannelConfig.FLOAT_SLOT_LABELS[field["index"]]
                spin.setToolTip(
                    f"{slot_name} not defined in config; using default value {field['default']}."
                )
                label.setToolTip(spin.toolTip())
            else:
                self.float_controls[field["key"]] = spin
            layout.addWidget(label, row, col)
            layout.addWidget(spin, row, col + 1)
        return group

    def _build_flag_group(self) -> QtWidgets.QGroupBox:
        group = QtWidgets.QGroupBox("Flag Channels", self)
        layout = QtWidgets.QGridLayout(group)
        columns = 2
        for idx, field in enumerate(self.config.flag_fields):
            row = idx // columns
            col = idx % columns
            if field["available"]:
                text = f"{field['label']} (bit {field['bit']})"
            else:
                text = "N/A"
            checkbox = QtWidgets.QCheckBox(text, group)
            checkbox.setChecked(field["default"])
            if not field["available"]:
                checkbox.setEnabled(False)
                checkbox.setToolTip(
                    f"Flag bit {field['bit']} not defined in config; using default value {field['default']}."
                )
            else:
                self.flag_controls[field["key"]] = checkbox
            if not field["available"]:
                checkbox.setTristate(False)
            layout.addWidget(checkbox, row, col)
        return group

    def _build_send_controls(self) -> QtWidgets.QGroupBox:
        group = QtWidgets.QGroupBox("Send Control", self)
        layout = QtWidgets.QGridLayout(group)
        self.auto_send_check = QtWidgets.QCheckBox("Auto send on change")
        layout.addWidget(self.auto_send_check, 0, 0, 1, 2)
        self.send_button = QtWidgets.QPushButton("Send Packet")
        self.send_button.clicked.connect(self.send_manual)
        layout.addWidget(self.send_button, 1, 0)
        self.auto_hint_label = QtWidgets.QLabel(
            "Manual mode: press Send Packet to transmit."
        )
        layout.addWidget(self.auto_hint_label, 1, 1)
        self.auto_send_check.stateChanged.connect(self.on_auto_mode_toggled)
        for widget in self._inputs_for_auto():
            if isinstance(widget, QtWidgets.QDoubleSpinBox):
                widget.valueChanged.connect(self.on_input_changed)
            elif isinstance(widget, QtWidgets.QSpinBox):
                widget.valueChanged.connect(self.on_input_changed)
            elif isinstance(widget, QtWidgets.QCheckBox):
                widget.stateChanged.connect(self.on_checkbox_changed)
            if isinstance(widget, QtWidgets.QAbstractSpinBox):
                widget.editingFinished.connect(self.on_spin_editing_finished)
        return group

    def _build_status_group(self) -> QtWidgets.QGroupBox:
        group = QtWidgets.QGroupBox("Status", self)
        layout = QtWidgets.QGridLayout(group)
        layout.addWidget(QtWidgets.QLabel("Serial Port:"), 0, 0)
        self.status_port_label = QtWidgets.QLabel("--")
        layout.addWidget(self.status_port_label, 0, 1)
        layout.addWidget(QtWidgets.QLabel("Last Packet:"), 1, 0)
        self.status_packet_label = QtWidgets.QLabel("No packets sent yet.")
        self.status_packet_label.setWordWrap(True)
        layout.addWidget(self.status_packet_label, 1, 1)
        layout.addWidget(QtWidgets.QLabel("Errors:"), 2, 0)
        self.status_error_label = QtWidgets.QLabel("None.")
        self.status_error_label.setWordWrap(True)
        layout.addWidget(self.status_error_label, 2, 1)
        return group

    def _build_log_view(self) -> QtWidgets.QGroupBox:
        group = QtWidgets.QGroupBox("Bridge Output", self)
        layout = QtWidgets.QVBoxLayout(group)
        self.log_view = QtWidgets.QPlainTextEdit(group)
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(1200)
        self.log_view.setMinimumHeight(260)
        self.log_view.setPlaceholderText("Waiting for data...")
        layout.addWidget(self.log_view)
        return group

    def _inputs_for_auto(self) -> List[QtWidgets.QWidget]:
        widgets: List[QtWidgets.QWidget] = []
        widgets.extend(self.float_controls.values())
        widgets.extend(self.byte_controls.values())
        widgets.extend(self.flag_controls.values())
        return widgets

    def _set_interaction_enabled(self, enabled: bool) -> None:
        targets = [
            self.byte_group,
            self.float_group,
            self.flag_group,
            self.send_group,
            self.status_group,
            self.log_group,
        ]
        for widget in targets:
            if widget is not None:
                widget.setEnabled(enabled)
        if not enabled:
            self.auto_timer.stop()

    @Slot()
    def refresh_ports(self) -> None:
        current = self.port_combo.currentText()
        self.port_combo.clear()
        ports = serial.tools.list_ports.comports()
        for port in ports:
            display = f"{port.device} ({port.description})"
            self.port_combo.addItem(display, port.device)
        if ports:
            self.port_combo.setCurrentIndex(0)
        if current:
            idx = self.port_combo.findData(current)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)
            else:
                self.port_combo.setEditText(current)
        elif self._default_port:
            self.port_combo.setEditText(self._default_port)

    @Slot()
    def connect_serial(self) -> None:
        if self.manager.is_open():
            self.manager.close()
            self.connect_button.setText("Connect")
            self.status_error_label.setText("None.")
            self._set_interaction_enabled(False)
            return
        port = self._selected_port()
        if not port:
            QtWidgets.QMessageBox.warning(self, "Serial Port", "No serial port selected.")
            return
        try:
            baud = int(self.baud_combo.currentText())
        except ValueError:
            QtWidgets.QMessageBox.warning(self, "Serial Port", "Invalid baud rate.")
            return
        try:
            self.manager.open(port, baud)
        except ValueError as exc:
            QtWidgets.QMessageBox.critical(self, "Serial Port", str(exc))
            return
        self._set_interaction_enabled(True)
        self.connect_button.setText("Disconnect")
        if self.auto_send_check.isChecked():
            self.schedule_auto_send(immediate=True)

    def _selected_port(self) -> Optional[str]:
        if self.port_combo.count() == 0:
            return self.port_combo.currentText().strip()
        data = self.port_combo.currentData()
        if data:
            return str(data)
        text = self.port_combo.currentText().strip()
        if "(" in text and text.endswith(")"):
            return text.split("(", 1)[0].strip()
        return text

    def collect_payload(self) -> Dict[str, Dict[str, Any]]:
        payload = {
            "bytes": {},
            "floats": {},
            "flags": {},
        }  # type: Dict[str, Dict[str, Any]]
        for field in self.config.byte_fields:
            if not field["available"] or field.get("key") is None:
                continue
            control = self.byte_controls.get(field["key"])
            if control is not None:
                text = control.text().strip()
                value: int
                try:
                    value = int(text)
                except (ValueError, TypeError):
                    value = int(control.value())
                value = max(control.minimum(), min(control.maximum(), value))
                if control.value() != value:
                    control.blockSignals(True)
                    control.setValue(value)
                    control.blockSignals(False)
                payload["bytes"][field["key"]] = value
        for field in self.config.float_fields:
            if not field["available"] or field.get("key") is None:
                continue
            control = self.float_controls.get(field["key"])
            if control is not None:
                text = control.text().strip()
                value: float
                try:
                    value = float(text)
                except (ValueError, TypeError):
                    value = float(control.value())
                value = max(control.minimum(), min(control.maximum(), value))
                payload["floats"][field["key"]] = round(value, 2)
                rounded = payload["floats"][field["key"]]
                if abs(control.value() - rounded) > 1e-6:
                    control.blockSignals(True)
                    control.setValue(rounded)
                    control.blockSignals(False)
        for field in self.config.flag_fields:
            if not field["available"] or field.get("key") is None:
                continue
            control = self.flag_controls.get(field["key"])
            if control is not None:
                payload["flags"][field["key"]] = bool(control.isChecked())
        return payload

    @Slot()
    def send_manual(self) -> None:
        if self.auto_send_check.isChecked():
            return
        self._try_send_payload("manual")

    def _auto_send_timeout(self) -> None:
        self.auto_timer.stop()
        if not self.auto_send_check.isChecked():
            return
        if not self.manager.is_open():
            return
        self._try_send_payload("auto")

    def schedule_auto_send(self, *, immediate: bool = False) -> None:
        if not self.auto_send_check.isChecked():
            return
        if not self.manager.is_open():
            return
        self.auto_timer.stop()
        if immediate or self.AUTO_SEND_DELAY_MS <= 0:
            self._auto_send_timeout()
        else:
            self.auto_timer.start(self.AUTO_SEND_DELAY_MS)

    def on_input_changed(self, _value: object = None) -> None:
        self.schedule_auto_send()

    @Slot(int)
    def on_checkbox_changed(self, _state: int) -> None:
        self.schedule_auto_send(immediate=True)

    @Slot()
    def on_spin_editing_finished(self) -> None:
        if self.auto_send_check.isChecked():
            self.schedule_auto_send(immediate=True)

    @Slot(int)
    def on_auto_mode_toggled(self, state: int) -> None:
        auto_mode = state == QtCore.Qt.Checked
        self.send_button.setEnabled(not auto_mode)
        self.auto_hint_label.setText(
            "Auto mode: changes send within ~100ms."
            if auto_mode
            else "Manual mode: press Send Packet to transmit."
        )
        if auto_mode and self.manager.is_open():
            self.schedule_auto_send(immediate=True)
        else:
            self.auto_timer.stop()

    def _try_send_payload(self, source: str) -> None:
        if not self.manager.is_open():
            QtWidgets.QMessageBox.warning(
                self, "Serial Port", "Serial port is not connected."
            )
            return
        payload = self.collect_payload()
        try:
            record = self.manager.send_payload(payload)
        except ValueError as exc:
            self.status_error_label.setText(str(exc))
            if source == "manual":
                QtWidgets.QMessageBox.critical(self, "Send Packet", str(exc))
            return
        self.status_error_label.setText("None.")
        self._update_last_packet(record)

    @Slot(object, int)
    def on_status_changed(self, port: Optional[str], baud: int) -> None:
        if port:
            self.status_port_label.setText(f"{port} @ {baud}")
            self._set_interaction_enabled(True)
        else:
            self.status_port_label.setText("--")
            self.connect_button.setText("Connect")
            self.auto_timer.stop()
            self._set_interaction_enabled(False)

    @Slot(dict)
    def on_packet_sent(self, record: Dict[str, Any]) -> None:
        self._update_last_packet(record)

    def _update_last_packet(self, record: Dict[str, Any]) -> None:
        parts: List[str] = []
        for item in record.get("named_bytes", []):
            parts.append(f"{item['label']}={int(item['value'])}")
        for item in record.get("named_floats", []):
            parts.append(f"{item['label']}={format_float(float(item['value']))}")
        flag_items = record.get("named_flags", [])
        if flag_items:
            flag_summary = ", ".join(
                f"{entry['label']}={'on' if entry['value'] else 'off'}" for entry in flag_items
            )
            parts.append(f"Flags: {flag_summary}")
        summary = "; ".join(parts) if parts else "Packet sent"
        sent_at = record.get("sent_at")
        if sent_at:
            summary = f"{summary} @ {sent_at}"
        self.status_packet_label.setText(summary)

    @Slot(str)
    def on_log_received(self, line: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        entry = f"[{timestamp}] {line}"
        self.log_entries.append(entry)
        self.log_view.appendPlainText(entry)

    @Slot(str)
    def on_serial_error(self, message: str) -> None:
        self.status_error_label.setText(message)
        QtWidgets.QMessageBox.critical(self, "Serial Error", message)
        self.manager.close()

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:  # type: ignore[name-defined]
        self.manager.close()
        super().closeEvent(event)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Qt UI for sending remote-control packets to the ESP32 serial bridge."
    )
    parser.add_argument(
        "--serial-port",
        dest="port",
        help="Serial port name, e.g. COM5 or /dev/ttyUSB0.",
    )
    parser.add_argument(
        "--baud", type=int, default=115200, help="Serial baud rate (default: 115200)."
    )
    parser.add_argument(
        "--auto-connect",
        action="store_true",
        help="Attempt to connect automatically when the UI starts.",
    )
    parser.add_argument(
        "--config",
        help="Path to channel config JSON (default: ui_channels.json next to this script).",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.config:
        config_path = Path(args.config)
    else:
        config_path = Path(__file__).with_name("ui_channels.json")
    config = ChannelConfig.from_file(config_path)

    app = QtWidgets.QApplication(sys.argv)
    window = MainWindow(
        config,
        args.port,
        args.baud,
        auto_connect=args.auto_connect,
    )
    window.show()

    QtCore.QTimer.singleShot(0, lambda: window.status_error_label.setText("None."))
    app.aboutToQuit.connect(window.manager.close)
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
