#pragma once

#include <string>
#include <cstdint>
#include <array>
#include <vector>

/**
 * @brief WSPR-ease configuration structure
 *
 * All configuration that gets persisted to filesystem as JSON.
 * Does NOT include runtime state (logs, statistics).
 */
struct WSPRConfig {
  // Station Identity
  std::string callsign = "NOCALL";
  std::string gridSquare = "AA00";  // 4-char Maidenhead
  uint8_t powerDbm = 30;  // 30dBm = 1W (WSPR-ease hardware output)

  // Band Configuration (80m, 60m, 40m, 30m, 20m, 17m, 15m, 12m, 10m)
  struct BandConfig {
    bool enabled = false;
    uint32_t freqHz = 0;
    uint8_t priority = 0;  // 0-255, higher = more frequent
  };

  static constexpr int NUM_BANDS = 9;
  std::array<BandConfig, NUM_BANDS> bands;

  // Scheduling
  enum class Mode {
    MANUAL,      // Only transmit on manual trigger
    SEQUENTIAL,  // Rotate through enabled bands
    RANDOM,      // Random enabled band
    PRIORITY     // Weight by priority
  };

  Mode mode = Mode::SEQUENTIAL;
  uint16_t slotIntervalMin = 10;   // Minutes between transmissions
  uint8_t dutyCycle = 1;            // 1 out of N slots (1 = every slot)

  // Time Sources
  enum class TimeSource {
    SYSTEM,   // System clock (user-set)
    NTP,      // Network Time Protocol
    GNSS      // GPS/GLONASS
  };

  TimeSource timeSource = TimeSource::NTP;
  std::string ntpServer = "pool.ntp.org";
  int16_t timezoneOffset = 0;  // Minutes from UTC

  // Location Sources
  enum class LocationSource {
    MANUAL,   // User-specified grid square
    GNSS      // From GPS
  };

  LocationSource locationSource = LocationSource::MANUAL;

  // WiFi Configuration
  std::string wifiSsid = "";
  std::string wifiPassword = "";
  std::string hostname = "wspr-ease";

  // Web UI Authentication
  std::string webUsername = "admin";
  std::string webPassword = "wspr";

  // Advanced Settings
  bool randomOffset = true;        // Add ±1s random offset
  uint16_t paTempLimitC = 85;      // PA temperature limit (Celsius)
  uint16_t cooldownSec = 120;      // Cooldown after transmission
  bool enableBeacon = true;        // Master enable/disable

  // Default band frequencies (dial frequency for WSPR sub-band)
  static constexpr uint32_t BAND_FREQS[NUM_BANDS] = {
    3568600,   // 80m
    5287200,   // 60m
    7038600,   // 40m
    10138700,  // 30m
    14095600,  // 20m
    18104600,  // 17m
    21094600,  // 15m
    24924600,  // 12m
    28124600   // 10m
  };

  static constexpr const char* BAND_NAMES[NUM_BANDS] = {
    "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m"
  };

  /**
   * @brief Initialize with default band frequencies
   */
  void initDefaults() {
    for (int i = 0; i < NUM_BANDS; i++) {
      bands[i].freqHz = BAND_FREQS[i];
      bands[i].enabled = (i == 4);  // Default: 20m only
      bands[i].priority = 100;
    }
  }

  /**
   * @brief Validate configuration
   *
   * @return Empty string if valid, error message otherwise
   */
  std::string validate() const {
    if (callsign.empty() || callsign == "NOCALL") {
      return "Callsign not set";
    }

    if (gridSquare.length() != 4) {
      return "Grid square must be 4 characters";
    }

    bool anyBandEnabled = false;
    for (const auto& band : bands) {
      if (band.enabled) {
        anyBandEnabled = true;
        if (band.freqHz < 1000000 || band.freqHz > 30000000) {
          return "Invalid band frequency";
        }
      }
    }

    if (!anyBandEnabled && mode != Mode::MANUAL) {
      return "No bands enabled";
    }

    return "";  // Valid
  }
};
