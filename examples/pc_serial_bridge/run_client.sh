#!/bin/bash
# ESP32 Bridge Client - Linux/Mac Shell Script
# Make executable: chmod +x run_client.sh
# Run: ./run_client.sh

echo "ESP32 Remote Control Bridge Client"
echo "===================================="
echo

# Check if Python is installed
if ! command -v python3 &> /dev/null; then
    echo "ERROR: Python 3 is not installed"
    echo "Please install Python 3.6+ using your package manager"
    exit 1
fi

# Create virtual environment if it doesn't exist
if [ ! -d "venv" ]; then
    echo "Setting up Python environment..."
    python3 -m venv venv
    source venv/bin/activate
    pip install -r requirements.txt
else
    source venv/bin/activate
fi

echo
echo "Starting ESP32 Bridge Client..."
echo "Commands: data, switch, status, help, demo, quit"
echo

# Run the client
python3 esp32_bridge_client.py

echo
echo "Bridge client closed."
read -p "Press Enter to exit..."