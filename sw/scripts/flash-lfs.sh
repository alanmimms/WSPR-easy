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

# Use west's Python venv or system python
WEST_VENV="$HOME/.local/share/pipx/venvs/west"
if [ -d "$WEST_VENV" ]; then
    PYTHON="$WEST_VENV/bin/python"
    ESPTOOL="$WEST_VENV/bin/esptool.py"
else
    PYTHON="python3"
    ESPTOOL="python3 -m esptool"
fi

# LittleFS partition info from devicetree (the source of truth)
ZEPHYR_DTS="$BUILD_DIR/zephyr/zephyr.dts"

if [ ! -f "$ZEPHYR_DTS" ]; then
    echo "Error: $ZEPHYR_DTS not found. Please build the project first ('west build')."
    exit 1
fi

# Use Python to extract values from the final devicetree
# We look for the node with label "littlefs" and extract its reg property
LFS_INFO=$($PYTHON -c '
import re, sys
dts = sys.argv[1]
with open(dts, "r") as f:
    content = f.read()
# Match partition that has label "littlefs"
match = re.search(r"partition@[0-9a-fA-F]+\s*\{[^}]*label\s*=\s*\"littlefs\";[^}]*reg\s*=\s*<\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*>;", content, re.DOTALL)
if match:
    print(f"{match.group(1)} {int(match.group(2), 16)}")
else:
    sys.exit(1)
' "$ZEPHYR_DTS" || true)

if [ -z "$LFS_INFO" ]; then
    echo "Error: Could not find 'littlefs' partition in $ZEPHYR_DTS"
    exit 1
fi

LFS_OFFSET=$(echo "$LFS_INFO" | awk '{print $1}')
LFS_SIZE=$(echo "$LFS_INFO" | awk '{print $2}')

echo "Using LittleFS partition from devicetree: offset $LFS_OFFSET, size $LFS_SIZE bytes"

# Serial port (command line arg > ESPTOOL_PORT env var > default)
PORT="${1:-${ESPTOOL_PORT:-/dev/ttyACM0}}"

# Check for littlefs-python in west venv
if ! $PYTHON -c "import littlefs" 2>/dev/null; then
    echo "Installing littlefs-python into west venv..."
    $PYTHON -m pip install littlefs-python
fi

# Create LittleFS image using Python
echo "Creating LittleFS image..."

$PYTHON << EOF
import os
from littlefs import LittleFS

# Paths
webui_dir = "$WEBUI_DIR"
fpga_img = os.path.join(os.path.dirname(os.path.dirname(webui_dir)), "FPGA", "build", "fpga.img")
image_path = "$BUILD_DIR/littlefs.bin"

# LittleFS config matching Zephyr defaults
block_size = 4096
block_count = $LFS_SIZE // 4096

# Create filesystem
fs = LittleFS(block_size=block_size, block_count=block_count)

# 1. Add web UI files
if os.path.exists(webui_dir):
    print(f"Adding web UI files from {webui_dir}:")
    for filename in os.listdir(webui_dir):
        filepath = os.path.join(webui_dir, filename)
        if os.path.isfile(filepath):
            with open(filepath, 'rb') as f:
                data = f.read()
            with fs.open('/' + filename, 'wb') as f:
                f.write(data)
            print(f"  Added: {filename} ({len(data)} bytes)")
else:
    print(f"Warning: web UI directory {webui_dir} not found")

# 2. Add FPGA bitstream
if os.path.exists(fpga_img):
    print(f"Adding FPGA bitstream from {fpga_img}:")
    with open(fpga_img, 'rb') as f:
        data = f.read()
    with fs.open('/fpga.img', 'wb') as f:
        f.write(data)
    print(f"  Added: fpga.img ({len(data)} bytes)")
else:
    print(f"Error: FPGA bitstream {fpga_img} not found! Run cd FPGA && ./build.sh first.")
    exit(1)

# Write image
os.makedirs(os.path.dirname(image_path), exist_ok=True)
with open(image_path, 'wb') as f:
    f.write(fs.context.buffer)
print(f"Created LittleFS image: {image_path} ({os.path.getsize(image_path)} bytes)")
EOF

echo ""
echo "Flashing LittleFS image to $PORT at offset $LFS_OFFSET"

# Use esptool to flash (same tool used by west)
$ESPTOOL --chip esp32s3 \
    --port "$PORT" \
    --baud 921600 \
    write-flash \
    --flash-mode dio \
    --flash-freq 80m \
    $LFS_OFFSET "$BUILD_DIR/littlefs.bin"

echo ""
echo "Done! Web UI files have been flashed to device."
echo "Reboot the device to load the new files."
