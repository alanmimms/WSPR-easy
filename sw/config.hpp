/* file: src/model/Configuration.hpp */
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

struct BandSchedule {
  std::string bandName;
  uint32_t frequencyHz;
  // ... scheduling times ...
};

/* Stored Configuration (User Intent) */
struct UserConfig {
  std::string callsign = "NOCALL";
  std::string gridSquare = ""; // If empty, auto-detect
  uint8_t powerDbm = 23;       // Default 23 dBm (200mW)
  // ...
};

/* Runtime State (Hardware Reality) */
struct GnssState {
  bool hasFix = false;
  double lat = 0.0;
  double lon = 0.0;
  std::string detectedGrid;
  int64_t lastPpsTimestamp = 0;
};

/* The Source of Truth */
class Configuration {
public:
  UserConfig user;
  GnssState gnss;

  // Logic: Return User Grid if set, otherwise GNSS Grid, otherwise Error
  std::string getEffectiveGrid() const {
    if (!user.gridSquare.empty()) {
      return user.gridSquare;
    }
    if (gnss.hasFix && !gnss.detectedGrid.empty()) {
      return gnss.detectedGrid;
    }
    return "AA00"; // Safe fallback
  }

  uint8_t getEffectivePower() const {
    // Implementation could clamp this based on Band voltage checks
    return user.powerDbm;
  }
    
  // Helper: Maidenhead Conversion
  static std::string latLonToGrid(double lat, double lon);
};
