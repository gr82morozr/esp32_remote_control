#!/usr/bin/env python3
"""
ESP32 Remote Control Bridge Client

A Python client for communicating with the ESP32 Serial Bridge.
Enables PC-based remote control testing via ESP-NOW/NRF24 protocols.

Features:
- Auto-detect ESP32 bridge on serial ports
- Interactive command interface
- Real-time data streaming
- Protocol switching
- Status monitoring
- JSON command validation

Usage:
    python esp32_bridge_client.py
    python esp32_bridge_client.py --port COM3 --protocol espnow
    python esp32_bridge_client.py --demo --duration 30

Author: ESP32 Remote Control Library
Version: 1.0.0
"""

import serial
import json
import time
import threading
import argparse
import sys
from datetime import datetime
from typing import Optional, Dict, Any

class ESP32BridgeClient:
    """Client for ESP32 Serial Bridge communication"""
    
    def __init__(self, port: str = None, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn: Optional[serial.Serial] = None
        self.connected = False
        self.running = False
        self.receive_thread: Optional[threading.Thread] = None
        
    def auto_detect_port(self) -> Optional[str]:
        """Auto-detect ESP32 bridge port by testing common ports"""
        import serial.tools.list_ports
        
        common_ports = []
        
        # Get all available ports
        available_ports = serial.tools.list_ports.comports()
        for port in available_ports:
            if any(keyword in port.description.lower() for keyword in 
                  ['ch340', 'cp210x', 'ftdi', 'esp32', 'silicon labs']):
                common_ports.append(port.device)
        
        # Add common port names
        for port_name in ['COM3', 'COM4', 'COM5', '/dev/ttyUSB0', '/dev/ttyACM0']:
            if port_name not in common_ports:
                common_ports.append(port_name)
        
        print(f"Testing ports: {common_ports}")
        
        for port in common_ports:
            try:
                test_conn = serial.Serial(port, self.baudrate, timeout=2)
                test_conn.write(b'{"cmd":"status"}\n')
                time.sleep(0.5)
                
                response = ""
                while test_conn.in_waiting:
                    response += test_conn.read(test_conn.in_waiting).decode('utf-8', errors='ignore')
                
                if "bridge" in response.lower() or "esp32_rc" in response.lower():
                    print(f"✅ Found ESP32 bridge on {port}")
                    test_conn.close()
                    return port
                    
                test_conn.close()
            except Exception as e:
                pass  # Port not available or no response
        
        return None
    
    def connect(self) -> bool:
        """Connect to ESP32 bridge"""
        if not self.port:
            self.port = self.auto_detect_port()
            if not self.port:
                print("❌ No ESP32 bridge found. Please specify port manually.")
                return False
        
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)  # Wait for ESP32 to initialize
            
            # Test connection
            self.send_command({"cmd": "status"})
            time.sleep(0.5)
            
            self.connected = True
            self.running = True
            
            # Start receive thread
            self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self.receive_thread.start()
            
            print(f"✅ Connected to ESP32 bridge on {self.port}")
            return True
            
        except Exception as e:
            print(f"❌ Failed to connect to {self.port}: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from ESP32 bridge"""
        self.running = False
        if self.serial_conn:
            self.serial_conn.close()
        self.connected = False
        print("🔌 Disconnected from ESP32 bridge")
    
    def send_command(self, command: Dict[str, Any]) -> bool:
        """Send JSON command to ESP32 bridge"""
        if not self.connected:
            print("❌ Not connected to bridge")
            return False
        
        try:
            json_str = json.dumps(command)
            self.serial_conn.write((json_str + '\n').encode())
            print(f"📤 Sent: {json_str}")
            return True
        except Exception as e:
            print(f"❌ Failed to send command: {e}")
            return False
    
    def send_data(self, v1=0.0, v2=0.0, v3=0.0, v4=0.0, v5=0.0, 
                  id1=0, id2=0, id3=0, id4=0, flags=0) -> bool:
        """Send remote control data"""
        return self.send_command({
            "cmd": "data",
            "v1": float(v1), "v2": float(v2), "v3": float(v3), 
            "v4": float(v4), "v5": float(v5),
            "id1": int(id1), "id2": int(id2), "id3": int(id3), "id4": int(id4),
            "flags": int(flags)
        })
    
    def switch_protocol(self, protocol: str) -> bool:
        """Switch wireless protocol"""
        if protocol.lower() not in ['espnow', 'nrf24']:
            print("❌ Invalid protocol. Use 'espnow' or 'nrf24'")
            return False
        return self.send_command({"cmd": "switch", "protocol": protocol.lower()})
    
    def get_status(self) -> bool:
        """Get bridge status"""
        return self.send_command({"cmd": "status"})
    
    def get_help(self) -> bool:
        """Get help information"""
        return self.send_command({"cmd": "help"})
    
    def _receive_loop(self):
        """Background thread to receive data from ESP32"""
        buffer = ""
        while self.running:
            try:
                if self.serial_conn.in_waiting:
                    data = self.serial_conn.read(self.serial_conn.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data
                    
                    # Process complete lines
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            self._process_received_line(line)
                
                time.sleep(0.01)  # Small delay to prevent CPU spinning
                
            except Exception as e:
                if self.running:
                    print(f"❌ Receive error: {e}")
                break
    
    def _process_received_line(self, line: str):
        """Process received JSON line from ESP32"""
        try:
            data = json.loads(line)
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            
            if "event" in data:
                # Incoming data from remote device
                if data["event"] == "data_received":
                    print(f"📥 [{timestamp}] Remote data: v1={data.get('v1', 0):.2f}, "
                          f"v2={data.get('v2', 0):.2f}, id1={data.get('id1', 0)}, flags={data.get('flags', 0)}")
                          
            elif "status" in data:
                if data["status"] == "data_sent":
                    print(f"✅ [{timestamp}] Data sent via {data.get('protocol', 'unknown')}")
                elif data["status"] == "protocol_switched":
                    print(f"🔄 [{timestamp}] Switched to {data.get('protocol', 'unknown')}")
                elif isinstance(data["status"], dict):
                    # Detailed status response
                    status = data["status"]
                    print(f"📊 [{timestamp}] Bridge Status:")
                    print(f"   Protocol: {status.get('protocol', 'unknown')}")
                    print(f"   Connection: {status.get('connection', 'unknown')}")
                    if 'send_metrics' in status:
                        sm = status['send_metrics']
                        print(f"   Send: {sm.get('success', 0)}/{sm.get('total', 0)} ({sm.get('rate', 0):.1f}%)")
                    if 'recv_metrics' in status:
                        rm = status['recv_metrics']
                        print(f"   Recv: {rm.get('success', 0)}/{rm.get('total', 0)} ({rm.get('rate', 0):.1f}%)")
                        
            elif "error" in data:
                print(f"❌ [{timestamp}] Error: {data['error']}")
                
            elif "heartbeat" in data:
                print(f"💓 [{timestamp}] Bridge heartbeat")
                
            elif "help" in data:
                print(f"📖 [{timestamp}] Help received")
                
            else:
                print(f"📨 [{timestamp}] {line}")
                
        except json.JSONDecodeError:
            print(f"📨 [{datetime.now().strftime('%H:%M:%S')}] {line}")

def run_interactive_mode(client: ESP32BridgeClient):
    """Run interactive command mode"""
    print("\n🎮 ESP32 Bridge Interactive Mode")
    print("Commands: data, switch, status, help, demo, quit")
    print("Example: data 45.0 30.0 1 0 3  (v1 v2 id1 id2 flags)")
    
    client.get_help()  # Show initial help
    
    while client.connected:
        try:
            cmd_input = input("\n> ").strip()
            if not cmd_input:
                continue
                
            parts = cmd_input.split()
            cmd = parts[0].lower()
            
            if cmd == "quit" or cmd == "exit":
                break
            elif cmd == "data" and len(parts) >= 2:
                # Parse data command: data v1 v2 [v3] [v4] [v5] [id1] [id2] [id3] [id4] [flags]
                args = [0.0] * 10  # Default values
                for i, part in enumerate(parts[1:]):
                    if i < 10:
                        try:
                            args[i] = float(part)
                        except ValueError:
                            args[i] = int(part) if i >= 5 else 0.0
                
                client.send_data(args[0], args[1], args[2], args[3], args[4],
                               int(args[5]), int(args[6]), int(args[7]), int(args[8]), int(args[9]))
                               
            elif cmd == "switch" and len(parts) == 2:
                client.switch_protocol(parts[1])
            elif cmd == "status":
                client.get_status()
            elif cmd == "help":
                client.get_help()
            elif cmd == "demo":
                run_demo_sequence(client)
            else:
                print("❌ Unknown command. Type 'help' for available commands.")
                
        except KeyboardInterrupt:
            print("\n👋 Exiting interactive mode...")
            break
        except Exception as e:
            print(f"❌ Command error: {e}")

def run_demo_sequence(client: ESP32BridgeClient, duration: int = 10):
    """Run automated demo sequence"""
    print(f"\n🎯 Running demo sequence for {duration} seconds...")
    
    start_time = time.time()
    counter = 0
    
    while time.time() - start_time < duration:
        # Simulate joystick movement
        t = time.time() - start_time
        x = 50 + 30 * math.sin(t * 2)      # X oscillation
        y = 50 + 20 * math.cos(t * 1.5)   # Y oscillation
        
        client.send_data(v1=x, v2=y, id1=1, flags=counter % 4)
        
        counter += 1
        time.sleep(0.1)  # 10Hz update rate
    
    print("✅ Demo sequence completed")

def main():
    """Main function with argument parsing"""
    import math  # Import here for demo function
    
    parser = argparse.ArgumentParser(description="ESP32 Remote Control Bridge Client")
    parser.add_argument("--port", "-p", help="Serial port (auto-detect if not specified)")
    parser.add_argument("--baudrate", "-b", type=int, default=115200, help="Baud rate")
    parser.add_argument("--protocol", choices=["espnow", "nrf24"], help="Initial protocol")
    parser.add_argument("--demo", action="store_true", help="Run demo sequence")
    parser.add_argument("--duration", type=int, default=10, help="Demo duration in seconds")
    
    args = parser.parse_args()
    
    # Create and connect client
    client = ESP32BridgeClient(args.port, args.baudrate)
    
    if not client.connect():
        sys.exit(1)
    
    try:
        # Set initial protocol if specified
        if args.protocol:
            time.sleep(1)  # Wait for bridge to be ready
            client.switch_protocol(args.protocol)
            time.sleep(0.5)
        
        if args.demo:
            # Run demo mode
            run_demo_sequence(client, args.duration)
        else:
            # Run interactive mode
            run_interactive_mode(client)
            
    except KeyboardInterrupt:
        print("\n👋 Goodbye!")
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()