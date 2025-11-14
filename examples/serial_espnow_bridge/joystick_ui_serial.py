#!/usr/bin/env python3
"""
Qt UI with gamepad control for the ESP32 serial-to-ESPNOW bridge.

This script reuses the serial UI mechanics from ui_serial.py and layers on
optional joystick/gamepad control. When enabled, joystick axes update float
channels and buttons toggle flag channels, then payloads are sent to the
bridge (leveraging the same auto-send behavior).

Dependencies:
- PyQt5 (preferred) or PySide6
- pyserial
- pygame (optional; required for joystick input)

Usage examples:
  python joystick_ui_serial.py --serial-port COM5 --auto-connect
  python joystick_ui_serial.py --axis-deadzone 0.15 --poll-hz 60
"""

from __future__ import annotations

import sys
import os
import ctypes

import json
from typing import Any, Dict, List, Optional, Tuple

## No CLI utility mode: this build is UI-only without command arguments.

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
            "PyQt5 or PySide6 is required for joystick_ui_serial.py; install one of them first."
        ) from exc

# Reuse the existing channel/serial UI machinery
import ui_serial  # type: ignore


# ----- XInput ctypes structures (module-level so they are in scope) -----
class _XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [
        ("wButtons", ctypes.c_ushort),
        ("bLeftTrigger", ctypes.c_ubyte),
        ("bRightTrigger", ctypes.c_ubyte),
        ("sThumbLX", ctypes.c_short),
        ("sThumbLY", ctypes.c_short),
        ("sThumbRX", ctypes.c_short),
        ("sThumbRY", ctypes.c_short),
    ]


class _XINPUT_STATE(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_uint), ("Gamepad", _XINPUT_GAMEPAD)]


class JoystickView(QtWidgets.QWidget):
    """Painted view of a PlayStation-style controller (self-contained).

    Uses relative positioning so the layout scales with available space and
    keeps labels readable. The goal is clarity over photorealism.
    """

    def __init__(self, parent: Optional[QtWidgets.QWidget] = None) -> None:
        super().__init__(parent)
        # Give the drawing more breathing room by default
        self.setMinimumSize(520, 320)
        self.state = {
            "dpad": {"up": False, "down": False, "left": False, "right": False},
            "face": {"triangle": False, "circle": False, "cross": False, "square": False},
            "shoulder": {"l1": False, "r1": False, "l2": 0.0, "r2": 0.0},
            "center": {"select": False, "start": False, "l3": False, "r3": False},
            "sticks": {"lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0},
        }

    def sizeHint(self) -> QtCore.QSize:  # type: ignore[override]
        return QtCore.QSize(400, 260)

    def update_state(self, new_state: Dict[str, Any]) -> None:
        self.state.update(new_state)
        self.update()

    def _draw_round_button(self, p: QtGui.QPainter, center: QtCore.QPointF, r: float, color: QtGui.QColor, on: bool, label: str) -> None:
        p.save()
        p.setRenderHint(QtGui.QPainter.Antialiasing, True)
        if on:
            brush = QtGui.QBrush(color)
        else:
            brush = QtGui.QBrush(QtGui.QColor("#e0e0e0"))
        pen = QtGui.QPen(QtGui.QColor("#424242"))
        pen.setWidthF(1.0)
        p.setPen(pen)
        p.setBrush(brush)
        p.drawEllipse(center, r, r)
        # label
        p.setPen(QtGui.QPen(QtGui.QColor("#212121" if on else "#616161")))
        font = p.font(); font.setPointSize(8); p.setFont(font)
        rect = QtCore.QRectF(center.x()-r, center.y()-r, 2*r, 2*r)
        p.drawText(rect, QtCore.Qt.AlignCenter, label)
        p.restore()

    def _draw_rect_button(self, p: QtGui.QPainter, rect: QtCore.QRectF, on: bool, text: str) -> None:
        p.save()
        p.setRenderHint(QtGui.QPainter.Antialiasing, True)
        p.setPen(QtGui.QPen(QtGui.QColor("#616161")))
        p.setBrush(QtGui.QBrush(QtGui.QColor("#c8e6c9" if on else "#f5f5f5")))
        p.drawRoundedRect(rect, 4, 4)
        p.setPen(QtGui.QPen(QtGui.QColor("#1b5e20" if on else "#424242")))
        font = p.font(); font.setPointSize(8); p.setFont(font)
        p.drawText(rect, QtCore.Qt.AlignCenter, text)
        p.restore()

    def paintEvent(self, event: QtGui.QPaintEvent) -> None:  # type: ignore[override]
        p = QtGui.QPainter(self)
        p.fillRect(self.rect(), QtGui.QColor("#fafafa"))

        w = self.width(); h = self.height()
        cx = w * 0.5; cy = h * 0.60
        scale = min(w, h)  # scale factor for relative placement

        # D‑Pad cluster: four square tiles around a center.
        d = self.state["dpad"]
        dpad_size = max(22.0, 0.11 * scale)
        center = QtCore.QPointF(cx - 0.5 * scale, cy)
        # Use arrow glyphs for clarity
        self._draw_rect_button(p, QtCore.QRectF(center.x()-dpad_size/2, center.y()-dpad_size*1.5, dpad_size, dpad_size), d.get("up", False), "🠉")
        self._draw_rect_button(p, QtCore.QRectF(center.x()-dpad_size*1.5, center.y()-dpad_size/2, dpad_size, dpad_size), d.get("left", False), "🠈")
        self._draw_rect_button(p, QtCore.QRectF(center.x()+dpad_size/2, center.y()-dpad_size/2, dpad_size, dpad_size),   d.get("right", False), "🠊")
        self._draw_rect_button(p, QtCore.QRectF(center.x()-dpad_size/2, center.y()+dpad_size/2, dpad_size, dpad_size),   d.get("down", False), "🠋")

        # Face buttons cluster: 1,2,3,4
        face = self.state["face"]
        face_center = QtCore.QPointF(cx + 0.5 * scale, cy)
        r = max(14.0, 0.060 * scale)
        off = 0.13 * scale
        self._draw_round_button(p, QtCore.QPointF(face_center.x(),       face_center.y()-off), r, QtGui.QColor("#00c853"), face.get("triangle", False), "1")
        self._draw_round_button(p, QtCore.QPointF(face_center.x()+off,   face_center.y()),    r, QtGui.QColor("#ff5252"), face.get("circle", False),   "2")
        self._draw_round_button(p, QtCore.QPointF(face_center.x(),       face_center.y()+off), r, QtGui.QColor("#448aff"), face.get("cross", False),    "3")
        self._draw_round_button(p, QtCore.QPointF(face_center.x()-off,   face_center.y()),    r, QtGui.QColor("#e040fb"), face.get("square", False),   "4")

        # Shoulders / Triggers row (L1/R1) + L2/R2 progress bars
        shoulder = self.state["shoulder"]
        top_y = cy - 0.50 * scale
        rect_w = 0.20 * scale
        rect_h = max(18.0, 0.06 * scale)
        l1_rect = QtCore.QRectF(cx - 0.46 * scale, top_y, rect_w, rect_h)
        r1_rect = QtCore.QRectF(cx + 0.26 * scale, top_y, rect_w, rect_h)
        self._draw_rect_button(p, l1_rect, shoulder.get("l1", False), "L1")
        self._draw_rect_button(p, r1_rect, shoulder.get("r1", False), "R1")
        
        # Triggers (L2/R2) as buttons under L1/R1
        l2_on = float(shoulder.get("l2", 0.0)) > 0.1
        r2_on = float(shoulder.get("r2", 0.0)) > 0.1
        l2_btn = QtCore.QRectF(l1_rect.x(), l1_rect.bottom() + 8, rect_w, rect_h)
        r2_btn = QtCore.QRectF(r1_rect.x(), r1_rect.bottom() + 8, rect_w, rect_h)
        self._draw_rect_button(p, l2_btn, l2_on, "L2")
        self._draw_rect_button(p, r2_btn, r2_on, "R2")

        # Sticks
        sticks = self.state["sticks"]
        sr = max(20.0, 0.15 * scale)
        def draw_stick(cx0: float, cy0: float, on: bool, x: float, y: float, label: str) -> None:
            p.save(); p.setRenderHint(QtGui.QPainter.Antialiasing, True)
            p.setPen(QtGui.QPen(QtGui.QColor("#757575")))
            p.setBrush(QtGui.QBrush(QtGui.QColor("#eeeeee")))
            p.drawEllipse(QtCore.QPointF(cx0, cy0), sr, sr)
            dot = QtCore.QPointF(cx0 + x*sr*0.75, cy0 + y*sr*0.75)
            p.setBrush(QtGui.QBrush(QtGui.QColor("#424242" if on else "#9e9e9e")))
            p.drawEllipse(dot, 6, 6)
            p.setPen(QtGui.QPen(QtGui.QColor("#424242")))
            font = p.font(); font.setPointSize(8); p.setFont(font)
            p.drawText(QtCore.QRectF(cx0 - sr, cy0 - sr, sr*2, sr*2), QtCore.Qt.AlignCenter, label)
            p.restore()
        draw_stick(cx - 0.20 * scale, cy + 0.20 * scale, bool(self.state["center"].get("l3", False)), float(sticks.get("lx", 0.0)), float(sticks.get("ly", 0.0)), "L3")
        draw_stick(cx + 0.20 * scale, cy + 0.20 * scale, bool(self.state["center"].get("r3", False)), float(sticks.get("rx", 0.0)), float(sticks.get("ry", 0.0)), "R3")

        # Center buttons
        c = self.state["center"]
        share_rect = QtCore.QRectF(cx - 0.3 * scale, cy - 0.30 * scale, 0.3 * scale, 0.12 * scale)
        start_rect = QtCore.QRectF(cx + 0.0 * scale, cy - 0.30 * scale, 0.3 * scale, 0.12 * scale)
        self._draw_rect_button(p, share_rect, c.get("select", False), "SELECT")
        self._draw_rect_button(p, start_rect, c.get("start", False), "START")


class JoystickSupport:
    """Joystick backend wrapper for pygame and XInput (Windows).
 
    Provides:
    - Small, backend-agnostic API: available(), open(index), close(), poll().
    - Basic normalization across backends (D-Pad hat on XInput; trigger ranges).
    - Minimal state so a Qt timer can poll safely.
    """

    def __init__(self) -> None:
        self._pygame_ok = False
        # Explicitly type pygame handle as Any so static analysis
        # doesn't treat it as possibly None when accessed.
        self._pygame: Any = None
        self._joystick: Optional[Any] = None
        self._last_buttons: List[int] = []
        self._last_axes: List[float] = []
        self._initialized = False
        # Track per-axis range behavior: 'unknown'|'bipolar'|'unipolar'
        self._axis_mode: List[str] = []
        # XInput (Windows) fallback
        self._xinput_ok = False
        self._xinput_id: Optional[int] = None

        try:
            import pygame  # type: ignore

            self._pygame = pygame
            self._pygame_ok = True
        except Exception:
            self._pygame_ok = False
            self._pygame = None
        # Try load XInput DLL on Windows
        self._xinput = None
        if os.name == 'nt':
            for dll in ("xinput1_4.dll", "xinput1_3.dll", "XInput9_1_0.dll"):
                try:
                    self._xinput = ctypes.windll.LoadLibrary(dll)
                    # Prototype XInputGetState
                    self._xinput.XInputGetState.argtypes = [ctypes.c_uint, ctypes.c_void_p]
                    self._xinput.XInputGetState.restype = ctypes.c_uint
                    self._xinput_ok = True
                    break
                except Exception:
                    self._xinput = None

    @property
    def available(self) -> bool:
        return self._pygame_ok or self._xinput_ok

    def init(self) -> None:
        if self._initialized:
            return
        if self._pygame_ok:
            # Match the reference snippet: init core + joystick only
            try:
                self._pygame.init()
            except Exception:
                pass
            try:
                self._pygame.joystick.init()
            except Exception:
                pass
        self._initialized = True

    def quit(self) -> None:
        if not self._pygame_ok:
            return
        try:
            if self._joystick is not None:
                self._joystick.quit()  # type: ignore[attr-defined]
        except Exception:
            pass
        try:
            self._pygame.joystick.quit()
        except Exception:
            pass
        try:
            self._pygame.quit()
        except Exception:
            pass
        self._joystick = None
        self._initialized = False

    def list_devices(self) -> List[str]:
        self.init()
        names: List[str] = []
        if self._pygame_ok:
            try:
                count = self._pygame.joystick.get_count()
            except Exception:
                count = 0
            for i in range(count):
                try:
                    joy = self._pygame.joystick.Joystick(i)
                    joy.init()
                    names.append(f"{i}: {joy.get_name()}")
                    joy.quit()
                except Exception:
                    names.append(f"{i}: <device {i}>")
        # Append XInput devices as high indices (100+pad)
        if self._xinput_ok:
            for pad in range(4):
                if self._xinput_connected(pad):
                    names.append(f"{100+pad}: XInput Controller {pad}")
        return names

    def open(self, index: int) -> Optional[str]:
        self.init()
        # XInput selection if index >= 100
        if index >= 100 and self._xinput_ok:
            pad = index - 100
            if self._xinput_connected(pad):
                # Release pygame device if any
                if self._joystick is not None:
                    try:
                        self._joystick.quit()
                    except Exception:
                        pass
                self._joystick = None
                self._xinput_id = pad
                # Prime arrays: 8 axes (LX,LY,RX,RY,LT,RT,HX,HY), 16 buttons
                self._last_axes = [0.0] * 8
                self._last_buttons = [0] * 16
                return f"XInput Controller {pad}"
            return None
        # Default to pygame if available
        if not self._pygame_ok:
            return None
        try:
            if self._joystick is not None:
                try:
                    self._joystick.quit()
                except Exception:
                    pass
            joy = self._pygame.joystick.Joystick(index)
            joy.init()
            self._joystick = joy
            self._xinput_id = None
            # Prime state arrays
            self._last_axes = [0.0] * joy.get_numaxes()
            self._last_buttons = [0] * joy.get_numbuttons()
            self._axis_mode = ["unknown"] * joy.get_numaxes()
            return joy.get_name()
        except Exception:
            self._joystick = None
            return None

    def close(self) -> None:
        try:
            if self._joystick is not None:
                self._joystick.quit()
        except Exception:
            pass
        self._joystick = None
        self._xinput_id = None

    def poll(self) -> Dict[str, Any]:
        """Return a snapshot of the current state.

        Returns a dict with two lists:
        - ``axes``: floats, typically in [-1, 1] for sticks; triggers may be
          0..1 (XInput) or -1..1/0..1 (pygame, depends on driver).
        - ``buttons``: integers (0 or 1) for digital button state.
        Returns empty lists when no device is open.
        """
        # XInput polling
        if self._xinput_id is not None and self._xinput_ok:
            axes, buttons = self._xinput_poll(self._xinput_id)
            self._last_axes = axes
            self._last_buttons = buttons
            return {"axes": axes, "buttons": buttons}
        if not self._pygame_ok or self._joystick is None:
            return {"axes": [], "buttons": []}
        # Pump events if possible to get fresh state; if not, proceed anyway
        try:
            self._pygame.event.pump()
        except Exception:
            pass
        axes: List[float] = []
        buttons: List[int] = []
        try:
            j = self._joystick
            assert j is not None
            for i in range(j.get_numaxes()):
                try:
                    axes.append(float(j.get_axis(i)))
                except Exception:
                    axes.append(0.0)
            # Read hats as virtual axes (x,y) appended to axes list
            try:
                hat_count = j.get_numhats()
            except Exception:
                hat_count = 0
            for h in range(hat_count):
                try:
                    hx, hy = j.get_hat(h)
                    # Map hat [-1,0,1] to float in same range
                    axes.append(float(hx))
                    axes.append(float(hy))
                except Exception:
                    axes.append(0.0)
                    axes.append(0.0)
            for i in range(j.get_numbuttons()):
                try:
                    buttons.append(int(j.get_button(i)))
                except Exception:
                    buttons.append(0)
        except Exception:
            axes = []
            buttons = []
        self._last_axes = axes
        self._last_buttons = buttons
        return {"axes": axes, "buttons": buttons}

    # ----- XInput helpers -----
    def _xinput_connected(self, pad: int) -> bool:
        if not self._xinput:
            return False
        state = _XINPUT_STATE()
        res = self._xinput.XInputGetState(pad, ctypes.byref(state))
        return res == 0

    def _xinput_poll(self, pad: int) -> Tuple[List[float], List[int]]:
        """Poll one XInput gamepad and map to axes/buttons arrays.

        Axes:
          0=LX, 1=LY, 2=RX, 3=RY, 4=LT(0..1), 5=RT(0..1), 6=hatX, 7=hatY
        Buttons (0..9): A,B,X,Y,LB,RB,Back,Start,LS,RS
        """
        axes = [0.0] * 8
        buttons = [0] * 16
        if not self._xinput:
            return axes, buttons
        state = _XINPUT_STATE()
        if self._xinput.XInputGetState(pad, ctypes.byref(state)) != 0:
            return axes, buttons
        gp = state.Gamepad
        # Normalize sticks
        def n(v: int) -> float:
            return max(-1.0, min(1.0, v / 32767.0))
        axes[0] = n(gp.sThumbLX)
        axes[1] = n(gp.sThumbLY)
        axes[2] = n(gp.sThumbRX)
        axes[3] = n(gp.sThumbRY)
        # Triggers 0..255 -> 0..1
        axes[4] = float(gp.bLeftTrigger) / 255.0
        axes[5] = float(gp.bRightTrigger) / 255.0
        # D-Pad from button bits as hat axes
        left = bool(gp.wButtons & 0x0004)
        right = bool(gp.wButtons & 0x0008)
        up = bool(gp.wButtons & 0x0001)
        down = bool(gp.wButtons & 0x0002)
        axes[6] = -1.0 if left else (1.0 if right else 0.0)
        axes[7] = -1.0 if up else (1.0 if down else 0.0)
        # Buttons: map major bits (A,B,X,Y,LB,RB,Back,Start,LS,RS)
        bits = [
            0x1000,  # A
            0x2000,  # B
            0x4000,  # X
            0x8000,  # Y
            0x0100,  # LB
            0x0200,  # RB
            0x0020,  # Back
            0x0010,  # Start
            0x0040,  # LS
            0x0080,  # RS
        ]
        for i, bit in enumerate(bits):
            buttons[i] = 1 if (gp.wButtons & bit) else 0
        return axes, buttons


class JoystickWindow(ui_serial.MainWindow):
    """Top-level window with joystick enable/select and polling/mapping.

    - Adds a simple "Joystick" menu (Enable/Refresh/Select device).
    - Polls the backend at ``poll_hz`` and pushes state to:
        * Compact diagnostic labels (axes/buttons preview)
        * The ``JoystickView`` drawing
        * The channel controls (if a mapping is present)
    """

    def __init__(
        self,
        config: ui_serial.ChannelConfig,
        default_port: Optional[str] = None,
        default_baud: int = 115200,
        auto_connect: bool = False,
        *,
        axis_deadzone: float = 0.10,
        poll_hz: int = 50,
        invert_axes: Optional[List[int]] = None,
        joystick_mapping: Optional[Dict[str, Any]] = None,
        button_index_map: Optional[Dict[str, int]] = None,
        axis_index_map: Optional[Dict[str, int]] = None
    ) -> None:
        super().__init__(config, default_port, default_baud, auto_connect)

        self.joy = JoystickSupport()
        self.axis_deadzone = max(0.0, min(axis_deadzone, 0.9))
        self.poll_interval_ms = max(5, int(1000 / max(1, poll_hz)))
        self.invert_axes = set(invert_axes or [])

        self._joystick_enabled = False
        self._joystick_index: Optional[int] = None

        # Normalize optional mappings to empty dicts for safer typing
        self._btn_map: Dict[str, int] = dict(button_index_map) if button_index_map else {}
        self._axis_map: Dict[str, int] = dict(axis_index_map) if axis_index_map else {}

        self._build_joystick_menu()

        self.joy_timer = QtCore.QTimer(self)
        self.joy_timer.setInterval(self.poll_interval_ms)
        self.joy_timer.timeout.connect(self._poll_joystick)

        # Removed old inline monitor widgets; using a dedicated dock with a custom drawing.

        # Right-side dock with a realistic painted controller view
        self._joy_view = JoystickView(self)
        dock_container = QtWidgets.QWidget(self)
        dock_layout = QtWidgets.QVBoxLayout(dock_container)
        dock_layout.setContentsMargins(6, 6, 6, 6)
        dock_layout.setSpacing(6)
        dock_layout.addWidget(self._joy_view)
        # Also show compact axes/buttons text below the drawing
        self._axes_label = QtWidgets.QLabel("(0):", dock_container)
        self._axes_label.setStyleSheet("color:#1e88e5;font-family:Consolas,'Courier New',monospace;")
        self._buttons_label = QtWidgets.QLabel("(0):", dock_container)
        self._buttons_label.setStyleSheet("color:#43a047;font-family:Consolas,'Courier New',monospace;")
        dock_layout.addWidget(self._axes_label)
        dock_layout.addWidget(self._buttons_label)

        self._joy_dock = QtWidgets.QDockWidget("Controller", self)
        self._joy_dock.setWidget(dock_container)
        dock_container.setMinimumWidth(520)
        self._joy_dock.setMinimumWidth(520)
        self._joy_dock.setFeatures(QtWidgets.QDockWidget.DockWidgetMovable | QtWidgets.QDockWidget.DockWidgetFloatable)
        self.addDockWidget(QtCore.Qt.RightDockWidgetArea, self._joy_dock)

        # Ensure the bridge input/output log views show 10 rows for readability
        try:
            self._set_log_view_rows(self.input_log_view, 10)
            self._set_log_view_rows(self.output_log_view, 10)
        except Exception:
            # If base UI changes or elements are missing, avoid crashing
            pass

        # Make window wider and recompute height to fit 10-row logs
        try:
            cur = self.size()
            new_w = max(cur.width() + 360, 1000)
            # Recalculate size hint after resizing the log views
            self.adjustSize()
            size_hint = self.sizeHint()
            self.setFixedSize(new_w, size_hint.height())
        except Exception:
            pass

        # Load joystick mapping from config (if provided)
        self._axis_bindings: List[Dict[str, Any]] = []
        self._button_bindings: List[Dict[str, Any]] = []
        if joystick_mapping:
            self._load_mapping(joystick_mapping)

    def _load_mapping(self, mapping: Dict[str, Any]) -> None:
        axes = mapping.get("axes", [])
        if isinstance(axes, list):
            for entry in axes:
                val = entry.get("axis")
                if not isinstance(val, (int, float, str)):
                    continue
                try:
                    axis_index = int(val)
                except Exception:
                    continue
                key = entry.get("key")
                if not isinstance(key, str):
                    continue
                # Safely coerce deadzone to float
                _dz_val = entry.get("deadzone", self.axis_deadzone)
                try:
                    _dz = float(_dz_val)
                except Exception:
                    _dz = self.axis_deadzone
                bind = {
                    "axis": axis_index,
                    "key": key,
                    "invert": bool(entry.get("invert", False)),
                    "deadzone": _dz,
                }
                self._axis_bindings.append(bind)
        buttons = mapping.get("buttons", [])
        if isinstance(buttons, list):
            for entry in buttons:
                val = entry.get("button")
                if not isinstance(val, (int, float, str)):
                    continue
                try:
                    btn_index = int(val)
                except Exception:
                    continue
                key = entry.get("key")
                if not isinstance(key, str):
                    continue
                self._button_bindings.append({"button": btn_index, "key": key})

    # ----- UI: Menu -----
    def _build_joystick_menu(self) -> None:
        """Create a simple Joystick menu.

        Items:
        - Enable: toggles polling + mapping
        - Refresh Devices: queries backends and rebuilds the device submenu
        - Select Device: radio-like list of detected gamepads
        """
        menubar = self.menuBar()
        joy_menu = menubar.addMenu("Joystick")

        self.action_enable = QtWidgets.QAction("Enable", self, checkable=True)
        self.action_enable.setChecked(False)
        self.action_enable.triggered.connect(self._toggle_enable)
        joy_menu.addAction(self.action_enable)

        joy_menu.addSeparator()

        action_refresh = QtWidgets.QAction("Refresh Devices", self)
        action_refresh.triggered.connect(self._refresh_devices)
        joy_menu.addAction(action_refresh)

        self.devices_menu = joy_menu.addMenu("Select Device")
        self._populate_devices_menu()

        # Do not alter the main status bar; keep it for COM status only.

    def _populate_devices_menu(self) -> None:
        self.devices_menu.clear()
        names = self.joy.list_devices() if self.joy.available else []
        if not names:
            act = QtWidgets.QAction("No devices detected", self)
            act.setEnabled(False)
            self.devices_menu.addAction(act)
            return
        for entry in names:
            idx_text, _, name = entry.partition(":")
            try:
                idx = int(idx_text.strip())
            except ValueError:
                continue
            action = QtWidgets.QAction(entry, self)
            action.setCheckable(True)
            action.triggered.connect(lambda _=False, i=idx: self._select_device(i))
            self.devices_menu.addAction(action)

    @Slot()
    def _refresh_devices(self) -> None:
        self._populate_devices_menu()

    @Slot(bool)
    def _toggle_enable(self, enabled: bool) -> None:
        if enabled:
            if not self.joy.available:
                QtWidgets.QMessageBox.warning(
                    self,
                    "Joystick",
                    "No joystick drivers available. Install pygame or use Windows XInput.",
                )
                self.action_enable.setChecked(False)
                return
            if self._joystick_index is None:
                # Try to default to the first device
                names = self.joy.list_devices() if self.joy.available else []
                if names:
                    # Parse index from first entry like "100: XInput Controller 0" or "0: ..."
                    first = names[0]
                    idx_text, _, _ = first.partition(":")
                    try:
                        guessed_idx = int(idx_text.strip())
                        self._select_device(guessed_idx)
                    except Exception:
                        pass
            if self._joystick_index is None:
                QtWidgets.QMessageBox.warning(
                    self, "Joystick", "No joystick detected. Plug in a gamepad and refresh."
                )
                self.action_enable.setChecked(False)
                return
            self._enable_polling(True)
        else:
            self._enable_polling(False)

    def _enable_polling(self, on: bool) -> None:
        self._joystick_enabled = bool(on and self.joy.available and self._joystick_index is not None)
        if self._joystick_enabled:
            self.joy_timer.start()
        else:
            self.joy_timer.stop()

    def _select_device(self, index: int) -> None:
        name = self.joy.open(index)
        if name is None:
            QtWidgets.QMessageBox.critical(self, "Joystick", f"Failed to open device {index}.")
            self._joystick_index = None
            self.action_enable.setChecked(False)
            self._enable_polling(False)
            return
        self._joystick_index = index
        try:
            self._joystick_name = str(name)
        except Exception:
            self._joystick_name = ""
        # Update check marks
        for action in self.devices_menu.actions():
            action.setChecked(False)
            if action.text().startswith(f"{index}:"):
                action.setChecked(True)
        # Show basic device info
        try:
            if index >= 100:
                # XInput device
                axes_n = 8
                btn_n = 16
            else:
                j = self.joy._joystick
                axes_n = j.get_numaxes() if j else 0
                btn_n = j.get_numbuttons() if j else 0
        except Exception:
            axes_n = btn_n = 0
        # Do not post joystick details to status bar; leave COM status intact.

    # ----- Poll & Map -----
    def _poll_joystick(self) -> None:
        """Poll backend, update preview UI, and map inputs to controls.

        Flow:
        1) poll() -> raw axes/buttons
        2) derive a normalized controller model (face/shoulders/dpad/triggers)
        3) update the painted view + compact labels
        4) apply mappings to float/byte/flag controls
        5) trigger auto-send if enabled
        """
        if not self._joystick_enabled:
            return
        state = self.joy.poll()
        axes: List[float] = state.get("axes", [])
        buttons: List[int] = state.get("buttons", [])

        # No global left/right swap

        # Update monitor
        try:
            ax_preview = ", ".join(f"{v:+.2f}" for v in axes[:8])
            btn_preview = ", ".join(str(int(b)) for b in buttons[:16])
            self._axes_label.setText(f"({len(axes)}):\n{ax_preview}")
            self._buttons_label.setText(f"({len(buttons)}):\n{btn_preview}")
        except Exception:
            pass

        d_left = d_right = d_up = d_down = False
        face_top = face_bottom = face_left = face_right = False
        l1 = r1 = l3 = r3 = select = start = False
        l2_val = r2_val = 0.0
        lx = ly = rx = ry = 0.0

        if getattr(self.joy, "_xinput_id", None) is not None:
            # XInput path: stable Xbox-style layout. Map to PS terminology.
            if len(axes) >= 8:
                d_left = axes[6] < -0.5
                d_right = axes[6] > 0.5
                d_up = axes[7] < -0.5
                d_down = axes[7] > 0.5
                l2_val = max(0.0, min(1.0, axes[4]))
                r2_val = max(0.0, min(1.0, axes[5]))
                lx = float(axes[0]); ly = float(axes[1])
                rx = float(axes[2]); ry = float(axes[3])
            if len(buttons) >= 10:
                
                face_bottom = bool(buttons[1])
                face_right = bool(buttons[3])
                face_left = bool(buttons[4])
                face_top = bool(buttons[2])
                
                                
                l1 = bool(buttons[4])
                r1 = bool(buttons[5])
                select = bool(buttons[6])        # Back -> SELECT
                start = bool(buttons[7])      # Start -> START
                l3 = bool(buttons[8])
                r3 = bool(buttons[9])
        else:
            # pygame path: layout depends on driver. Prefer DS4/DS5 heuristics.
            name = getattr(self, "_joystick_name", "").lower()
            is_dual = ("wireless controller" in name) or ("dualsense" in name) or ("dualshock" in name)
            try:
                j = self.joy._joystick
                if j:
                    base_axes = j.get_numaxes()
                    hat_count = j.get_numhats()
                    if hat_count >= 1 and len(axes) >= base_axes + 2:
                        hx = axes[base_axes]
                        hy = axes[base_axes + 1]
                        d_left = hx < -0.5
                        d_right = hx > 0.5
                        d_up = hy > 0.5
                        d_down = hy < -0.5
                    else:
                        # Fallback: some drivers expose D-Pad as buttons (SDL order)
                        if len(buttons) >= 15:
                            # Swapped up/down mapping per request
                            d_up = d_up or bool(buttons[12]) or (len(buttons) > 13 and bool(buttons[13]))
                            d_down = d_down or bool(buttons[11]) or (len(buttons) > 12 and bool(buttons[12]))
                            d_left = d_left or bool(buttons[13]) or (len(buttons) > 14 and bool(buttons[14]))
                            d_right = d_right or bool(buttons[14]) or (len(buttons) > 15 and bool(buttons[15]))
                    # Common stick axes order (used only if no axis_index_map)
                    if not self._axis_map:
                        if len(axes) >= 2:
                            lx = float(axes[0]); ly = float(axes[1])
                        if len(axes) >= 4:
                            rx = float(axes[2]); ry = float(axes[3])
            except Exception:
                pass
            if is_dual and len(buttons) >= 12:
                # SDL/pygame common mapping for DualShock/DualSense:
                # 0: Cross, 1: Circle, 2: Square, 3: Triangle
                # Only swap 4 (top) and 2 (right) from the original DS order
                # Original DS (common SDL): bottom=0, right=1, left=2, top=3
                face_bottom = bool(buttons[0])  # Cross
                face_right = bool(buttons[3])   # Circle
                face_left = bool(buttons[2])    # Square
                face_top = bool(buttons[1])     # Triangle
                l1 = bool(buttons[4])
                r1 = bool(buttons[5])
                # Triggers: prefer axes 4 (L2) / 5 (R2) when present, else use buttons 6/7 (digital)
                try:
                    if len(axes) >= 6:
                        l2_raw = float(axes[4])
                        r2_raw = float(axes[5])
                        def _norm_trig(v: float) -> float:
                            # Handle either -1..+1 or 0..1 ranges
                            if v <= 0.0:
                                return max(0.0, min(1.0, (v + 1.0) / 2.0))
                            return max(0.0, min(1.0, v))
                        l2_val = _norm_trig(l2_raw)
                        r2_val = _norm_trig(r2_raw)
                    else:
                        l2_val = 1.0 if (len(buttons) > 6 and buttons[6]) else 0.0
                        r2_val = 1.0 if (len(buttons) > 7 and buttons[7]) else 0.0
                except Exception:
                    pass
                select = bool(buttons[8]) if len(buttons) > 8 else False
                start = bool(buttons[9]) if len(buttons) > 9 else False
                l3 = bool(buttons[10]) if len(buttons) > 10 else False
                r3 = bool(buttons[11]) if len(buttons) > 11 else False
            elif len(buttons) >= 12:
                # Generic fallback ordering sometimes seen via pygame.
                # Generic pygame fallback: start from left=0, bottom=1, right=2, top=3
                # then swap 4 (top) and 2 (right)
                face_left = bool(buttons[0])
                face_bottom = bool(buttons[1])
                face_right = bool(buttons[3])
                face_top = bool(buttons[2])
                l1 = bool(buttons[4])
                r1 = bool(buttons[5])
                # Triggers as digital fallback
                l2_val = 1.0 if (len(buttons) > 6 and buttons[6]) else 0.0
                r2_val = 1.0 if (len(buttons) > 7 and buttons[7]) else 0.0
                select = bool(buttons[8]) if len(buttons) > 8 else False
                start = bool(buttons[9]) if len(buttons) > 9 else False
                l3 = bool(buttons[10]) if len(buttons) > 10 else False
                r3 = bool(buttons[11]) if len(buttons) > 11 else False
        # Optional: override button rolee mapping using config-provided indices
        if self._btn_map and buttons:
            def _btn(name: str):
                val = self._btn_map.get(name)
                if val is None:
                    return None
                try:
                    idx = int(val)
                except Exception:
                    return None
                return bool(buttons[idx]) if 0 <= idx < len(buttons) else None
            def _btn_any(names: List[str]):
                for n in names:
                    v = _btn(n)
                    if v is not None:
                        return v
                return None
            # Face buttons: support shape names and numeric aliases
            _tmp = _btn_any(['cross','button1','button 1','1'])
            face_bottom = _tmp if _tmp is not None else face_bottom
            _tmp = _btn_any(['circle','button2','button 2','2'])
            face_right = _tmp if _tmp is not None else face_right
            _tmp = _btn_any(['square','button3','button 3','3'])
            face_left = _tmp if _tmp is not None else face_left
            _tmp = _btn_any(['triangle','button4','button 4','4'])
            face_top = _tmp if _tmp is not None else face_top
            # Shoulder buttons and center buttons
            _b = _btn('l1')
            l1 = _b if _b is not None else l1
            _b = _btn('r1')
            r1 = _b if _b is not None else r1
            _b = _btn('select')
            select = _b if _b is not None else select
            _b = _btn('start')
            start = _b if _b is not None else start
            _tmp = _btn_any(['l3'])
            l3 = _tmp if _tmp is not None else l3
            # Allow alias 'l4' for right stick press per request
            _tmp = _btn_any(['r3','l4'])
            r3 = _tmp if _tmp is not None else r3
            # Optional D-Pad button mapping from JSON (single-section)
            d_up        = (_btn('dpad_up')    if _btn('dpad_up')    is not None else d_up)
            d_down      = (_btn('dpad_down')  if _btn('dpad_down')  is not None else d_down)
            d_left      = (_btn('dpad_left')  if _btn('dpad_left')  is not None else d_left)
            d_right     = (_btn('dpad_right') if _btn('dpad_right') is not None else d_right)
            # Optional digital triggers from buttons if provided
            _l2b = _btn('l2')
            if _l2b is not None:
                l2_val = 1.0 if _l2b else 0.0
            _r2b = _btn('r2')
            if _r2b is not None:
                r2_val = 1.0 if _r2b else 0.0

        # Optional: override stick axes mapping using config-provided indices
        if self._axis_map and axes:
            try:
                def _ax(name: str, default_val: float) -> float:
                    val = self._axis_map.get(name)
                    if val is None:
                        return default_val
                    try:
                        idx = int(val)
                    except Exception:
                        return default_val

                    if 0 <= idx < len(axes):
                        return float(axes[idx])
                    return default_val
                lx = _ax('lx', lx)
                ly = _ax('ly', ly)
                rx = _ax('rx', rx)
                ry = _ax('ry', ry)
                # Triggers as axes if present
                l2_val = _ax('lt', l2_val)
                r2_val = _ax('rt', r2_val)
                # D-Pad from axes if present: left<0,right>0 ; up>0,down<0
                try:
                    dpx = self._axis_map.get('dpad_x')
                    dpy = self._axis_map.get('dpad_y')
                except Exception:
                    dpx = dpy = None
                if dpx is not None:
                    try:
                        idx = int(dpx)
                        if 0 <= idx < len(axes):
                            v = float(axes[idx])
                            d_left = v < -0.5
                            d_right = v > 0.5
                    except Exception:
                        pass
                if dpy is not None:
                    try:
                        idx = int(dpy)
                        if 0 <= idx < len(axes):
                            v = float(axes[idx])
                            d_up = v > 0.5
                            d_down = v < -0.5
                    except Exception:
                        pass
            except Exception:
                pass

        # Update the painted controller view on the right
        try:
            self._joy_view.update_state({
                "dpad": {"up": d_up, "down": d_down, "left": d_left, "right": d_right},
                "face": {"triangle": face_top, "circle": face_right, "cross": face_bottom, "square": face_left},
                "shoulder": {"l1": l1, "r1": r1, "l2": float(l2_val), "r2": float(r2_val)},
                "center": {"select": select, "start": start, "l3": l3, "r3": r3},
                "sticks": {"lx": lx, "ly": ly, "rx": rx, "ry": ry},
            })
        except Exception:
            pass

        # Map axes -> controls per mapping if provided, else sensible default order
        if axes and (getattr(self, "_axis_bindings", None) or self.float_controls):
            if getattr(self, "_axis_bindings", None):
                for bind in self._axis_bindings:
                    idx = bind.get("axis", -1)
                    if not (0 <= idx < len(axes)):
                        continue
                    key = bind.get("key")
                    if not isinstance(key, str):
                        continue
                    val = float(axes[idx])
                    if bind.get("invert"):
                        val = -val
                    _dz_val2 = bind.get("deadzone", self.axis_deadzone)
                    try:
                        dz = float(_dz_val2)
                    except Exception:
                        dz = self.axis_deadzone
                    # Determine target control (float or byte)
                    ctrl_f = self.float_controls.get(key)
                    ctrl_b = self.byte_controls.get(key)
                    if ctrl_f is None and ctrl_b is None:
                        continue
                    # Lookup bounds from config
                    field = next((f for f in (self.config.float_fields + self.config.byte_fields) if f.get("key") == key), None)
                    if not field:
                        continue
                    fmin = float(field["min"])  # type: ignore[index]
                    fmax = float(field["max"])  # type: ignore[index]
                    # Deadzone maps to midpoint
                    if abs(val) < dz:
                        mapped = (fmin + fmax) / 2.0
                    else:
                        t = (val + 1.0) / 2.0
                        mapped = fmin + (fmax - fmin) * t
                    if ctrl_f is not None:
                        mapped = round(float(mapped), 2)
                        if abs(ctrl_f.value() - mapped) > 1e-6:
                            ctrl_f.blockSignals(True)
                            ctrl_f.setValue(mapped)
                            ctrl_f.blockSignals(False)
                    elif ctrl_b is not None:
                        iv = int(round(mapped))
                        iv = max(ctrl_b.minimum(), min(ctrl_b.maximum(), iv))
                        if ctrl_b.value() != iv:
                            ctrl_b.blockSignals(True)
                            ctrl_b.setValue(iv)
                            ctrl_b.blockSignals(False)
            else:
                available_keys = [f["key"] for f in self.config.float_fields if f.get("available")]
                preferred = [k for k in ["target_angle", "max_output", "kp", "ki", "kd"] if k in self.float_controls]
                # Merge preferred (in order) then any remaining available keys
                used = []
                for k in preferred:
                    if k in available_keys and k not in used:
                        used.append(k)
                for k in available_keys:
                    if k not in used:
                        used.append(k)
                # Drive up to len(axes) keys
                for i, key in enumerate(used):
                    if i >= len(axes):
                        break
                    ctrl = self.float_controls.get(key)
                    if ctrl is None:
                        continue
                    axis_val = axes[i]
                    if i in self.invert_axes:
                        axis_val = -axis_val
                    # Get field limits from config
                    field = next((f for f in self.config.float_fields if f.get("key") == key), None)
                    if not field:
                        continue
                    fmin = float(field["min"])  # type: ignore[index]
                    fmax = float(field["max"])  # type: ignore[index]
                    # Deadzone to midpoint of field
                    if abs(axis_val) < self.axis_deadzone:
                        mapped = round((fmin + fmax) / 2.0, 2)
                    else:
                        t = (axis_val + 1.0) / 2.0
                        mapped = fmin + (fmax - fmin) * t
                    mapped = round(mapped, 2)
                    if abs(ctrl.value() - mapped) > 1e-6:
                        ctrl.blockSignals(True)
                        ctrl.setValue(mapped)
                        ctrl.blockSignals(False)

        # Map buttons -> flags by mapping if present, else default priority
        if buttons and self.flag_controls:
            if getattr(self, "_button_bindings", None):
                for bind in self._button_bindings:
                    idx = bind.get("button", -1)
                    if not (0 <= idx < len(buttons)):
                        continue
                    key = bind.get("key")
                    if not isinstance(key, str):
                        continue
                    checkbox = self.flag_controls.get(key)
                    if checkbox is None:
                        continue
                    pressed = bool(buttons[idx])
                    if checkbox.isChecked() != pressed:
                        checkbox.blockSignals(True)
                        checkbox.setChecked(pressed)
                        checkbox.blockSignals(False)
            else:
                preferred_flags = [k for k in ["enable_motors", "persist", "trigger_calibration"] if k in self.flag_controls]
                other_flags = [k for k in self.flag_controls.keys() if k not in preferred_flags]
                keys = preferred_flags + other_flags
                for i, key in enumerate(keys):
                    if i >= len(buttons):
                        break
                    checkbox = self.flag_controls.get(key)
                    if checkbox is None:
                        continue
                    pressed = bool(buttons[i])
                    if checkbox.isChecked() != pressed:
                        checkbox.blockSignals(True)
                        checkbox.setChecked(pressed)
                        checkbox.blockSignals(False)

        # Use existing auto-send behavior
        if self.manager.is_open():
            if self.auto_send_check.isChecked():
                self.schedule_auto_send(immediate=False)
            # Keep status bar reserved for COM connection; no controller preview here.



def main() -> None:
    # Build Qt application
    app = QtWidgets.QApplication(sys.argv)

    # Same default as ui_serial: ui_channels.json next to this script
    config_path = QtCore.QFileInfo(__file__).dir().filePath("ui_channels.json")

    # Load the channel configuration
    from pathlib import Path as _Path
    cfg_path = _Path(config_path)
    config = ui_serial.ChannelConfig.from_file(cfg_path)

    # Optional: build joystick mapping and index maps from JSON (UTF-8 with BOM supported)
    # Supported formats:
    # - Top-level "joystick": { "axes": [{axis,key,invert?,deadzone?}], "buttons": [{button,key}] }
    # - Or per-field entries under "bytes"/"floats"/"flags" with a "joystick" sub-object
    #   e.g. { "key": "target_angle", "joystick": { "axis": 0, "invert": true, "deadzone": 0.1 } }
    # - Optional single-section index helpers: "joystick_button_map": { "cross": 0, "axes": {"lx":0, ...} }
    joy_map: Optional[Dict[str, Any]] = None
    btn_index_map: Optional[Dict[str, int]] = None
    axis_index_map: Optional[Dict[str, int]] = None
    try:
        raw_cfg = json.loads(cfg_path.read_text(encoding="utf-8-sig"))
        jm = raw_cfg.get("joystick")
        if isinstance(jm, dict):
            joy_map = jm
        else:
            # Build mapping from per-field 'joystick' attributes on bytes/floats/flags.
            axes_list: List[Dict[str, Any]] = []
            buttons_list: List[Dict[str, Any]] = []
            for section in ("bytes", "floats"):
                for entry in raw_cfg.get(section, []) or []:
                    try:
                        key = entry.get("key")
                        js = entry.get("joystick")
                        if isinstance(key, str) and isinstance(js, dict) and ("axis" in js):
                            ax_val = js.get("axis")
                            if not isinstance(ax_val, (int, float, str)):
                                continue
                            bind: Dict[str, Any] = {
                                "axis": int(ax_val),
                                "key": key,
                            }
                            if "invert" in js:
                                bind["invert"] = bool(js.get("invert"))
                            if "deadzone" in js:
                                _dzj = js.get("deadzone")
                                try:
                                    bind["deadzone"] = float(_dzj)  # type: ignore[arg-type]
                                except Exception:
                                    pass
                            axes_list.append(bind)
                    except Exception:
                        pass
            for entry in raw_cfg.get("flags", []) or []:
                try:
                    key = entry.get("key")
                    js = entry.get("joystick")
                    if isinstance(key, str) and isinstance(js, dict) and ("button" in js):
                        b_val = js.get("button")
                        if not isinstance(b_val, (int, float, str)):
                            continue
                        buttons_list.append({"button": int(b_val), "key": key})
                except Exception:
                    pass
            if axes_list or buttons_list:
                joy_map = {"axes": axes_list, "buttons": buttons_list}

        # Single-section index maps for buttons and axes
        raw_btn = raw_cfg.get("joystick_button_map")
        if isinstance(raw_btn, dict):
            # Buttons by name (skip nested dicts like 'axes')
            tmp_btn: Dict[str, int] = {}
            for k, v in raw_btn.items():
                if k == "axes" or isinstance(v, dict):
                    continue
                if isinstance(v, (int, float, str)):
                    try:
                        tmp_btn[str(k)] = int(v)
                    except Exception:
                        pass
            if tmp_btn:
                btn_index_map = tmp_btn
            # Axes by name
            axes_obj = raw_btn.get("axes")
            if isinstance(axes_obj, dict):
                tmp_axes: Dict[str, int] = {}
                for k2, v2 in axes_obj.items():
                    if isinstance(v2, (int, float, str)):
                        try:
                            tmp_axes[str(k2)] = int(v2)
                        except Exception:
                            pass
                if tmp_axes:
                    axis_index_map = tmp_axes
    except Exception:
        pass

    window = JoystickWindow(
        config,
        default_port=None,
        default_baud=115200,
        auto_connect=False,
        axis_deadzone=0.10,
        poll_hz=50,
        invert_axes=[],
        joystick_mapping=joy_map,
        button_index_map=btn_index_map,
        axis_index_map=axis_index_map,
    )
    window.show()

    # Keep status bar for COM only; joystick is controlled from menu.
    app.aboutToQuit.connect(window.manager.close)
    sys.exit(app.exec())


if __name__ == "__main__":
    main()







