#!/usr/bin/env python3
"""
Keyboard Remote Control Test Demo
=================================

Simple test script to demonstrate the keyboard remote control system without
requiring actual keyboard input. Sends predefined sequences of robot commands
to test the complete communication chain.

This is useful for:
- Testing the ESP32 PC Serial Bridge
- Verifying the keyboard receiver ESP32 responds correctly
- Debugging communication issues
- Demonstrating the system functionality

Usage:
    python test_demo.py --port COM3
    python test_demo.py --port /dev/ttyUSB0 --protocol nrf24
"""

import serial
import json
import time
import argparse
import sys

class RobotCommand:
    """Robot movement command constants (matching ESP32 receiver)"""
    STOP = 0
    FORWARD = 1
    BACKWARD = 2
    TURN_LEFT = 3
    TURN_RIGHT = 4

class TestDemo:
    """Automated test demo for keyboard remote control system"""
    
    def __init__(self, port, baudrate=115200, protocol='espnow'):
        self.port = port
        self.baudrate = baudrate
        self.protocol = protocol
        self.serial = None
        
    def connect(self):
        """Connect to ESP32 PC Serial Bridge"""
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=2)
            time.sleep(2)  # Wait for ESP32 boot/reset
            print(f"[OK] Connected to ESP32 bridge on {self.port}")
            
            # Clear startup messages
            self._clear_buffer()
            return True
        except Exception as e:
            print(f"[ERROR] Connection failed: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from bridge"""
        if self.serial and self.serial.is_open:
            self.serial.close()
            print("🔌 Disconnected from bridge")
    
    def send_command(self, command, speed=50.0, turn_rate=30.0, description=""):
        """Send robot command to bridge"""
        if not self.serial or not self.serial.is_open:
            return False
        
        # Create JSON command (matching robot_controller.py format)
        json_cmd = {
            "cmd": "data",
            "id1": 0xAA,  # Magic byte
            "id2": 0xBB,  # Protocol version
            "id3": 0,
            "id4": 0,
            "v1": float(command),
            "v2": speed,
            "v3": turn_rate,
            "v4": 0.0,
            "v5": 0.0,
            "flags": 0x01  # Command valid flag
        }
        
        try:
            json_str = json.dumps(json_cmd) + '\n'
            self.serial.write(json_str.encode('utf-8'))
            self.serial.flush()
            
            # Print command info
            command_names = ["STOP", "FORWARD", "BACKWARD", "TURN_LEFT", "TURN_RIGHT"]
            command_name = command_names[command] if 0 <= command <= 4 else "UNKNOWN"
            
            print(f">> Sent: {command_name}", end="")
            if command in [RobotCommand.FORWARD, RobotCommand.BACKWARD]:
                print(f" (speed: {speed}%)", end="")
            elif command in [RobotCommand.TURN_LEFT, RobotCommand.TURN_RIGHT]:
                print(f" (rate: {turn_rate}°/s)", end="")
            if description:
                print(f" - {description}", end="")
            print()
            
            return True
        except Exception as e:
            print(f"[ERROR] Send error: {e}")
            return False
    
    def _clear_buffer(self):
        """Clear any pending serial data"""
        if self.serial and self.serial.in_waiting > 0:
            self.serial.read(self.serial.in_waiting)
    
    def _check_bridge_response(self, timeout=0.5):
        """Check for bridge response and print if available"""
        if not self.serial:
            return
            
        start_time = time.time()
        while (time.time() - start_time) < timeout:
            if self.serial.in_waiting > 0:
                try:
                    line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        try:
                            msg = json.loads(line)
                            if 'status' in msg and msg.get('status') == 'data_sent':
                                protocol = msg.get('protocol', 'unknown')
                                print(f"   [OK] Bridge confirmed: sent via {protocol}")
                            elif 'error' in msg:
                                print(f"   [ERROR] Bridge error: {msg.get('error')}")
                        except json.JSONDecodeError:
                            # Not JSON, might be other bridge output
                            if 'bridge' in line.lower() or 'error' in line.lower():
                                print(f"   [INFO] Bridge: {line}")
                except:
                    pass
            time.sleep(0.01)
    
    def run_demo_sequence(self):
        """Run automated demo sequence"""
        print(">> Starting Keyboard Remote Control Demo")
        print("=" * 50)
        print()
        
        print("This demo will send a sequence of robot commands to test:")
        print("1. ESP32 PC Serial Bridge communication")
        print("2. Wireless transmission (ESP-NOW/NRF24)")
        print("3. ESP32 receiver keyboard command processing")
        print()
        print("Watch the receiver ESP32 serial output for command feedback!")
        print()
        
        # Demo sequence: various robot movements
        demo_commands = [
            (RobotCommand.STOP, 0, 0, "Initial stop command"),
            (RobotCommand.FORWARD, 25, 0, "Slow forward"),
            (RobotCommand.FORWARD, 50, 0, "Medium forward"),
            (RobotCommand.FORWARD, 75, 0, "Fast forward"),
            (RobotCommand.STOP, 0, 0, "Stop after forward"),
            (RobotCommand.BACKWARD, 30, 0, "Backward movement"),
            (RobotCommand.STOP, 0, 0, "Stop after backward"),
            (RobotCommand.TURN_LEFT, 0, 20, "Gentle left turn"),
            (RobotCommand.TURN_LEFT, 0, 45, "Sharp left turn"),
            (RobotCommand.TURN_RIGHT, 0, 30, "Right turn"),
            (RobotCommand.STOP, 0, 0, "Final stop"),
        ]
        
        print(f"Sending {len(demo_commands)} test commands...")
        print("-" * 50)
        
        for i, (command, speed, turn_rate, description) in enumerate(demo_commands, 1):
            print(f"[{i:2d}/{len(demo_commands)}] ", end="")
            
            if self.send_command(command, speed, turn_rate, description):
                # Brief pause to check for bridge response
                self._check_bridge_response()
            
            # Pause between commands to allow observation
            time.sleep(1.5)
        
        print("-" * 50)
        print("[OK] Demo sequence completed!")
        print()
        print("Expected behavior on receiver ESP32:")
        print("- LED should flash for each received command")
        print("- Serial output should show detailed command information")
        print("- Each command should be interpreted and echoed back")
        print()
        print("If you don't see receiver output:")
        print("- Check receiver ESP32 is flashed with keyboard_receiver.cpp")
        print("- Verify both devices are using same protocol (ESP-NOW/NRF24)")
        print("- Confirm receiver ESP32 serial monitor is open (115200 baud)")

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description="Keyboard Remote Control Test Demo",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python test_demo.py --port COM3
  python test_demo.py --port /dev/ttyUSB0 --protocol nrf24

This demo sends a sequence of robot commands to test the complete
keyboard remote control communication chain:
PC -> ESP32 Bridge -> Wireless -> ESP32 Receiver
        """
    )
    
    parser.add_argument('--port', '-p', required=True,
                       help='Serial port for ESP32 bridge (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--baud', '-b', type=int, default=115200,
                       help='Serial baud rate (default: 115200)')
    parser.add_argument('--protocol', choices=['espnow', 'nrf24'], default='espnow',
                       help='Wireless protocol to use (default: espnow)')
    
    return parser.parse_args()

def main():
    """Main program entry point"""
    print("ESP32 Keyboard Remote Control - Test Demo")
    print("========================================")
    
    args = parse_arguments()
    
    # Create demo instance
    demo = TestDemo(port=args.port, baudrate=args.baud, protocol=args.protocol)
    
    try:
        # Connect to bridge
        if not demo.connect():
            print("[ERROR] Failed to connect to ESP32 bridge")
            print("Check:")
            print("- ESP32 bridge is connected and powered")
            print("- Correct serial port specified")
            print("- PC Serial Bridge firmware is running")
            return 1
        
        # Run demo sequence
        demo.run_demo_sequence()
        
    except KeyboardInterrupt:
        print("\n[WARNING] Demo interrupted by user (Ctrl+C)")
    except Exception as e:
        print(f"[ERROR] Unexpected error: {e}")
        return 1
    finally:
        demo.disconnect()
    
    return 0

if __name__ == "__main__":
    sys.exit(main())