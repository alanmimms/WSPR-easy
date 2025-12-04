# WSPR-ease Web UI Test Environment

Mock web server environment for developing and testing the WSPR-ease web UI before deploying to ESP32.

## Architecture

```
┌─────────────────────────────────────────┐
│  Web Browser (localhost:8080)           │
└───────────────┬─────────────────────────┘
                │ HTTP
┌───────────────┴─────────────────────────┐
│  cpp-httplib Server (C++)                │
│  - Serves static files (HTML/CSS/JS)    │
│  - REST API endpoints                    │
└───────────────┬─────────────────────────┘
                │
┌───────────────┴─────────────────────────┐
│  API Server (api_server.hpp)            │
│  - File operations                       │
│  - Configuration management              │
│  - Status/control endpoints              │
└───────────────┬─────────────────────────┘
                │
┌───────────────┴─────────────────────────┐
│  HAL Layer                               │
│  - MockFilesystem (./www-test-data)        │
│  - ConfigManager (JSON)                  │
│  - Mock WSPR encoder/transmitter         │
└──────────────────────────────────────────┘
```

## REST API Endpoints

### File Operations
- `GET /api/files?path=/` - List files in directory
- `GET /api/files/<path>` - Download file
- `PUT /api/files/<path>` - Upload/update file
- `DELETE /api/files/<path>` - Delete file

### Configuration (persisted to filesystem)
- `GET /api/config` - Get current configuration (JSON)
- `PUT /api/config` - Update configuration
- `GET /api/config/export` - Download config.json
- `POST /api/config/import` - Upload config.json
- `POST /api/config/reset` - Reset to defaults

### Runtime Status (RAM only)
- `GET /api/status` - System status (GNSS, clock, PA, TX)

### Transmission Control
- `POST /api/tx/trigger` - Trigger immediate transmission (mock)

## Building

```bash
make        # Build the server
make setup  # Create directories and download dependencies
make clean  # Remove build artifacts
```

## Running

```bash
make run    # Build and run server
```

Or run directly:
```bash
./wspr_webui_server
```

Server will start on `http://localhost:8080`

## Testing

Run automated API tests:
```bash
make test
```

Manual testing:
```bash
# Get status
curl http://localhost:8080/api/status

# Get configuration
curl http://localhost:8080/api/config

# Trigger transmission
curl -X POST http://localhost:8080/api/tx/trigger

# Upload a file
curl -X PUT http://localhost:8080/api/files/test.txt -d "Hello WSPR"

# Download a file
curl http://localhost:8080/api/files/test.txt

# List files
curl http://localhost:8080/api/files?path=/
```

## File Structure

```
webui/
├── src/
│   ├── fs_hal.hpp          - Filesystem HAL interface
│   ├── mock_fs.hpp         - Linux filesystem implementation
│   ├── config.hpp          - Configuration structure
│   ├── config_manager.hpp  - JSON config serialization
│   ├── api_server.hpp      - REST API routing
│   ├── main.cpp            - Server entry point
│   └── httplib.h           - cpp-httplib (auto-downloaded)
├── www/
│   ├── index.html          - Web UI main page
│   ├── style.css           - Styling
│   └── app.js              - JavaScript application
├── test/
│   └── test_api.sh         - API test script
├── www-test-data/             - Runtime data directory
│   ├── www/                - Served static files
│   └── config.json         - Persisted configuration
├── Makefile
└── README.md
```

## Configuration

Configuration is stored as JSON in `www-test-data/config.json`:

```json
{
  "callsign": "W1ABC",
  "gridSquare": "FN42",
  "powerDbm": 23,
  "bands": [
    {"name": "80m", "enabled": false, "freqHz": 3570100, "priority": 100},
    {"name": "40m", "enabled": false, "freqHz": 7040100, "priority": 100},
    ...
  ],
  "mode": "sequential",
  "slotIntervalMin": 10,
  ...
}
```

## Development Workflow

1. **Modify Web UI files** in `www/` directory
2. **Upload to running server** via file transfer API:
   ```bash
   curl -X PUT http://localhost:8080/api/files/index.html --data-binary @www/index.html
   ```
3. **Refresh browser** to see changes
4. **Test REST API** using curl or web UI
5. **Deploy to ESP32** when stable

## Porting to ESP32

The code is designed to be portable:

- **HAL Layer**: Replace `MockFilesystem` with LittleFS implementation
- **HTTP Server**: Replace `cpp-httplib` with ESP32 web server (ESP-IDF or Zephyr)
- **API Server**: Reuse `api_server.hpp` unchanged
- **Config Manager**: Reuse `config_manager.hpp` unchanged
- **Web UI**: Copy `www/*` files to ESP32 filesystem

## Dependencies

- C++20 compiler (g++ 10+)
- cpp-httplib (auto-downloaded by Makefile)
- curl (for testing)

## Notes

- Configuration persists across restarts in `www-test-data/config.json`
- Logs and statistics are mock/in-memory only (no flash wear)
- File transfer allows rapid web UI iteration without recompiling
- All endpoints return JSON (except file downloads)
