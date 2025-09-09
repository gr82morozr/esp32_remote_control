#!/usr/bin/env python3
"""
LED Control Demo - End-to-End Testing

Demonstrates complete PC -> ESP32 Bridge -> Mock Receiver -> LED control chain.
This script sends various commands to test different LED behaviors on the receiver.

Usage:
    python led_control_demo.py COM3
    python led_control_demo.py /dev/ttyUSB0 --protocol nrf24 --duration 60

Requirements:
1. ESP32 Bridge device connected via USB (running pc_serial_bridge.cpp)
2. ESP32 Mock Receiver device (running mock_receiver.cpp) 
3. Both devices powered and within wireless range

Features:
- Automated LED control patterns
- Real-time feedback monitoring
- Protocol switching demonstration
- Interactive mode for manual testing
- Status monitoring and error handling
"""

import serial
import json
import time
import sys
import argparse
import threading
from datetime import datetime
import math

class LEDControlDemo:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.connected = False
        self.running = False
        self.receive_thread = None
        
    def connect(self):
        """Connect to ESP32 bridge"""
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)  # Wait for ESP32 initialization
            
            # Test connection
            self.send_command({"cmd": "status"})
            time.sleep(0.5)
            
            self.connected = True
            self.running = True
            
            # Start receive thread
            self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self.receive_thread.start()
            
            print(f"[OK] Connected to ESP32 bridge on {self.port}")
            return True
            
        except Exception as e:
            print(f"[ERROR] Failed to connect: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from bridge"""
        self.running = False
        if self.serial_conn:
            self.serial_conn.close()
        self.connected = False
        print("[INFO] Disconnected from bridge")
    
    def send_command(self, command):
        """Send JSON command to bridge"""
        if not self.connected:
            return False
        
        try:
            json_str = json.dumps(command)
            self.serial_conn.write((json_str + '\n').encode())
            print(f"[SEND] {json_str}")
            return True
        except Exception as e:
            print(f"[ERROR] Send error: {e}")
            return False
    
    def _receive_loop(self):
        """Background thread to receive bridge responses"""
        buffer = ""
        while self.running:
            try:
                if self.serial_conn.in_waiting:
                    data = self.serial_conn.read(self.serial_conn.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data
                    
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            self._process_response(line)
                
                time.sleep(0.01)
            except Exception as e:
                if self.running:
                    print(f"[ERROR] Receive error: {e}")
                break
    
    def _process_response(self, line):
        """Process JSON responses from bridge"""
        try:
            data = json.loads(line)
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            
            if "status" in data:
                if data["status"] == "data_sent":
                    print(f"[OK] [{timestamp}] Command sent via {data.get('protocol', 'unknown')}")
                elif data["status"] == "protocol_switched":
                    print(f"[SWITCH] [{timestamp}] Switched to {data.get('protocol', 'unknown')}")
                    
            elif "event" in data and data["event"] == "data_received":
                print(f"[RECV] [{timestamp}] Receiver response: v1={data.get('v1', 0):.1f}")
                
            elif "error" in data:
                print(f"[ERROR] [{timestamp}] Error: {data['error']}")
                
        except json.JSONDecodeError:
            # Non-JSON response, just print it
            timestamp = datetime.now().strftime("%H:%M:%S")
            print(f"[MSG] [{timestamp}] {line}")

def run_demo_sequence(demo, duration=30):
    """Run automated LED control demonstration"""
    print(f"\n[DEMO] Starting LED Control Demo ({duration} seconds)")
    print("=" * 50)
    
    start_time = time.time()
    
    # Demo sequence
    demos = [
        {"name": "Fast Blink", "cmd": {"cmd": "data", "v1": 80.0, "id1": 1}, "duration": 3},
        {"name": "Medium Blink", "cmd": {"cmd": "data", "v1": 35.0, "id1": 1}, "duration": 3},
        {"name": "Slow Blink", "cmd": {"cmd": "data", "v1": 10.0, "id1": 1}, "duration": 3},
        {"name": "LED Always ON", "cmd": {"cmd": "data", "v1": 0.0, "flags": 1}, "duration": 2},
        {"name": "LED Always OFF", "cmd": {"cmd": "data", "v1": 0.0, "flags": 2}, "duration": 2},
        {"name": "Burst Blink x5", "cmd": {"cmd": "data", "v1": 50.0, "id1": 5}, "duration": 3},
        {"name": "Burst Blink x3", "cmd": {"cmd": "data", "v1": 50.0, "id1": 3}, "duration": 2},
    ]
    
    demo_index = 0
    current_demo_start = time.time()
    
    while time.time() - start_time < duration:
        now = time.time()
        
        # Check if it's time for next demo
        if demo_index < len(demos):
            current_demo = demos[demo_index]
            
            # Start new demo
            if now - current_demo_start >= current_demo.get("duration", 3):
                if demo_index + 1 < len(demos):
                    demo_index += 1
                    current_demo_start = now
                    next_demo = demos[demo_index]
                    print(f"\n[DEMO] {next_demo['name']}")
                    demo.send_command(next_demo["cmd"])
                else:
                    # Restart sequence
                    demo_index = 0
                    current_demo_start = now
                    print(f"\n[DEMO] Restarting sequence...")
                    
        else:
            # First demo
            print(f"[DEMO] {demos[0]['name']}")
            demo.send_command(demos[0]["cmd"])
            current_demo_start = now
        
        time.sleep(0.1)
    
    # End with normal blinking
    print("\n[DEMO] Normal Blink (End)")
    demo.send_command({"cmd": "data", "v1": 25.0, "flags": 0})
    
    print("\n[OK] Demo sequence completed!")

def run_interactive_mode(demo):
    """Run interactive LED control mode"""
    print("\n[INTERACTIVE] LED Control Mode")
    print("Commands:")
    print("  fast        - Fast blink (v1=80)")
    print("  medium      - Medium blink (v1=35)")
    print("  slow        - Slow blink (v1=10)")
    print("  on          - LED always ON (flags=1)")
    print("  off         - LED always OFF (flags=2)")
    print("  burst N     - Burst blink N times (id1=N)")
    print("  switch PROT - Switch protocol (espnow/nrf24)")
    print("  status      - Get bridge status")
    print("  quit        - Exit")
    
    while demo.connected:
        try:
            cmd_input = input("\n> ").strip().lower()
            if not cmd_input:
                continue
                
            parts = cmd_input.split()
            cmd = parts[0]
            
            if cmd in ['quit', 'exit']:
                break
            elif cmd == 'fast':
                demo.send_command({"cmd": "data", "v1": 80.0})
                print("[LED] Fast blink mode")
            elif cmd == 'medium':
                demo.send_command({"cmd": "data", "v1": 35.0})
                print("[LED] Medium blink mode")
            elif cmd == 'slow':
                demo.send_command({"cmd": "data", "v1": 10.0})
                print("[LED] Slow blink mode")
            elif cmd == 'on':
                demo.send_command({"cmd": "data", "flags": 1})
                print("[LED] LED forced ON")
            elif cmd == 'off':
                demo.send_command({"cmd": "data", "flags": 2})
                print("[LED] LED forced OFF")
            elif cmd == 'burst' and len(parts) == 2:
                try:
                    count = int(parts[1])
                    demo.send_command({"cmd": "data", "v1": 50.0, "id1": count})
                    print(f"[LED] Burst blink x{count}")
                except ValueError:
                    print("[ERROR] Invalid burst count")
            elif cmd == 'switch' and len(parts) == 2:
                protocol = parts[1]
                if protocol in ['espnow', 'nrf24']:
                    demo.send_command({"cmd": "switch", "protocol": protocol})
                    print(f"[SWITCH] Switching to {protocol}")
                else:
                    print("[ERROR] Invalid protocol (use espnow or nrf24)")
            elif cmd == 'status':
                demo.send_command({"cmd": "status"})
            else:
                print("[ERROR] Unknown command. Available: fast, medium, slow, on, off, burst N, switch PROT, status, quit")
                
        except KeyboardInterrupt:
            print("\n[INFO] Exiting interactive mode...")
            break
        except Exception as e:
            print(f"[ERROR] Command error: {e}")

def run_sine_wave_demo(demo, duration=20):
    """Run sine wave LED brightness control"""
    print(f"\n[SINE] Sine Wave Demo ({duration} seconds)")
    print("LED brightness will follow a sine wave pattern")
    
    start_time = time.time()
    
    while time.time() - start_time < duration:
        t = time.time() - start_time
        
        # Generate sine wave value (0-100)
        brightness = 50 + 40 * math.sin(t * 2 * math.pi / 4)  # 4 second period
        
        demo.send_command({
            "cmd": "data",
            "v1": brightness,
            "v2": t,  # Send time as v2 for reference
            "id1": 1
        })
        
        print(f"[SINE] Brightness: {brightness:.1f}%", end='\r')
        time.sleep(0.1)
    
    print("\n[OK] Sine wave demo completed!")

def main():
    parser = argparse.ArgumentParser(description="LED Control Demo for ESP32 Remote Control")
    parser.add_argument("port", help="Serial port for ESP32 bridge")
    parser.add_argument("--protocol", choices=["espnow", "nrf24"], help="Initial protocol")
    parser.add_argument("--mode", choices=["demo", "interactive", "sine"], default="demo", 
                       help="Demo mode: demo, interactive, or sine")
    parser.add_argument("--duration", type=int, default=30, help="Demo duration in seconds")
    
    args = parser.parse_args()
    
    # Create demo instance
    demo = LEDControlDemo(args.port)
    
    if not demo.connect():
        sys.exit(1)
    
    try:
        # Set initial protocol if specified
        if args.protocol:
            time.sleep(1)
            demo.send_command({"cmd": "switch", "protocol": args.protocol})
            time.sleep(0.5)
        
        # Get initial status
        demo.send_command({"cmd": "status"})
        time.sleep(1)
        
        print("\n[READY] ESP32 LED Control Demo Ready!")
        print("Make sure mock_receiver.cpp is running on a second ESP32")
        print("Watch the LED on the receiver device!")
        
        # Run selected mode
        if args.mode == "demo":
            run_demo_sequence(demo, args.duration)
        elif args.mode == "interactive":
            run_interactive_mode(demo)
        elif args.mode == "sine":
            run_sine_wave_demo(demo, args.duration)
            
    except KeyboardInterrupt:
        print("\n[INFO] Demo interrupted by user")
    except Exception as e:
        print(f"[ERROR] Demo error: {e}")
    finally:
        demo.disconnect()

if __name__ == "__main__":
    main()