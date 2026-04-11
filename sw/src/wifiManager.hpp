/*
 * WiFi Manager for WSPR-ease
 * Handles WiFi connection and AP mode fallback
 */

#pragma once

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

#include <cstdint>

namespace wspr {

  enum class WifiState {
    Disconnected,
    Connecting,
    Connected,
    ApMode
  };

  struct WifiConfig {
    char ssid[33];
    char password[65];
    bool ap_mode_enabled;
    char ap_ssid[33];
    char ap_password[65];
  };

  class WifiManager {
  public:
    static WifiManager& instance();

    int init();
    int connect(const char* ssid, const char* password);
    int startAP(const char* ssid, const char* password);
    void disconnect();

    WifiState getState() const { return state; }
    bool isConnected() const { return state == WifiState::Connected; }

    const char* getIPAddress() const { return ipAddr; }
    const char* getSSID() const { return currentSSID; }
    int8_t getRSSI() const;

    // Called from C callback
    void onWifiEvent(uint64_t mgmt_event, struct net_if* iface);

  private:
    WifiManager() = default;

    WifiState state = WifiState::Disconnected;
    WifiConfig config = {};
    char ipAddr[16] = "0.0.0.0";
    char currentSSID[33] = "";
    struct net_if* iface = nullptr;
  };

} // namespace wspr
