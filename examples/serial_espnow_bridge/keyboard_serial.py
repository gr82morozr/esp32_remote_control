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
    buffer = b""
    payload_size = struct.calcsize(PAYLOAD_FORMAT)
    
    while not stop_event.is_set():
        try:
            if serial_conn.in_waiting > 0:
                data = serial_conn.read(serial_conn.in_waiting)
                buffer += data
                
                # Process complete payloads
                while len(buffer) >= payload_size:
                    payload_data = buffer[:payload_size]
                    buffer = buffer[payload_size:]
                    
                    try:
                        values = struct.unpack(PAYLOAD_FORMAT, payload_data)
                        message = f"RX: ID1={values[0]:3} V1={values[4]:6.1f} V2={values[5]:6.1f}"
                        response_queue.put(message)
                    except struct.error:
                        pass  # Ignore malformed data
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

    # Spawn only the output process
    output_process = Process(target=output_window_process, args=(response_queue,), daemon=False)
    output_process.start()
    
    print("Output window started.")
    print("- Type commands in this window")
    print("- View responses in the separate Output window")
    print("- Type ':quit' to stop both windows")
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
                        payload = create_payload(command)
                        serial_conn.write(payload)
                        print(f"Sent: {command}")
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

    # Wait for output process to finish
    print("Waiting for output window to close...")
    try:
        output_process.join(timeout=5)
        if output_process.is_alive():
            print("Terminating output window...")
            output_process.terminate()
            output_process.join(timeout=2)
    except Exception:
        pass
    
    print("Program finished.")


if __name__ == "__main__":
    main()