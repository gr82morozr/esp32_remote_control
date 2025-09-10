#!/usr/bin/env python3
"""
Keyboard to Serial Bridge for ESP32 ESPNOW

Captures keyboard input and transmits it to the ESP32 Serial-to-ESPNOW bridge.
Maps keystrokes to RCPayload_t structure compatible with serial_espnow_bridge.cpp.

Uses built-in Python libraries only (no external dependencies).

Supported Commands:
- Letters: a-z (mapped to id1=1-26)  
- Numbers: 0-9 (mapped to id1=48-57, ASCII values)
- Arrow Keys: up/down/left/right (mapped to value1-value4)
- quit/exit: Exit program

Usage:
    python keyboard_serial.py

Requirements:
    - Python 3.x with pyserial: pip install pyserial
"""

import serial
import serial.tools.list_ports
import struct
import sys
import time

class KeyboardSerial:
    def __init__(self):
        """Initialize keyboard serial bridge"""
        self.serial_conn = None
        self.running = False
        
        # RCPayload_t structure: id1, id2, id3, id4, value1-5 (floats), flags
        self.payload_format = '<BBBB5fB'  # Little-endian, 4 bytes + 5 floats + 1 byte = 25 bytes
        
        # Key mappings
        self.key_mappings = {
            # Letters a-z -> id1 = 1-26
            **{chr(ord('a') + i): {'id1': i + 1, 'description': f'Letter {chr(ord("A") + i)}'} for i in range(26)},
            
            # Numbers 0-9 -> id1 = ASCII values (48-57)  
            **{str(i): {'id1': ord(str(i)), 'description': f'Number {i}'} for i in range(10)},
            
            # Arrow keys -> value1-4 (directional values)
            'up': {'value1': 100.0, 'id1': 200, 'description': 'Arrow Up'},
            'down': {'value1': -100.0, 'id1': 201, 'description': 'Arrow Down'}, 
            'left': {'value2': -100.0, 'id1': 202, 'description': 'Arrow Left'},
            'right': {'value2': 100.0, 'id1': 203, 'description': 'Arrow Right'},
        }
        
    def list_com_ports(self):
        """List available COM ports"""
        ports = serial.tools.list_ports.comports()
        available_ports = []
        
        print("\n=== Available COM Ports ===")
        if not ports:
            print("No COM ports found!")
            return []
            
        for i, port in enumerate(ports, 1):
            print(f"{i}. {port.device} - {port.description}")
            if port.hwid:
                print(f"   Hardware ID: {port.hwid}")
            available_ports.append(port.device)
            
        return available_ports
        
    def select_com_port(self):
        """Let user select COM port"""
        available_ports = self.list_com_ports()
        
        if not available_ports:
            return None
            
        while True:
            try:
                print(f"\nEnter port number (1-{len(available_ports)}) or 'q' to quit:")
                choice = input("> ").strip().lower()
                
                if choice in ['q', 'quit', 'exit']:
                    return None
                    
                port_index = int(choice) - 1
                if 0 <= port_index < len(available_ports):
                    return available_ports[port_index]
                else:
                    print("Invalid selection. Please try again.")
                    
            except ValueError:
                print("Please enter a valid number.")
                
    def connect_serial(self, port, baud=115200):
        """Connect to serial port"""
        try:
            self.serial_conn = serial.Serial(port, baud, timeout=1)
            time.sleep(2)  # Wait for Arduino to reset
            print(f"✅ Connected to {port} at {baud} baud")
            return True
        except serial.SerialException as e:
            print(f"❌ Error connecting to {port}: {e}")
            return False
            
    def create_payload(self, key_data):
        """Create RCPayload_t structure from key mapping"""
        # Default payload structure
        payload = {
            'id1': 0, 'id2': 0, 'id3': 0, 'id4': 0,
            'value1': 0.0, 'value2': 0.0, 'value3': 0.0, 'value4': 0.0, 'value5': 0.0,
            'flags': 1  # Flag to indicate keystroke
        }
        
        # Update with key-specific data
        payload.update(key_data)
        
        # Pack into binary structure
        return struct.pack(self.payload_format,
            payload['id1'], payload['id2'], payload['id3'], payload['id4'],
            payload['value1'], payload['value2'], payload['value3'], payload['value4'], payload['value5'],
            payload['flags']
        )
        
    def send_command(self, command):
        """Send command data via serial"""
        if command in self.key_mappings and self.serial_conn:
            payload_data = self.create_payload(self.key_mappings[command])
            
            # Send to serial bridge
            self.serial_conn.write(payload_data)
            
            # Debug output
            key_info = self.key_mappings[command]
            print(f"📤 Sent '{command}' ({key_info['description']}) -> "
                  f"ID1:{key_info.get('id1', 0)} "
                  f"V1:{key_info.get('value1', 0.0):.1f} "
                  f"V2:{key_info.get('value2', 0.0):.1f}")
            return True
        return False
        
    def show_help(self):
        """Display available commands"""
        print("\n=== Available Commands ===")
        print("📝 Letters: a-z (ID1: 1-26)")
        print("🔢 Numbers: 0-9 (ID1: 48-57)")  
        print("⬆️  Arrows: up, down, left, right (Value1/Value2: ±100.0)")
        print("❓ Commands: help, status, quit/exit")
        print("\nType a command and press Enter...")
        
    def show_status(self):
        """Show connection status"""
        if self.serial_conn and self.serial_conn.is_open:
            print(f"✅ Connected to {self.serial_conn.port} at {self.serial_conn.baudrate} baud")
        else:
            print("❌ Not connected")
            
    def run(self):
        """Main loop - get user commands"""
        print("\n🚀 ESP32 Keyboard-to-Serial Bridge")
        print("=" * 40)
        
        # Select COM port
        selected_port = self.select_com_port()
        if not selected_port:
            print("No port selected. Exiting...")
            return
            
        # Connect to serial
        if not self.connect_serial(selected_port):
            return
            
        self.show_help()
        self.running = True
        
        try:
            while self.running:
                print("\n> ", end="")
                command = input().strip().lower()
                
                if not command:
                    continue
                    
                # Handle special commands
                if command in ['quit', 'exit', 'q']:
                    print("👋 Goodbye!")
                    break
                elif command in ['help', '?', 'h']:
                    self.show_help()
                elif command in ['status', 's']:
                    self.show_status()
                elif command == 'clear':
                    print("\n" * 50)  # Clear screen
                else:
                    # Try to send as keystroke command
                    if not self.send_command(command):
                        print(f"❌ Unknown command: '{command}'. Type 'help' for available commands.")
                        
        except KeyboardInterrupt:
            print("\n\n🛑 Ctrl+C pressed. Exiting...")
        except EOFError:
            print("\n\n🛑 EOF received. Exiting...")
        finally:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
                print("📡 Serial connection closed.")

def main():
    """Main entry point"""
    print("ESP32 Serial-ESPNOW Keyboard Bridge")
    print("Requires: pip install pyserial")
    
    try:
        import serial
    except ImportError:
        print("❌ Error: pyserial not found. Install with: pip install pyserial")
        return
        
    # Start keyboard bridge
    bridge = KeyboardSerial()
    bridge.run()

if __name__ == "__main__":
    main()