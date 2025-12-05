#pragma once

#include "config.hpp"
#include "fs_hal.hpp"
#include <string>
#include <sstream>
#include <iomanip>

/**
 * @brief Configuration manager with JSON serialization
 *
 * Handles loading/saving configuration to filesystem as JSON.
 * Uses simple JSON formatting to avoid external dependencies.
 */
class ConfigManager {
public:
  ConfigManager(HAL::IFilesystem* fs, const std::string& configPath = "/config.json")
    : filesystem(fs), path(configPath) {
    config.initDefaults();
  }

  /**
   * @brief Load configuration from filesystem
   *
   * @return true on success
   */
  bool load() {
    std::string jsonStr;
    if (!filesystem->readFile(path, jsonStr)) {
      return false;
    }

    return fromJson(jsonStr);
  }

  /**
   * @brief Save configuration to filesystem
   *
   * @return true on success
   */
  bool save() {
    std::string jsonStr = toJson();
    return filesystem->writeFile(path, jsonStr);
  }

  /**
   * @brief Save to specific backup slot
   *
   * @param slot Backup slot number (1-3)
   * @return true on success
   */
  bool saveBackup(int slot) {
    if (slot < 1 || slot > 3) return false;
    std::string backupPath = "/config.backup" + std::to_string(slot) + ".json";
    std::string jsonStr = toJson();
    return filesystem->writeFile(backupPath, jsonStr);
  }

  /**
   * @brief Load from specific backup slot
   *
   * @param slot Backup slot number (1-3)
   * @return true on success
   */
  bool loadBackup(int slot) {
    if (slot < 1 || slot > 3) return false;
    std::string backupPath = "/config.backup" + std::to_string(slot) + ".json";
    std::string jsonStr;
    if (!filesystem->readFile(backupPath, jsonStr)) {
      return false;
    }
    return fromJson(jsonStr);
  }

  /**
   * @brief Reset to default configuration
   */
  void reset() {
    config = WSPRConfig();
    config.initDefaults();
  }

  /**
   * @brief Get current configuration
   */
  const WSPRConfig& getConfig() const { return config; }

  /**
   * @brief Update configuration
   */
  void setConfig(const WSPRConfig& newConfig) { config = newConfig; }

  /**
   * @brief Serialize configuration to JSON string
   */
  std::string toJson() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"callsign\": \"" << escapeJson(config.callsign) << "\",\n";
    json << "  \"gridSquare\": \"" << escapeJson(config.gridSquare) << "\",\n";
    json << "  \"powerDbm\": " << (int)config.powerDbm << ",\n";

    json << "  \"bands\": [\n";
    for (int i = 0; i < WSPRConfig::NUM_BANDS; i++) {
      const auto& band = config.bands[i];
      const auto& tw = band.timeWindow;
      json << "    {\n";
      json << "      \"name\": \"" << WSPRConfig::BAND_NAMES[i] << "\",\n";
      json << "      \"enabled\": " << (band.enabled ? "true" : "false") << ",\n";
      json << "      \"freqHz\": " << band.freqHz << ",\n";
      json << "      \"timeWindow\": {\n";
      json << "        \"enabled\": " << (tw.enabled ? "true" : "false") << ",\n";
      json << "        \"startBase\": \"" << timeBaseToString(tw.startBase) << "\",\n";
      json << "        \"startOffsetMin\": " << tw.startOffsetMin << ",\n";
      json << "        \"endBase\": \"" << timeBaseToString(tw.endBase) << "\",\n";
      json << "        \"endOffsetMin\": " << tw.endOffsetMin << "\n";
      json << "      }\n";
      json << "    }" << (i < WSPRConfig::NUM_BANDS - 1 ? "," : "") << "\n";
    }
    json << "  ],\n";

    json << "  \"mode\": \"" << modeToString(config.mode) << "\",\n";
    json << "  \"bandList\": \"" << escapeJson(config.bandList) << "\",\n";
    json << "  \"slotIntervalMin\": " << config.slotIntervalMin << ",\n";
    json << "  \"dutyCycle\": " << (int)config.dutyCycle << ",\n";

    json << "  \"timeSource\": \"" << timeSourceToString(config.timeSource) << "\",\n";
    json << "  \"ntpServer\": \"" << escapeJson(config.ntpServer) << "\",\n";
    json << "  \"timezoneOffset\": " << config.timezoneOffset << ",\n";

    json << "  \"locationSource\": \"" << locationSourceToString(config.locationSource) << "\",\n";

    json << "  \"wifi\": {\n";
    json << "    \"ssid\": \"" << escapeJson(config.wifiSsid) << "\",\n";
    json << "    \"password\": \"" << escapeJson(config.wifiPassword) << "\",\n";
    json << "    \"hostname\": \"" << escapeJson(config.hostname) << "\"\n";
    json << "  },\n";

    json << "  \"webAuth\": {\n";
    json << "    \"username\": \"" << escapeJson(config.webUsername) << "\",\n";
    json << "    \"password\": \"" << escapeJson(config.webPassword) << "\"\n";
    json << "  },\n";

    json << "  \"advanced\": {\n";
    json << "    \"randomOffset\": " << (config.randomOffset ? "true" : "false") << ",\n";
    json << "    \"paTempLimitC\": " << config.paTempLimitC << ",\n";
    json << "    \"cooldownSec\": " << config.cooldownSec << ",\n";
    json << "    \"enableBeacon\": " << (config.enableBeacon ? "true" : "false") << "\n";
    json << "  }\n";

    json << "}\n";
    return json.str();
  }

  /**
   * @brief Deserialize JSON string to configuration
   *
   * Note: This is a simple parser. For production, use nlohmann/json or similar.
   */
  bool fromJson(const std::string& jsonStr) {
    // TODO: Implement JSON parsing
    // For now, return false - will add proper parsing next
    return false;
  }

private:
  HAL::IFilesystem* filesystem;
  std::string path;
  WSPRConfig config;

  static std::string escapeJson(const std::string& str) {
    std::ostringstream escaped;
    for (char c : str) {
      switch (c) {
        case '"':  escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:   escaped << c; break;
      }
    }
    return escaped.str();
  }

  static std::string modeToString(WSPRConfig::Mode mode) {
    switch (mode) {
      case WSPRConfig::Mode::MANUAL: return "manual";
      case WSPRConfig::Mode::RANDOM: return "random";
      case WSPRConfig::Mode::ROUND_ROBIN: return "round-robin";
      case WSPRConfig::Mode::LIST: return "list";
      default: return "round-robin";
    }
  }

  static std::string timeBaseToString(WSPRConfig::TimeWindow::TimeBase base) {
    switch (base) {
      case WSPRConfig::TimeWindow::TimeBase::UTC: return "utc";
      case WSPRConfig::TimeWindow::TimeBase::LOCAL: return "local";
      case WSPRConfig::TimeWindow::TimeBase::SUNRISE: return "sunrise";
      case WSPRConfig::TimeWindow::TimeBase::SUNSET: return "sunset";
      default: return "utc";
    }
  }

  static std::string timeSourceToString(WSPRConfig::TimeSource src) {
    switch (src) {
      case WSPRConfig::TimeSource::SYSTEM: return "system";
      case WSPRConfig::TimeSource::NTP: return "ntp";
      case WSPRConfig::TimeSource::GNSS: return "gnss";
      default: return "ntp";
    }
  }

  static std::string locationSourceToString(WSPRConfig::LocationSource src) {
    switch (src) {
      case WSPRConfig::LocationSource::MANUAL: return "manual";
      case WSPRConfig::LocationSource::GNSS: return "gnss";
      default: return "manual";
    }
  }
};
