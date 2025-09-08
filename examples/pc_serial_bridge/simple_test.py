#!/usr/bin/env python3
"""
Simple ESP32 Bridge Test Script

A minimal example showing how to send commands to the ESP32 bridge.
Perfect for quick testing and learning the API.

Usage:
    python simple_test.py COM3
    python simple_test.py /dev/ttyUSB0
"""

import serial
import json
import time
import sys

def send_command(ser, command):
    """Send JSON command to ESP32 bridge"""
    json_str = json.dumps(command)
    ser.write((json_str + '\n').encode())
    print(f"Sent: {json_str}")

def read_responses(ser, timeout=2):
    """Read and print responses from ESP32 bridge"""
    start_time = time.time()
    while time.time() - start_time < timeout:
        if ser.in_waiting:
            response = ser.readline().decode('utf-8', errors='ignore').strip()
            if response:
                print(f"Response: {response}")
        time.sleep(0.01)

def main():
    if len(sys.argv) != 2:
        print("Usage: python simple_test.py <COM_PORT>")
        print("Example: python simple_test.py COM3")
        sys.exit(1)
    
    port = sys.argv[1]
    
    try:
        # Connect to ESP32 bridge
        print(f"Connecting to ESP32 bridge on {port}...")
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)  # Wait for ESP32 initialization
        
        print("✅ Connected! Running test sequence...")
        
        # Test 1: Get status
        print("\n1. Getting bridge status...")
        send_command(ser, {"cmd": "status"})
        read_responses(ser)
        
        # Test 2: Send some data
        print("\n2. Sending test data...")
        send_command(ser, {
            "cmd": "data",
            "v1": 45.0,
            "v2": 30.0,
            "id1": 1,
            "flags": 3
        })
        read_responses(ser)
        
        # Test 3: Switch protocol (if NRF24 is connected)
        print("\n3. Testing protocol switch...")
        send_command(ser, {"cmd": "switch", "protocol": "nrf24"})
        read_responses(ser)
        
        # Test 4: Send more data with new protocol
        print("\n4. Sending data with NRF24...")
        send_command(ser, {
            "cmd": "data",
            "v1": 75.5,
            "v2": 20.0,
            "v3": 15.5,
            "id1": 2,
            "id2": 3,
            "flags": 7
        })
        read_responses(ser)
        
        # Test 5: Switch back to ESP-NOW
        print("\n5. Switching back to ESP-NOW...")
        send_command(ser, {"cmd": "switch", "protocol": "espnow"})
        read_responses(ser)
        
        # Test 6: Final status check
        print("\n6. Final status check...")
        send_command(ser, {"cmd": "status"})
        read_responses(ser, 3)  # Wait longer for status
        
        print("\n✅ Test sequence completed!")
        
    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        print("Make sure the ESP32 is connected and the correct port is specified.")
    except KeyboardInterrupt:
        print("\n👋 Test interrupted by user")
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
    finally:
        if 'ser' in locals():
            ser.close()
            print("🔌 Serial connection closed")

if __name__ == "__main__":
    main()