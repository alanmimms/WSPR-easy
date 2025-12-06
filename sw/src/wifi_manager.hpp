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
    int start_ap(const char* ssid, const char* password);
    void disconnect();

    WifiState state() const { return state_; }
    bool is_connected() const { return state_ == WifiState::Connected; }

    const char* ip_address() const { return ip_addr_; }
    const char* ssid() const { return current_ssid_; }
    int8_t rssi() const;

    // Load/save credentials to NVS
    int load_config();
    int save_config(const WifiConfig& config);
    const WifiConfig& config() const { return config_; }

    // Called from C callback
    void on_wifi_event(uint64_t mgmt_event, struct net_if* iface);

private:
    WifiManager() = default;

    WifiState state_ = WifiState::Disconnected;
    WifiConfig config_ = {};
    char ip_addr_[16] = "0.0.0.0";
    char current_ssid_[33] = "";
    struct net_if* iface_ = nullptr;
};

} // namespace wspr
