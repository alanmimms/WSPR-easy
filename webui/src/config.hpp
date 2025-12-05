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

  // Time window for band eligibility
  struct TimeWindow {
    enum class TimeBase : uint8_t {
      UTC,      // Absolute UTC time
      LOCAL,    // User's local time (timezone offset applied)
      SUNRISE,  // Relative to sunrise at location
      SUNSET    // Relative to sunset at location
    };

    bool enabled = false;          // If false, band is always eligible (when band enabled)
    TimeBase startBase = TimeBase::UTC;
    int16_t startOffsetMin = 0;    // Minutes from base (negative OK for sunrise/sunset)
    TimeBase endBase = TimeBase::UTC;
    int16_t endOffsetMin = 1440;   // Minutes from base (1440 = 24h, i.e., end of day)
  };

  // Band Configuration (80m, 60m, 40m, 30m, 20m, 17m, 15m, 12m, 10m)
  struct BandConfig {
    bool enabled = false;
    uint32_t freqHz = 0;
    TimeWindow timeWindow;         // When this band is eligible
  };

  static constexpr int NUM_BANDS = 9;
  std::array<BandConfig, NUM_BANDS> bands;

  // Scheduling
  enum class Mode : uint8_t {
    MANUAL,      // Only transmit on manual trigger
    RANDOM,      // Random from eligible bands
    ROUND_ROBIN, // Sequential through eligible bands
    LIST         // Use bandList, skipping ineligible bands
  };

  Mode mode = Mode::ROUND_ROBIN;
  std::string bandList;            // For LIST mode: "20m,20m,40m,30m" (repeats for weighting)
  uint16_t slotIntervalMin = 10;   // Minutes between transmissions
  uint8_t dutyCycle = 1;           // 1 out of N slots (1 = every slot)

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
      bands[i].timeWindow.enabled = false;  // No time restriction by default
    }
    bandList = "20m";  // Default band list
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
