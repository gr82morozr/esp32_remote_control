#!/usr/bin/env python3
"""
ESP32 Keyboard Serial Bridge - Two Window Implementation

Process 1 (Input Window): Handles user input and serial communication
Process 2 (Output Window): Displays serial responses via multiprocessing Queue

Usage:
    python keyboard_serial.py

Requirements:
    pip install pyserial
"""

import os
import sys
import time
import ctypes
import struct
import serial
import serial.tools.list_ports
import argparse
from multiprocessing import Process, Queue, set_start_method
try:
    import win32gui
    import win32con
    WINDOWS_FOCUS_AVAILABLE = True
except ImportError:
    WINDOWS_FOCUS_AVAILABLE = False


# ---------- Windows console helpers ----------

def _open_own_console(title: str) -> None:
    """
    Allocate a new console window with the given title and bind Python's stdio to it.
    """
    k32 = ctypes.windll.kernel32

    # Detach from parent's console if any
    k32.FreeConsole()

    # Allocate a new console for this process
    if not k32.AllocConsole():
        pass

    # Set the window title
    try:
        k32.SetConsoleTitleW(title)
    except Exception:
        pass

    # Rebind Python's stdio to this console
    sys.stdin = open('CONIN$', 'r', encoding='utf-8', errors='ignore')
    sys.stdout = open('CONOUT$', 'w', encoding='utf-8', buffering=1, errors='ignore')
    sys.stderr = open('CONOUT$', 'w', encoding='utf-8', buffering=1, errors='ignore')


def _force_focus_console() -> bool:
    """
    Aggressively try to focus the current console window.
    Uses multiple Windows API calls for maximum effectiveness.
    """
    if not WINDOWS_FOCUS_AVAILABLE:
        return False
    
    try:
        # Get the current console window
        hwnd = win32gui.GetConsoleWindow()
        if not hwnd:
            return False
            
        # Multiple aggressive focus attempts
        try:
            # Method 1: Standard window activation
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
            win32gui.SetForegroundWindow(hwnd)
            win32gui.BringWindowToTop(hwnd)
            
            # Method 2: Force to foreground using AttachThreadInput trick
            import win32process
            current_thread = win32api.GetCurrentThreadId()
            fg_window = win32gui.GetForegroundWindow()
            if fg_window:
                fg_thread = win32process.GetWindowThreadProcessId(fg_window)[0]
                if fg_thread != current_thread:
                    win32process.AttachThreadInput(fg_thread, current_thread, True)
                    win32gui.SetForegroundWindow(hwnd)
                    win32gui.BringWindowToTop(hwnd)
                    win32process.AttachThreadInput(fg_thread, current_thread, False)
            
            # Method 3: Additional activation calls
            win32gui.SetActiveWindow(hwnd)
            win32gui.SetFocus(hwnd)
            
            # Method 4: Flash window to get attention then focus
            import win32con
            win32gui.FlashWindow(hwnd, True)
            time.sleep(0.1)
            win32gui.SetForegroundWindow(hwnd)
            
            return True
            
        except Exception as e:
            # Fallback: just try basic focus
            win32gui.SetForegroundWindow(hwnd)
            return True
            
    except Exception:
        pass
    
    return False


def _focus_window_by_title(title: str) -> bool:
    """
    Try to focus a window by its title (Windows only).
    Returns True if successful, False otherwise.
    """
    if not WINDOWS_FOCUS_AVAILABLE:
        return False
    
    try:
        def enum_windows_callback(hwnd, windows):
            if win32gui.IsWindowVisible(hwnd):
                window_title = win32gui.GetWindowText(hwnd)
                if title in window_title:
                    windows.append(hwnd)
            return True
        
        windows = []
        win32gui.EnumWindows(enum_windows_callback, windows)
        
        if windows:
            # Focus the first matching window
            hwnd = windows[0]
            win32gui.SetForegroundWindow(hwnd)
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
            win32gui.BringWindowToTop(hwnd)
            return True
    except Exception:
        pass
    
    return False


# RCPayload_t: id1, id2, id3, id4, value1-5 (floats), flags (25 bytes)
PAYLOAD_FORMAT = '<BBBB5fB'

# Key mappings
KEY_MAPPINGS = {
    # Letters a-z -> id1 = 1-26
    **{chr(ord('a') + i): i + 1 for i in range(26)},
    # Numbers 0-9 -> id1 = ASCII values (48-57)  
    **{str(i): ord(str(i)) for i in range(10)},
    # Arrow keys
    'up': 200, 'down': 201, 'left': 202, 'right': 203,
}


def list_ports():
    """List available COM ports"""
    ports = serial.tools.list_ports.comports()
    print("\nAvailable COM ports:")
    for i, port in enumerate(ports, 1):
        print(f"{i}. {port.device} - {port.description}")
    return [port.device for port in ports]


def select_port():
    """Select COM port"""
    ports = list_ports()
    if not ports:
        print("No COM ports found")
        return None
        
    while True:
        try:
            choice = input(f"\nSelect port (1-{len(ports)}) or 'q' to quit: ").strip()
            if choice.lower() in ['q', 'quit']:
                return None
            port_idx = int(choice) - 1
            if 0 <= port_idx < len(ports):
                return ports[port_idx]
            print("Invalid selection")
        except ValueError:
            print("Please enter a number")


def create_payload(command):
    """Create binary payload from command"""
    id1 = KEY_MAPPINGS.get(command, 0)
    
    # Set directional values for arrows
    value1 = value2 = 0.0
    if command == 'up': value1 = 100.0
    elif command == 'down': value1 = -100.0
    elif command == 'left': value2 = -100.0
    elif command == 'right': value2 = 100.0
    
    return struct.pack(PAYLOAD_FORMAT,
        id1, 0, 0, 0,  # id1-4
        value1, value2, 0.0, 0.0, 0.0,  # value1-5
        1  # flags
    )


def serial_reader_thread(serial_conn, response_queue, stop_event):
    """Background thread to continuously read serial data"""
    line_buffer = ""
    
    while not stop_event.is_set():
        try:
            if serial_conn.in_waiting > 0:
                # Read text data line by line
                data = serial_conn.read(serial_conn.in_waiting).decode('utf-8', errors='ignore')
                line_buffer += data
                
                # Process complete lines
                while '\n' in line_buffer:
                    line, line_buffer = line_buffer.split('\n', 1)
                    line = line.strip()
                    
                    # Filter for RC_DATA messages only
                    if line.startswith('RC_DATA:'):
                        try:
                            # Parse CSV data after RC_DATA: prefix
                            csv_data = line[8:]  # Remove "RC_DATA:" prefix
                            values = csv_data.split(',')
                            
                            if len(values) >= 10:
                                # Parse all RCPayload_t fields
                                id1 = int(values[0])
                                id2 = int(values[1]) 
                                id3 = int(values[2])
                                id4 = int(values[3])
                                value1 = float(values[4])
                                value2 = float(values[5])
                                value3 = float(values[6])
                                value4 = float(values[7])
                                value5 = float(values[8])
                                flags = int(values[9])
                                
                                # Create comprehensive message with all fields
                                message = (f"RX: ID[{id1:3},{id2:3},{id3:3},{id4:3}] "
                                          f"VAL[{value1:6.1f},{value2:6.1f},{value3:6.1f},{value4:6.1f},{value5:6.1f}] "
                                          f"FLAGS=0x{flags:02X}")
                                response_queue.put(message)
                        except (ValueError, IndexError):
                            pass  # Ignore malformed RC_DATA lines
                    
                    # Also show RC_SENT messages for debugging
                    elif line.startswith('RC_SENT:'):
                        try:
                            csv_data = line[8:]  # Remove "RC_SENT:" prefix
                            values = csv_data.split(',')
                            
                            if len(values) >= 10:
                                # Parse all RCPayload_t fields
                                id1 = int(values[0])
                                id2 = int(values[1])
                                id3 = int(values[2]) 
                                id4 = int(values[3])
                                value1 = float(values[4])
                                value2 = float(values[5])
                                value3 = float(values[6])
                                value4 = float(values[7])
                                value5 = float(values[8])
                                flags = int(values[9])
                                
                                # Create comprehensive message with all fields
                                message = (f"TX: ID[{id1:3},{id2:3},{id3:3},{id4:3}] "
                                          f"VAL[{value1:6.1f},{value2:6.1f},{value3:6.1f},{value4:6.1f},{value5:6.1f}] "
                                          f"FLAGS=0x{flags:02X}")
                                response_queue.put(message)
                        except (ValueError, IndexError):
                            pass
                            
            time.sleep(0.01)
        except Exception:
            break



def output_window_process(response_queue: Queue) -> None:
    """
    Output Window: Display serial responses from the queue.
    """
    _open_own_console("ESP32 Serial Response Display")

    print("=== ESP32 Serial Response Display - OUTPUT WINDOW ===")
    print("This window displays responses received from the ESP32 device.")
    print("Messages from the main window will appear below.")
    print("=" * 55)
    print("Waiting for serial responses...\n")

    try:
        while True:
            message = response_queue.get()  # Blocks until something arrives
            if message is None:  # Sentinel: stop
                print("\n[INFO] Main window closed. Exiting output window.")
                break
            
            # Display the message with timestamp
            timestamp = time.strftime("%H:%M:%S")
            print(f"[{timestamp}] {message}")
            
    except KeyboardInterrupt:
        print("\nInterrupted. Exiting output window.")
    except Exception as e:
        print(f"Output window error: {e}")

    print("Output window closing...")
    time.sleep(1)  # Brief delay so user can see the message


def main():
    """Entry point - uses main window for input and spawns one output window"""
    
    # Check for pyserial
    try:
        import serial
        import threading
    except ImportError:
        print("Error: Install pyserial with: pip install pyserial")
        return

    # On Windows, use 'spawn' to avoid forking console state
    try:
        set_start_method("spawn")
    except RuntimeError:
        # Start method may already be set
        pass

    # Create a multiprocessing Queue for communication
    response_queue = Queue()

    print("ESP32 Keyboard Serial Bridge - Main Window + Output Window")
    print("=" * 58)
    print("- Main Window: Input & Serial Control (this window)")
    print("- Output Window: Serial Response Display (separate window)")
    print("=" * 58)

    # Get current window title for focus restoration
    main_window_title = "ESP32 Keyboard Serial Bridge - Main Window + Output Window"
    
    # Set main window title
    if os.name == 'nt':  # Windows
        try:
            ctypes.windll.kernel32.SetConsoleTitleW(main_window_title)
        except Exception:
            pass
    
    # Spawn only the output process
    output_process = Process(target=output_window_process, args=(response_queue,), daemon=False)
    output_process.start()
    
    print("Output window started.")
    print("- Type commands in this window")
    print("- View responses in the separate Output window")
    print("- Type ':quit' to stop both windows")
    print()
    
    # Give output window time to initialize, then restore focus to main window
    time.sleep(1.0)  # Wait for window creation
    
    # Aggressively try to restore focus to main window
    focus_restored = False
    if WINDOWS_FOCUS_AVAILABLE:
        print("Attempting to restore focus to main window...")
        
        # Try aggressive focus restoration
        if _force_focus_console():
            focus_restored = True
            print("✓ Focus restored to main window - ready for input!")
        else:
            # Final desperate attempt: simulate clicking on console
            try:
                hwnd = win32gui.GetConsoleWindow()
                if hwnd:
                    # Get window rectangle and click center
                    rect = win32gui.GetWindowRect(hwnd)
                    center_x = (rect[0] + rect[2]) // 2
                    center_y = (rect[1] + rect[3]) // 2
                    
                    # Simulate mouse click to activate window
                    import win32api
                    win32api.SetCursorPos((center_x, center_y))
                    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
                    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
                    
                    time.sleep(0.2)
                    win32gui.SetForegroundWindow(hwnd)
                    focus_restored = True
                    print("✓ Focus restored via mouse click - ready for input!")
            except:
                pass
    
    if not focus_restored:
        print("⚠ FOCUS FAILED: Please click this window to focus it for input")
        print("   (The output window may have stolen focus)")
    print()

    # Select and connect to serial port in main window
    port = select_port()
    if not port:
        print("No port selected. Closing...")
        response_queue.put(None)  # Signal output window to stop
        output_process.join(timeout=5)
        return

    try:
        serial_conn = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)  # Wait for ESP32 reset
        print(f"Connected to {port}")
    except Exception as e:
        print(f"Connection failed: {e}")
        response_queue.put(None)  # Signal output window to stop
        output_process.join(timeout=5)
        return

    # Start background serial reader thread
    stop_event = threading.Event()
    reader_thread = threading.Thread(
        target=serial_reader_thread,
        args=(serial_conn, response_queue, stop_event),
        daemon=True
    )
    reader_thread.start()

    try:
        print(f"\nReady! Type commands and press Enter:")
        while True:
            try:
                # Show a simple prompt and wait for input
                command = input("CMD> ").strip().lower()
                
                if command == ":quit":
                    print("Sending quit signal...")
                    response_queue.put(None)
                    break
                elif command in KEY_MAPPINGS:
                    try:
                        # Send as text instead of binary
                        id1 = KEY_MAPPINGS.get(command, 0)
                        
                        # Set all ID fields for better command identification
                        id2 = 0xAA  # Magic byte for identification
                        id3 = 0xBB  # Magic byte for identification  
                        id4 = 0xFF  # Command type flag
                        
                        # Set directional values for arrows
                        value1 = value2 = value3 = value4 = value5 = 0.0
                        if command == 'up': 
                            value1 = 100.0
                            value3 = 1.0  # Direction indicator
                        elif command == 'down': 
                            value1 = -100.0
                            value3 = 2.0  # Direction indicator
                        elif command == 'left': 
                            value2 = -100.0
                            value3 = 3.0  # Direction indicator
                        elif command == 'right': 
                            value2 = 100.0
                            value3 = 4.0  # Direction indicator
                        
                        # Add timestamp and command validation
                        value4 = time.time() % 1000  # Timestamp in ms
                        value5 = len(command)        # Command length for validation
                        flags = 0x01  # Command valid flag
                        
                        # Send as CSV text format with all fields populated
                        text_data = f"{id1},{id2},{id3},{id4},{value1:.2f},{value2:.2f},{value3:.2f},{value4:.2f},{value5:.2f},{flags}\n"
                        serial_conn.write(text_data.encode('utf-8'))
                        print(f"Sent: {command} -> ID[{id1},{id2},{id3},{id4}] VAL[{value1:.1f},{value2:.1f},{value3:.1f},{value4:.1f},{value5:.1f}] FLAGS=0x{flags:02X}")
                    except Exception as e:
                        print(f"Send failed: {e}")
                elif command:
                    print(f"Unknown command: {command}")
                    
            except EOFError:
                print("Input ended.")
                response_queue.put(None)
                break
            except KeyboardInterrupt:
                print("Interrupted.")
                response_queue.put(None)
                break

    except KeyboardInterrupt:
        print("\nInterrupted. Exiting main window.")
        response_queue.put(None)
    finally:
        # Stop the serial reader thread
        stop_event.set()
        if serial_conn and serial_conn.is_open:
            serial_conn.close()
        print("Serial connection closed.")

    # Immediately terminate output process - no need to wait
    try:
        if output_process.is_alive():
            output_process.terminate()
            output_process.join(timeout=1)  # Brief wait for cleanup
    except Exception:
        pass
    
    print("Program finished.")


if __name__ == "__main__":
    main()