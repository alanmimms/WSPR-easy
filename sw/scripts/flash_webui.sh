#!/bin/bash
#
# Build LittleFS image from web UI files and flash to ESP32-S3
#
# Usage: ./flash_webui.sh [port]
#   port: Serial port (default: /dev/ttyACM0)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WEBUI_DIR="$(dirname "$PROJECT_DIR")/webui/www"
BUILD_DIR="$PROJECT_DIR/build"

# Use west's Python venv
WEST_VENV="$HOME/.local/share/pipx/venvs/west"
PYTHON="$WEST_VENV/bin/python"

# LittleFS partition info from app.overlay
# After ESP32-S3 default partitions: boot(64KB) + slot0(1472KB) + slot1(1472KB) + scratch(64KB) + storage(64KB)
LFS_OFFSET=0x310000   # 3.0625MB offset
LFS_SIZE=$((512 * 1024))  # 512KB

# Serial port
PORT="${1:-/dev/ttyACM0}"

# Check for littlefs-python in west venv
if ! $PYTHON -c "import littlefs" 2>/dev/null; then
    echo "Installing littlefs-python into west venv..."
    $PYTHON -m pip install littlefs-python
fi

# Create LittleFS image using Python
echo "Creating LittleFS image from $WEBUI_DIR"

$PYTHON << EOF
import os
from littlefs import LittleFS

# LittleFS config matching Zephyr defaults
block_size = 4096
block_count = $LFS_SIZE // 4096

# Create filesystem
fs = LittleFS(block_size=block_size, block_count=block_count)

# Add web UI files
webui_dir = "$WEBUI_DIR"
for filename in os.listdir(webui_dir):
    filepath = os.path.join(webui_dir, filename)
    if os.path.isfile(filepath):
        with open(filepath, 'rb') as f:
            data = f.read()
        with fs.open('/' + filename, 'wb') as f:
            f.write(data)
        print(f"  Added: {filename} ({len(data)} bytes)")

# Write image
image_path = "$BUILD_DIR/littlefs.bin"
os.makedirs(os.path.dirname(image_path), exist_ok=True)
with open(image_path, 'wb') as f:
    f.write(fs.context.buffer)
print(f"Created: {image_path} ({os.path.getsize(image_path)} bytes)")
EOF

echo ""
echo "Flashing LittleFS image to $PORT at offset $LFS_OFFSET"

# Use esptool to flash (same tool used by west)
ESPTOOL="$HOME/.local/share/pipx/venvs/west/bin/esptool.py"
if [ ! -f "$ESPTOOL" ]; then
    ESPTOOL="esptool.py"
fi

$ESPTOOL --chip esp32s3 \
    --port "$PORT" \
    --baud 921600 \
    write_flash \
    --flash_mode dio \
    --flash_freq 80m \
    $LFS_OFFSET "$BUILD_DIR/littlefs.bin"

echo ""
echo "Done! Web UI files have been flashed to device."
echo "Reboot the device to load the new files."
