#!/usr/bin/env python3
"""
ESP32 Self-Balancing Robot Controller
=====================================

This script provides a Python-based remote controller for an ESP32 self-balancing robot.
It communicates with the robot using ESP-NOW protocol through a serial connection to an ESP32.

PURPOSE:
--------
- Remote control of self-balancing robot via keyboard input
- Real-time command transmission using ESP-NOW wireless protocol
- Compatible with ESP32 remote control library message format

HARDWARE SETUP:
--------------
1. ESP32 robot with self-balancing code and ESP-NOW receiver
2. ESP32 controller/bridge connected to PC via USB (acts as ESP-NOW transmitter)
3. Both ESP32s must be on same WiFi channel for ESP-NOW communication

SOFTWARE REQUIREMENTS:
---------------------
- Python 3.7+
- pyserial library: pip install pyserial
- ESP32 controller must run PC Serial Bridge firmware (pc_serial_bridge.cpp)

USAGE:
------
1. Upload pc_serial_bridge.cpp to ESP32 controller
2. Connect ESP32 controller to USB port
3. Run: python robot_controller.py --port COM3
4. Use keyboard controls:
   - UP ARROW    : Move Forward
   - DOWN ARROW  : Move Backward  
   - LEFT ARROW  : Turn Left
   - RIGHT ARROW : Turn Right
   - SPACE BAR   : Stop/Stand Still
   - ESC         : Exit Program

COMMAND-LINE OPTIONS:
--------------------
--port, -p    : Serial port (e.g., COM3, /dev/ttyUSB0)
--baud, -b    : Baud rate (default: 115200)
--protocol    : Wireless protocol (espnow, nrf24) (default: espnow)
--help, -h    : Show help message

MESSAGE PROTOCOL:
----------------
Uses JSON commands with ESP32 PC Serial Bridge:
- {"cmd":"data", "id1":170, "id2":187, "v1":1.0, "v2":50.0, "v3":30.0, "flags":1}
- Command encoding: v1=movement type, v2=speed, v3=turn rate
- Bridge forwards commands via ESP-NOW/NRF24 to robot

TROUBLESHOOTING:
---------------
- Ensure correct COM port in script (default: COM3)
- Check ESP32 controller is running ESP-NOW bridge code
- Verify robot and controller are paired for ESP-NOW
- Windows: May need to adjust COM port permissions
- Linux: May need sudo access or add user to dialout group

AUTHORS: Generated for ESP32 self-balancing robot project
LICENSE: Use freely for educational and personal projects
"""

import serial
import serial.tools.list_ports
import json
import time
import sys
import argparse
from enum import IntEnum

# Try to import keyboard input libraries
try:
    import msvcrt  # Windows
    HAS_MSVCRT = True
except ImportError:
    HAS_MSVCRT = False

try:
    import tty, termios  # Unix/Linux
    HAS_TERMIOS = True
except ImportError:
    HAS_TERMIOS = False

# Check if we have any keyboard input support
if not HAS_MSVCRT and not HAS_TERMIOS:
    print("Warning: No keyboard input library available")

# ANSI Color codes for command output
class Colors:
    """ANSI color codes for terminal output"""
    RESET = '\033[0m'      # Reset to default
    BOLD = '\033[1m'       # Bold text
    
    # Command colors
    FORWARD = '\033[92m'   # Bright Green
    BACKWARD = '\033[93m'  # Bright Yellow  
    LEFT = '\033[94m'      # Bright Blue
    RIGHT = '\033[95m'     # Bright Magenta
    STOP = '\033[91m'      # Bright Red
    EXIT = '\033[96m'      # Bright Cyan
    
    # Status colors
    SUCCESS = '\033[92m'   # Bright Green
    ERROR = '\033[91m'     # Bright Red
    TIMESTAMP = '\033[90m' # Dark Gray

def enable_ansi_colors():
    """Enable ANSI color support in Windows terminal"""
    if sys.platform == "win32":
        try:
            import os
            # Enable ANSI escape sequence processing on Windows
            os.system('color')
            # Alternative method using Windows API
            import ctypes
            kernel32 = ctypes.windll.kernel32
            kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
        except:
            pass  # Ignore errors if color support can't be enabled

class RobotCommand(IntEnum):
    """
    Robot movement commands enumeration
    
    These values are sent as float in the payload.value1 field
    to indicate the desired robot movement direction.
    """
    STOP = 0        # Robot stops and maintains balance
    FORWARD = 1     # Robot moves forward while balancing
    BACKWARD = 2    # Robot moves backward while balancing
    TURN_LEFT = 3   # Robot turns left (pivots or differential steering)
    TURN_RIGHT = 4  # Robot turns right (pivots or differential steering)

class RCPayload:
    """
    ESP32 Remote Control Payload for JSON Bridge Communication
    
    This class creates JSON commands compatible with the ESP32 PC Serial Bridge
    that uses the esp32_remote_control library. The bridge accepts JSON commands
    and converts them to binary RCPayload_t structures for wireless transmission.
    
    JSON Format: {"cmd":"data", "id1":int, "id2":int, "v1":float, "v2":float, "flags":int}
    """
    def __init__(self):
        # Command identification bytes (0-255)
        self.id1 = 0xAA     # Primary command identifier (magic byte)
        self.id2 = 0xBB     # Protocol version identifier
        self.id3 = 0        # Reserved for future use
        self.id4 = 0        # Reserved for future use
        
        # Command parameters (float values)
        self.value1 = 0.0   # Movement command type (see RobotCommand enum)
        self.value2 = 0.0   # Speed/intensity (0-100 typical range)
        self.value3 = 0.0   # Turn rate in degrees per second
        self.value4 = 0.0   # Reserved for additional parameters
        self.value5 = 0.0   # Reserved for additional parameters
        
        # Control flags (0-255)
        self.flags = 0      # Bit flags for command options
    
    def to_json_command(self):
        """
        Convert payload to JSON command for PC Serial Bridge
        
        Returns:
            dict: JSON command dictionary ready for serial transmission
            
        The ESP32 PC Serial Bridge expects JSON in this format:
        {"cmd":"data", "id1":170, "id2":187, "v1":1.0, "v2":50.0, "flags":1}
        """
        return {
            "cmd": "data",
            "id1": int(self.id1),
            "id2": int(self.id2),
            "id3": int(self.id3),
            "id4": int(self.id4),
            "v1": float(self.value1),
            "v2": float(self.value2),
            "v3": float(self.value3),
            "v4": float(self.value4),
            "v5": float(self.value5),
            "flags": int(self.flags)
        }

class RobotController:
    """
    ESP32 Robot Controller via PC Serial Bridge
    
    This class manages communication with an ESP32 self-balancing robot through
    the ESP32 PC Serial Bridge (pc_serial_bridge.cpp). The bridge accepts JSON
    commands via serial and forwards them to the robot using ESP-NOW or NRF24.
    
    The controller sends movement commands in real-time based on keyboard input
    and maintains connection with the bridge using JSON protocol.
    """
    
    def __init__(self, port, baudrate=115200, protocol='espnow'):
        """
        Initialize robot controller
        
        Args:
            port (str): Serial port for ESP32 bridge connection (e.g., COM3, /dev/ttyUSB0)
            baudrate (int): Serial communication speed (default: 115200)
            protocol (str): Wireless protocol to use ('espnow' or 'nrf24')
        """
        self.port = port                            # Serial port device name
        self.baudrate = baudrate                    # Serial communication baud rate
        self.protocol = protocol                    # Wireless protocol (espnow/nrf24)
        self.serial = None                          # Serial connection object
        self.running = False                        # Control loop state flag
        self.current_command = RobotCommand.STOP    # Currently active movement command
        self.bridge_ready = False                   # Bridge initialization status
        
    def connect(self):
        """
        Connect to ESP32 PC Serial Bridge and initialize protocol
        
        Returns:
            bool: True if connected and bridge is ready, False if serial connection failed
        """
        try:
            # Establish serial connection
            self.serial = serial.Serial(self.port, self.baudrate, timeout=2)
            time.sleep(2)  # Wait for ESP32 boot/reset
            print(f"Connected to ESP32 bridge on {self.port} at {self.baudrate} baud")
            
            # Clear any startup messages
            self._read_responses(timeout=1)
            
            # Try to set protocol if not default
            if self.protocol != 'espnow':
                if self._switch_protocol(self.protocol):
                    print(f"Successfully switched to {self.protocol} protocol")
                else:
                    print(f"Could not switch to {self.protocol} protocol (bridge may not be running)")
            
            # Check bridge status
            if self._check_bridge_status():
                self.bridge_ready = True
                print(f"Bridge ready, using {self.protocol} protocol")
                return True
            else:
                print("Bridge status check failed (may not be running PC Serial Bridge firmware)")
                # Keep connection open but mark bridge as not ready
                self.bridge_ready = False
                return False  # Bridge not ready, but serial connection is open
                
        except Exception as e:
            print(f"Failed to establish serial connection: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from ESP32 bridge"""
        if self.serial and self.serial.is_open:
            self.serial.close()
            print("Disconnected from bridge")
            self.bridge_ready = False
    
    def send_command(self, command, speed=50.0, turn_rate=30.0):
        """
        Send movement command to robot via PC Serial Bridge JSON protocol
        
        Args:
            command (RobotCommand): Movement command (STOP, FORWARD, etc.)
            speed (float): Movement speed percentage (0-100)
            turn_rate (float): Turning rate in degrees per second (0-90 typical)
            
        Returns:
            bool: True if command sent successfully, False otherwise
            
        The command is converted to JSON format and sent to the ESP32 PC Serial
        Bridge, which converts it to binary and forwards via ESP-NOW/NRF24.
        """
        if not self.serial or not self.serial.is_open:
            return False
        
        # Create payload with command parameters
        payload = RCPayload()
        payload.id1 = 0xAA              # Primary command identifier (magic byte)
        payload.id2 = 0xBB              # Secondary identifier (protocol version)
        payload.value1 = float(command) # Movement command type (see RobotCommand)
        payload.value2 = speed          # Speed percentage (0.0-100.0)
        payload.value3 = turn_rate      # Turn rate in degrees/second
        payload.flags = 0x01            # Command valid flag (bit 0 = valid)
        
        try:
            # Convert to JSON command for bridge
            json_cmd = payload.to_json_command()
            json_str = json.dumps(json_cmd) + '\n'
            
            # Send JSON command to bridge
            self.serial.write(json_str.encode('utf-8'))
            self.serial.flush()
            return True
        except Exception as e:
            print(f"Send error: {e}")
            return False
    
    def _send_json_command(self, command_dict):
        """
        Send JSON command to bridge and return response
        
        Args:
            command_dict (dict): JSON command dictionary
            
        Returns:
            dict: Response from bridge, or None if error
        """
        if not self.serial or not self.serial.is_open:
            return None
            
        try:
            json_str = json.dumps(command_dict) + '\n'
            self.serial.write(json_str.encode('utf-8'))
            self.serial.flush()
            
            # Read response (bridge responds with JSON)
            return self._read_json_response(timeout=1)
        except Exception as e:
            print(f"JSON command error: {e}")
            return None
    
    def _read_json_response(self, timeout=1):
        """
        Read JSON response from bridge
        
        Args:
            timeout (float): Read timeout in seconds
            
        Returns:
            dict: Parsed JSON response, or None if timeout/error
        """
        if not self.serial:
            return None
            
        start_time = time.time()
        line_buffer = ""
        
        while (time.time() - start_time) < timeout:
            if self.serial.in_waiting > 0:
                try:
                    char = self.serial.read(1).decode('utf-8')
                    if char == '\n' or char == '\r':
                        if line_buffer.strip():
                            try:
                                return json.loads(line_buffer.strip())
                            except json.JSONDecodeError:
                                # Not JSON, ignore this line
                                pass
                            line_buffer = ""
                    else:
                        line_buffer += char
                except:
                    pass
            time.sleep(0.01)
        return None
    
    def _read_responses(self, timeout=1):
        """Read and discard any pending responses from bridge"""
        if not self.serial:
            return
            
        start_time = time.time()
        while (time.time() - start_time) < timeout:
            if self.serial.in_waiting > 0:
                try:
                    self.serial.read(self.serial.in_waiting)
                except:
                    pass
            time.sleep(0.01)
    
    def _switch_protocol(self, protocol):
        """
        Switch bridge to specified wireless protocol
        
        Args:
            protocol (str): Protocol name ('espnow' or 'nrf24')
            
        Returns:
            bool: True if switch successful
        """
        cmd = {"cmd": "switch", "protocol": protocol}
        response = self._send_json_command(cmd)
        
        if response and response.get("status") == "protocol_switched":
            return True
        elif response and "error" in response:
            print(f"Protocol switch error: {response.get('error')}")
        return False
    
    def _check_bridge_status(self):
        """
        Check if bridge is responding and ready
        
        Returns:
            bool: True if bridge is ready
        """
        cmd = {"cmd": "status"}
        response = self._send_json_command(cmd)
        
        if response and "status" in response:
            return True
        return False
    
    def _check_incoming_messages(self):
        """
        Check for and display incoming messages from bridge
        """
        if not self.serial or not self.serial.is_open:
            return
            
        while self.serial.in_waiting > 0:
            try:
                line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    # Try to parse as JSON for structured messages
                    try:
                        msg = json.loads(line)
                        if 'event' in msg:
                            # Show incoming events (data received from robot)
                            if msg.get('event') == 'data_received':
                                print(f"\n[ROBOT] Data received: v1={msg.get('v1')}, v2={msg.get('v2')}")
                            else:
                                print(f"\n[BRIDGE] {msg.get('event')}: {msg}")
                        elif 'status' in msg:
                            # Filter out repetitive "data_sent" status messages and other routine status
                            status = msg.get('status')
                            if status not in ['data_sent', 'protocol_switched']:
                                print(f"\n[BRIDGE] Status: {msg}")
                        elif 'error' in msg:
                            print(f"\n[BRIDGE] Error: {msg.get('error')}")
                        elif 'bridge' in msg:
                            print(f"\n[BRIDGE] {msg}")
                        # Ignore other routine messages
                    except json.JSONDecodeError:
                        # Not JSON, just display important text messages
                        if ('bridge' in line.lower() or 'error' in line.lower() or 
                            'connected' in line.lower() or 'ready' in line.lower()):
                            print(f"\n[BRIDGE] {line}")
            except Exception:
                pass  # Ignore read errors
    
    def _read_immediate_response(self, timeout=0.1):
        """
        Read immediate response from bridge after sending command
        
        Args:
            timeout (float): Time to wait for response in seconds
            
        Returns:
            str: Response message from bridge, or None if no response
        """
        if not self.serial or not self.serial.is_open:
            return None
            
        start_time = time.time()
        responses = []
        
        while (time.time() - start_time) < timeout:
            if self.serial.in_waiting > 0:
                try:
                    line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        # Try to parse as JSON
                        try:
                            msg = json.loads(line)
                            # Look for meaningful responses
                            if 'status' in msg:
                                status = msg.get('status')
                                if status == 'data_sent':
                                    protocol = msg.get('protocol', 'unknown')
                                    timestamp = msg.get('timestamp', '')
                                    return f"Sent via {protocol} (t:{timestamp})"
                                elif status == 'protocol_switched':
                                    return f"Protocol: {msg.get('protocol', 'unknown')}"
                                else:
                                    return f"Status: {status}"
                            elif 'error' in msg:
                                return f"Error: {msg.get('error')}"
                            elif 'bridge' in msg:
                                return f"Bridge: {msg}"
                        except json.JSONDecodeError:
                            # Not JSON, check for important text
                            if ('error' in line.lower() or 'connected' in line.lower() or 
                                'ready' in line.lower() or 'bridge' in line.lower()):
                                responses.append(line[:50])  # Truncate long messages
                except:
                    pass
            time.sleep(0.01)
        
        # Return first meaningful response
        return responses[0] if responses else None
    
    def get_key_input(self):
        """Get keyboard input (cross-platform)"""
        if HAS_MSVCRT:  # Windows
            if msvcrt.kbhit():
                key = msvcrt.getch()
                if key == b'\xe0':  # Arrow key prefix
                    key = msvcrt.getch()
                    if key == b'H': return 'up'
                    elif key == b'P': return 'down'
                    elif key == b'K': return 'left'
                    elif key == b'M': return 'right'
                elif key == b' ': return 'space'
                elif key == b'\x1b': return 'esc'
        elif HAS_TERMIOS:  # Unix/Linux
            key = sys.stdin.read(1)
            if key == '\x1b':  # ESC sequence
                key += sys.stdin.read(2)
                if key == '\x1b[A': return 'up'
                elif key == '\x1b[B': return 'down'
                elif key == '\x1b[D': return 'left'
                elif key == '\x1b[C': return 'right'
                elif key == '\x1b': return 'esc'
            elif key == ' ': return 'space'
        
        return None
    
    def control_loop(self):
        """Main control loop with keyboard input"""
        print("\n=== ESP32 Robot Controller ===")
        print("Controls:")
        print("  UP ARROW  - Move Forward")
        print("  DOWN ARROW- Move Backward") 
        print("  LEFT ARROW- Turn Left")
        print("  RIGHT ARROW-Turn Right")
        print("  SPACE     - Stop")
        print("  ESC       - Exit")
        
        if self.bridge_ready:
            print("\n[OK] Bridge is ready and responding")
        else:
            print("\n[WARNING] Bridge may not be running PC Serial Bridge firmware")
            print("  Commands will still be sent, but may not reach robot")
            
        print("\nPress keys to control robot...")
        print("(Commands and bridge responses will be displayed)\n")
        
        if HAS_TERMIOS:
            # Set terminal to raw mode for immediate key response
            old_settings = termios.tcgetattr(sys.stdin)
            tty.setraw(sys.stdin.fileno())
        
        self.running = True
        last_command_time = 0
        
        try:
            while self.running:
                key = self.get_key_input()
                
                if key:
                    new_command = self.current_command
                    command_sent = False
                    
                    # Get timestamp for command
                    now = time.time()
                    timestamp = time.strftime("%H:%M:%S", time.localtime(now))
                    millis = int((now % 1) * 1000)
                    timestamp = f"{Colors.TIMESTAMP}[{timestamp}.{millis:03d}]{Colors.RESET}"
                    
                    if key == 'up':
                        new_command = RobotCommand.FORWARD
                        print(f"{timestamp} -> {Colors.FORWARD}FORWARD{Colors.RESET}", end='', flush=True)
                    elif key == 'down':
                        new_command = RobotCommand.BACKWARD
                        print(f"{timestamp} -> {Colors.BACKWARD}BACKWARD{Colors.RESET}", end='', flush=True)
                    elif key == 'left':
                        new_command = RobotCommand.TURN_LEFT
                        print(f"{timestamp} -> {Colors.LEFT}TURN LEFT{Colors.RESET}", end='', flush=True)
                    elif key == 'right':
                        new_command = RobotCommand.TURN_RIGHT
                        print(f"{timestamp} -> {Colors.RIGHT}TURN RIGHT{Colors.RESET}", end='', flush=True)
                    elif key == 'space':
                        new_command = RobotCommand.STOP
                        print(f"{timestamp} -> {Colors.STOP}STOP{Colors.RESET}", end='', flush=True)
                    elif key == 'esc':
                        print(f"{timestamp} -> {Colors.EXIT}EXITING...{Colors.RESET}")
                        break
                    
                    # Always send command on keypress (even if same command)
                    if key in ['up', 'down', 'left', 'right', 'space']:
                        self.current_command = new_command
                        if self.send_command(self.current_command):
                            print(f" {Colors.SUCCESS}OK{Colors.RESET}", end='')
                            command_sent = True
                            
                            # Wait briefly and show bridge response
                            time.sleep(0.05)  # Small delay to let bridge respond
                            bridge_response = self._read_immediate_response()
                            if bridge_response:
                                print(f" -> {Colors.TIMESTAMP}{bridge_response}{Colors.RESET}")
                            else:
                                print()  # New line if no response
                        else:
                            print(f" {Colors.ERROR}FAIL (send failed){Colors.RESET}")
                        last_command_time = time.time()
                    
                    # Move to new line after each command
                    if command_sent or key == 'esc':
                        if not command_sent:  # Only print newline if we haven't already
                            print()  # New line
                
                # Check for incoming messages from bridge
                self._check_incoming_messages()
                
                # Send periodic heartbeat (every 100ms)
                if time.time() - last_command_time > 0.1:
                    self.send_command(self.current_command)
                    last_command_time = time.time()
                
                time.sleep(0.01)  # Small delay
                
        finally:
            if HAS_TERMIOS:
                # Restore terminal settings
                termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
            
            # Send final stop command
            self.send_command(RobotCommand.STOP)
            self.running = False

def list_serial_ports():
    """
    List all available serial ports on the system
    
    Returns:
        list: List of available port names and descriptions
    """
    ports = serial.tools.list_ports.comports()
    available_ports = []
    
    print("Available Serial Ports:")
    print("=" * 50)
    
    if not ports:
        print("No serial ports found.")
        return []
    
    for i, port in enumerate(ports, 1):
        port_info = {
            'device': port.device,
            'description': port.description,
            'hwid': port.hwid
        }
        available_ports.append(port_info)
        
        print(f"{i}. {port.device}")
        print(f"   Description: {port.description}")
        if port.hwid:
            print(f"   Hardware ID: {port.hwid}")
        
        # Highlight potential ESP32 devices
        desc_lower = port.description.lower()
        if any(keyword in desc_lower for keyword in ['cp210', 'ch340', 'ftdi', 'usb', 'serial']):
            print("   >>> Possible ESP32 device <<<")
        print()
    
    return available_ports

def prompt_port_selection(available_ports):
    """
    Prompt user to select a port from available ports
    
    Args:
        available_ports (list): List of available port information
        
    Returns:
        str: Selected port device name, or None if cancelled
    """
    if not available_ports:
        return None
    
    while True:
        try:
            choice = input(f"Select port (1-{len(available_ports)}) or 'q' to quit: ").strip().lower()
            
            if choice == 'q':
                return None
                
            port_num = int(choice)
            if 1 <= port_num <= len(available_ports):
                selected_port = available_ports[port_num - 1]['device']
                print(f"Selected: {selected_port}")
                return selected_port
            else:
                print(f"Invalid choice. Please enter 1-{len(available_ports)} or 'q'")
                
        except ValueError:
            print("Invalid input. Please enter a number or 'q'")
        except KeyboardInterrupt:
            print("\nCancelled by user")
            return None

def parse_arguments():
    """
    Parse command-line arguments
    
    Returns:
        argparse.Namespace: Parsed command-line arguments
    """
    parser = argparse.ArgumentParser(
        description="ESP32 Self-Balancing Robot Controller",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python robot_controller.py --list-ports         # List available ports
  python robot_controller.py                      # Auto-detect and select port
  python robot_controller.py --port COM3          # Use specific port
  python robot_controller.py -p /dev/ttyUSB0 --protocol nrf24
  python robot_controller.py -p COM5 -b 9600 --protocol espnow

Controls:
  UP ARROW    - Move Forward
  DOWN ARROW  - Move Backward  
  LEFT ARROW  - Turn Left
  RIGHT ARROW - Turn Right
  SPACE       - Stop/Stand Still
  ESC         - Exit Program

The ESP32 must be running the PC Serial Bridge firmware (pc_serial_bridge.cpp)
from the esp32_remote_control library examples.
        """
    )
    
    parser.add_argument(
        '--port', '-p',
        help='Serial port for ESP32 bridge (e.g., COM3, /dev/ttyUSB0). If not specified, available ports will be listed.'
    )
    
    parser.add_argument(
        '--baud', '-b',
        type=int,
        default=115200,
        help='Serial baud rate (default: 115200)'
    )
    
    parser.add_argument(
        '--protocol',
        choices=['espnow', 'nrf24'],
        default='espnow',
        help='Wireless protocol to use (default: espnow)'
    )
    
    parser.add_argument(
        '--list-ports',
        action='store_true',
        help='List available serial ports and exit'
    )
    
    return parser.parse_args()

def main():
    """
    Main program entry point
    
    Parses command-line arguments, sets up the robot controller, establishes 
    serial connection to ESP32 bridge, and starts the interactive keyboard 
    control loop. Handles graceful shutdown on user interrupt or errors.
    """
    # Enable ANSI colors in Windows terminal
    enable_ansi_colors()
    
    print("ESP32 Self-Balancing Robot Controller")
    print("=====================================")
    
    # Parse command-line arguments
    args = parse_arguments()
    
    # Handle --list-ports option
    if args.list_ports:
        list_serial_ports()
        return
    
    # Handle port discovery if no port specified
    if not args.port:
        print("No port specified. Scanning for available serial ports...\n")
        available_ports = list_serial_ports()
        
        if not available_ports:
            print("No serial ports found. Connect ESP32 bridge and try again.")
            return
            
        selected_port = prompt_port_selection(available_ports)
        if not selected_port:
            print("No port selected. Exiting.")
            return
        
        args.port = selected_port
        print()
    
    # Verify keyboard input support is available
    if not HAS_MSVCRT and not HAS_TERMIOS:
        print("Error: No keyboard input support available")
        print("This system requires either:")
        print("- Windows (msvcrt module)")  
        print("- Linux/Unix (termios module)")
        print("- Install required libraries or run on supported platform")
        return
    
    # Create robot controller instance with parsed arguments
    controller = RobotController(
        port=args.port, 
        baudrate=args.baud, 
        protocol=args.protocol
    )
    
    try:
        # Establish serial connection to ESP32 bridge
        print(f"Connecting to ESP32 bridge on {controller.port}...")
        print(f"Protocol: {controller.protocol}, Baud: {controller.baudrate}")
        
        connection_result = controller.connect()
        
        if connection_result:
            print("[SUCCESS] Bridge connection successful!\n")
        else:
            # Check if serial connection was established
            if controller.serial and controller.serial.is_open:
                print("WARNING: Serial connected but bridge not responding correctly")
                print("This could mean:")
                print("- ESP32 PC Serial Bridge firmware is not running")
                print("- Wrong firmware uploaded to ESP32")
                print("- Bridge is starting up or busy")
                print("- Wrong protocol specified")
                print("\nContinuing anyway - commands will be sent to serial port...")
                print("Upload pc_serial_bridge.cpp if connection issues persist.\n")
            else:
                print("ERROR: Failed to establish serial connection")
                print("Check:")
                print("- ESP32 is connected and powered")
                print("- Correct serial port specified")
                print("- Port is not in use by another application")
                return
        
        # Start interactive control loop
        controller.control_loop()
        
    except KeyboardInterrupt:
        print("\nController interrupted by user (Ctrl+C)")
    except Exception as e:
        print(f"Unexpected error: {e}")
        print("Check connections and ESP32 bridge status")
    finally:
        # Ensure clean shutdown
        print("Shutting down controller...")
        controller.disconnect()

if __name__ == "__main__":
    main()