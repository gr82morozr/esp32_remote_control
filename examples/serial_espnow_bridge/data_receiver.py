#!/usr/bin/env python3
"""
ESP32 ESPNOW Data Receiver
=========================

Receives raw RCPayload_t data from ESP32 Serial-ESPNOW Bridge and displays it in JSON format.

The ESP32 bridge receives ESPNOW packets and forwards the raw 25-byte RCPayload_t 
structure to serial. This script parses that binary data and displays it as 
human-readable JSON on the console.

RCPayload_t Structure (25 bytes):
- id1, id2, id3, id4: uint8_t (4 bytes)
- value1, value2, value3, value4, value5: float (20 bytes)  
- flags: uint8_t (1 byte)

Usage:
    python data_receiver.py
    python data_receiver.py --port COM3
    python data_receiver.py --port COM3 --baud 115200

Requirements:
    - Python 3.x with pyserial: pip install pyserial
    - ESP32 running serial_espnow_bridge.cpp
"""

import serial
import serial.tools.list_ports
import struct
import sys
import time
import json
import argparse

class ESPNOWDataReceiver:
    def __init__(self, port=None, baudrate=115200):
        """Initialize data receiver"""
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.running = False
        
        # RCPayload_t structure: 4 uint8_t + 5 float + 1 uint8_t = 25 bytes
        self.payload_format = '<BBBB5fB'  # Little-endian format
        self.payload_size = struct.calcsize(self.payload_format)  # Should be 25 bytes
        
        print(f"RCPayload_t size: {self.payload_size} bytes")
        
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
                
    def connect_serial(self):
        """Connect to serial port"""
        try:
            if not self.port:
                self.port = self.select_com_port()
                if not self.port:
                    return False
                    
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)  # Wait for Arduino to reset
            print(f"✅ Connected to {self.port} at {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"❌ Error connecting to {self.port}: {e}")
            return False
            
    def parse_payload(self, raw_data):
        """Parse raw bytes into RCPayload_t structure"""
        if len(raw_data) != self.payload_size:
            return None
            
        try:
            # Unpack binary data
            unpacked = struct.unpack(self.payload_format, raw_data)
            
            # Create JSON structure
            payload = {
                "id1": unpacked[0],
                "id2": unpacked[1], 
                "id3": unpacked[2],
                "id4": unpacked[3],
                "value1": round(unpacked[4], 3),
                "value2": round(unpacked[5], 3),
                "value3": round(unpacked[6], 3),
                "value4": round(unpacked[7], 3),
                "value5": round(unpacked[8], 3),
                "flags": unpacked[9]
            }
            
            return payload
        except struct.error as e:
            print(f"Parse error: {e}")
            return None
            
    def run(self):
        """Main receive loop"""
        print("\n🚀 ESP32 ESPNOW Data Receiver")
        print("=" * 40)
        print("Listening for ESPNOW data from ESP32 bridge...")
        print("Press Ctrl+C to exit\n")
        
        if not self.connect_serial():
            return
            
        self.running = True
        buffer = bytearray()
        packet_count = 0
        
        try:
            while self.running:
                if self.serial_conn.in_waiting > 0:
                    # Read available bytes
                    data = self.serial_conn.read(self.serial_conn.in_waiting)
                    buffer.extend(data)
                    
                    # Process complete packets
                    while len(buffer) >= self.payload_size:
                        # Extract one payload packet
                        packet_data = bytes(buffer[:self.payload_size])
                        buffer = buffer[self.payload_size:]
                        
                        # Parse and display
                        payload = self.parse_payload(packet_data)
                        if payload:
                            packet_count += 1
                            
                            # Print JSON on one line
                            json_str = json.dumps(payload, separators=(',', ':'))
                            timestamp = time.strftime("%H:%M:%S")
                            print(f"[{timestamp}] #{packet_count:04d}: {json_str}")
                        else:
                            print("Failed to parse packet")
                            
                time.sleep(0.001)  # Small delay to prevent busy waiting
                
        except KeyboardInterrupt:
            print("\n\n🛑 Ctrl+C pressed. Exiting...")
        except Exception as e:
            print(f"\n❌ Error: {e}")
        finally:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
                print("📡 Serial connection closed.")
                
def parse_arguments():
    """Parse command-line arguments"""
    parser = argparse.ArgumentParser(
        description="ESP32 ESPNOW Data Receiver - Parse raw data to JSON",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python data_receiver.py                    # Auto-select port
  python data_receiver.py --port COM3        # Use specific port
  python data_receiver.py -p /dev/ttyUSB0 -b 9600

Expected data format:
  RCPayload_t: 4 uint8_t IDs + 5 float values + 1 uint8_t flags (25 bytes total)
  
Output format:
  [HH:MM:SS] #0001: {"id1":123,"id2":45,"value1":12.345,"flags":1}
        """
    )
    
    parser.add_argument(
        '--port', '-p',
        help='Serial port (e.g., COM3, /dev/ttyUSB0). If not specified, will prompt for selection.'
    )
    
    parser.add_argument(
        '--baud', '-b',
        type=int,
        default=115200,
        help='Serial baud rate (default: 115200)'
    )
    
    return parser.parse_args()

def main():
    """Main entry point"""
    print("ESP32 ESPNOW Data Receiver")
    print("Requires: pip install pyserial")
    
    try:
        import serial
    except ImportError:
        print("❌ Error: pyserial not found. Install with: pip install pyserial")
        return
        
    args = parse_arguments()
    
    # Create and run receiver
    receiver = ESPNOWDataReceiver(port=args.port, baudrate=args.baud)
    receiver.run()

if __name__ == "__main__":
    main()