/* file: src/model/GnssData.hpp */
#pragma once
#include <string>
#include <cstdint>
#include <ctime>
#include <zephyr/kernel.h> // For k_uptime_get()

struct GnssData {
  // --- Metadata ---
  int64_t lastMS = 0; // ESP32 uptime when the NMEA string ended
    
  // --- Position & Time ---
  bool hasFix = false;
  double lat = 0.0;
  double lon = 0.0;
  std::string gridSquare = "AA00"; // calculated immediately
  time_t utcTime = 0;

  // --- Signal Stats ---
  int satellites = 0;
  float hdop = 99.9f;
  float altitude = 0.0f;

  /**
   * @brief Checks if this data is older than the specified duration.
   */
  bool isStale(int64_t maxAgeMs = 5000) const {
    return (k_uptime_get() - lastMS) > maxAgeMs;
  }
};
