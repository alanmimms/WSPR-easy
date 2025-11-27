/* file: src/dev/GnssReceiver.cpp */
#include "GnssReceiver.hpp"
#include <zephyr/logging/log.h>
#include <vector>
#include <charconv>
#include <cmath>

LOG_MODULE_DECLARE(gnss);

GnssReceiver::GnssReceiver(const struct device* uartDev) : uart(uartDev) {}

void GnssReceiver::init() {
  if (!device_is_ready(uart)) {
    LOG_ERR("GNSS UART not ready");
    return;
  }
  uart_irq_callback_user_data_set(uart, uartCallback, this);
  uart_irq_rx_enable(uart);
}

GnssData GnssReceiver::getLatestData() const {
  std::lock_guard<std::mutex> lock(dataMtx);
  return latestData;
}

void GnssReceiver::uartCallback(const struct device *dev, void *user_data) {
  GnssReceiver* self = static_cast<GnssReceiver*>(user_data);
  uint8_t c;
    
  if (!uart_irq_update(dev)) return;

  if (uart_irq_rx_ready(dev)) {
    while (uart_fifo_read(dev, &c, 1) == 1) {
      // Timestamp capture: When we hit a newline, that is the "Time Received"
      if (c == '\n' || c == '\r') {
	if (self->rxIndex > 0) {
	  int64_t now = k_uptime_get(); // Capture time immediately
	  self->rxBuffer[self->rxIndex] = '\0';
                    
	  // Process immediately (or push to queue if stack is tight)
	  self->processLine(std::string_view(self->rxBuffer, self->rxIndex), now);
	  self->rxIndex = 0;
	}
      } else {
	if (self->rxIndex < RCV_BUF_SIZE - 1) {
	  self->rxBuffer[self->rxIndex++] = (char)c;
	} else {
	  self->rxIndex = 0; // Overflow protection
	}
      }
    }
  }
}

void GnssReceiver::processLine(std::string_view line, int64_t timestamp) {
  // We use a local copy to modify, then swap into the protected member
  // This minimizes mutex hold time
  std::lock_guard<std::mutex> lock(dataMtx);
    
  // We update the existing state rather than replacing it, 
  // because RMC and GGA update different fields of the same "Truth".
  // However, we MUST update the timestamp for freshness.
    
  bool validParse = false;
    
  if (line.starts_with("$GPRMC") || line.starts_with("$GNRMC")) {
    parseRmc(line, latestData);
    validParse = true;
  } else if (line.starts_with("$GPGGA") || line.starts_with("$GNGGA")) {
    parseGga(line, latestData);
    validParse = true;
  }

  if (validParse) {
    latestData.receivedAtMs = timestamp;
  }
}

// ... parseNmeaCoord and split helpers (same as previous) ...

void GnssReceiver::parseRmc(std::string_view line, GnssData& target) {
  // Example: $GPRMC,123519,A,4807.038,N,01131.000,E,...
  // RMC updates: Fix, Lat, Lon, Grid, Time
  auto parts = split(line, ','); // (Assume split helper exists)
  if (parts.size() < 10) return;

  if (parts[2] == "A") {
    target.hasFix = true;
    target.lat = parseNmeaCoord(parts[3], parts[4]);
    target.lon = parseNmeaCoord(parts[5], parts[6]);
    target.gridSquare = latLonToGrid(target.lat, target.lon);
        
    // Parse UTC Time (HHMMSS) + Date (DDMMYY)
    // ... (Parsing logic from previous response) ...
    // target.utcTime = ...;
  } else {
    target.hasFix = false;
  }
}

void GnssReceiver::parseGga(std::string_view line, GnssData& target) {
  // GGA updates: Satellites, Altitude
  auto parts = split(line, ',');
  if (parts.size() < 10) return;

  // Check fix quality (Index 6)
  // Update Satellites (Index 7)
  std::from_chars(parts[7].data(), parts[7].data() + parts[7].size(), target.satellites);
    
  // Update Altitude (Index 9)
  char* end;
  target.altitude = strtof(std::string(parts[9]).c_str(), &end);
}

std::string GnssReceiver::latLonToGrid(double lat, double lon) {
  // (Logic from previous response)
  // ...
  return "AA00"; // placeholder
}
