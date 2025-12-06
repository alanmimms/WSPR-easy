/*
 * WiFi Manager Implementation for WSPR-ease
 */

#include "wifi_manager.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/settings/settings.h>

#include <cstring>

LOG_MODULE_REGISTER(wifi_mgr, LOG_LEVEL_INF);

namespace wspr {

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static K_SEM_DEFINE(wifi_connected_sem, 0, 1);
static K_SEM_DEFINE(wifi_disconnected_sem, 0, 1);
static K_SEM_DEFINE(ip_acquired_sem, 0, 1);

// AP mode configuration
#define AP_IP_ADDR      "192.168.4.1"
#define AP_NETMASK      "255.255.255.0"
#define AP_DHCP_START   "192.168.4.10"
#define AP_DHCP_END     "192.168.4.50"

// C-style callback that forwards to WifiManager
static void wifi_event_handler(struct net_mgmt_event_callback* cb,
                               uint64_t mgmt_event,
                               struct net_if* iface) {
    WifiManager::instance().on_wifi_event(mgmt_event, iface);
}

WifiManager& WifiManager::instance() {
    static WifiManager inst;
    return inst;
}

void WifiManager::on_wifi_event(uint64_t mgmt_event, struct net_if* iface) {
    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status* status =
            (const struct wifi_status*)wifi_cb.info;
        if (status->status == 0) {
            LOG_INF("WiFi connected");
            state_ = WifiState::Connected;
        } else {
            LOG_ERR("WiFi connection failed: %d", status->status);
            state_ = WifiState::Disconnected;
        }
        k_sem_give(&wifi_connected_sem);
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        // Ignore disconnect events while connecting - ESP32 driver quirk
        if (state_ == WifiState::Connecting) {
            LOG_DBG("Ignoring disconnect during connection attempt");
            return;
        }
        LOG_INF("WiFi disconnected");
        state_ = WifiState::Disconnected;
        ip_addr_[0] = '\0';
        k_sem_give(&wifi_disconnected_sem);
    } else if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        char buf[NET_IPV4_ADDR_LEN];
        struct net_if_ipv4* ipv4 = iface->config.ip.ipv4;
        if (ipv4) {
            net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr,
                          buf, sizeof(buf));
            strncpy(ip_addr_, buf, sizeof(ip_addr_) - 1);
            LOG_INF("Got IP address: %s", ip_addr_);
            k_sem_give(&ip_acquired_sem);
        }
    } else if (mgmt_event == NET_EVENT_WIFI_AP_ENABLE_RESULT) {
        LOG_INF("WiFi AP enabled");
    } else if (mgmt_event == NET_EVENT_WIFI_AP_DISABLE_RESULT) {
        LOG_INF("WiFi AP disabled");
    } else if (mgmt_event == NET_EVENT_WIFI_AP_STA_CONNECTED) {
        LOG_INF("Station connected to AP");
    } else if (mgmt_event == NET_EVENT_WIFI_AP_STA_DISCONNECTED) {
        LOG_INF("Station disconnected from AP");
    }
}

int WifiManager::init() {
    LOG_INF("Initializing WiFi manager");

    // Get the default WiFi interface
    iface_ = net_if_get_default();
    if (!iface_) {
        LOG_ERR("No network interface found");
        return -ENODEV;
    }

    // Register for WiFi events
    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT |
                                 NET_EVENT_WIFI_AP_ENABLE_RESULT |
                                 NET_EVENT_WIFI_AP_DISABLE_RESULT |
                                 NET_EVENT_WIFI_AP_STA_CONNECTED |
                                 NET_EVENT_WIFI_AP_STA_DISCONNECTED);
    net_mgmt_add_event_callback(&wifi_cb);

    // Register for IP events
    net_mgmt_init_event_callback(&ipv4_cb, wifi_event_handler,
                                 NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);

    // Load saved config
    load_config();

    LOG_INF("WiFi manager initialized");
    return 0;
}

int WifiManager::connect(const char* ssid, const char* password) {
    if (!iface_) {
        LOG_ERR("WiFi not initialized");
        return -ENODEV;
    }

    // If already connected, check if it's to the same network with valid IP
    if (state_ == WifiState::Connected) {
        // Check for valid IP (not empty and not 0.0.0.0)
        bool has_valid_ip = ip_addr_[0] != '\0' && strcmp(ip_addr_, "0.0.0.0") != 0;
        if (strcmp(current_ssid_, ssid) == 0 && has_valid_ip) {
            LOG_INF("Already connected to %s with IP %s", ssid, ip_addr_);
            return 0;
        }
        // Disconnect first if no valid IP or connecting to different network
        LOG_INF("Disconnecting (ssid_match=%s, valid_ip=%s)",
                (strcmp(current_ssid_, ssid) == 0) ? "yes" : "no",
                has_valid_ip ? "yes" : "no");
        disconnect();
        k_sleep(K_MSEC(500));
    }

    LOG_INF("Connecting to WiFi SSID: %s", ssid);
    state_ = WifiState::Connecting;

    // Reset semaphores before connecting
    k_sem_reset(&wifi_connected_sem);
    k_sem_reset(&ip_acquired_sem);

    struct wifi_connect_req_params params = {};
    params.ssid = (uint8_t*)ssid;
    params.ssid_length = strlen(ssid);
    params.psk = (uint8_t*)password;
    params.psk_length = strlen(password);
    params.channel = WIFI_CHANNEL_ANY;
    params.security = password[0] ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
    params.band = WIFI_FREQ_BAND_UNKNOWN;
    params.mfp = WIFI_MFP_OPTIONAL;

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface_, &params, sizeof(params));
    if (ret) {
        LOG_ERR("WiFi connect request failed: %d", ret);
        state_ = WifiState::Disconnected;
        return ret;
    }

    // Save current SSID
    strncpy(current_ssid_, ssid, sizeof(current_ssid_) - 1);

    // Wait for connection with timeout
    ret = k_sem_take(&wifi_connected_sem, K_SECONDS(30));
    if (ret) {
        LOG_ERR("WiFi connection timeout");
        state_ = WifiState::Disconnected;
        return -ETIMEDOUT;
    }

    if (state_ != WifiState::Connected) {
        return -ECONNREFUSED;
    }

    // Start DHCP client to get IP address
    LOG_INF("Starting DHCP client...");
    net_dhcpv4_start(iface_);

    // Wait for DHCP to assign IP address
    ret = k_sem_take(&ip_acquired_sem, K_SECONDS(30));
    if (ret) {
        LOG_WRN("DHCP timeout, no IP address assigned");
        return -ETIMEDOUT;
    }

    return 0;
}

int WifiManager::start_ap(const char* ssid, const char* password) {
    if (!iface_) {
        LOG_ERR("WiFi not initialized");
        return -ENODEV;
    }

    LOG_INF("Starting AP mode: %s", ssid);

    // Configure static IP for AP mode
    struct in_addr addr, netmask;
    net_addr_pton(AF_INET, AP_IP_ADDR, &addr);
    net_addr_pton(AF_INET, AP_NETMASK, &netmask);

    // Remove any existing addresses
    struct net_if_addr* ifaddr;
    for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
        ifaddr = &iface_->config.ip.ipv4->unicast[i].ipv4;
        if (ifaddr->is_used) {
            net_if_ipv4_addr_rm(iface_, &ifaddr->address.in_addr);
        }
    }

    // Add AP IP address
    if (!net_if_ipv4_addr_add(iface_, &addr, NET_ADDR_MANUAL, 0)) {
        LOG_ERR("Failed to add AP IP address");
        return -EFAULT;
    }

    // Set netmask
    net_if_ipv4_set_netmask_by_addr(iface_, &addr, &netmask);

    LOG_INF("AP IP configured: %s", AP_IP_ADDR);

    // Enable AP mode
    struct wifi_connect_req_params params = {};
    params.ssid = (uint8_t*)ssid;
    params.ssid_length = strlen(ssid);
    params.psk = (uint8_t*)password;
    params.psk_length = strlen(password);
    params.channel = 6;  // Fixed channel for AP
    params.security = (password && password[0]) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface_, &params, sizeof(params));
    if (ret) {
        LOG_ERR("Failed to enable AP: %d", ret);
        return ret;
    }

    // Start DHCP server
    struct in_addr dhcp_start, dhcp_end;
    net_addr_pton(AF_INET, AP_DHCP_START, &dhcp_start);
    net_addr_pton(AF_INET, AP_DHCP_END, &dhcp_end);

    ret = net_dhcpv4_server_start(iface_, &dhcp_start);
    if (ret) {
        LOG_WRN("Failed to start DHCP server: %d (may already be running)", ret);
    } else {
        LOG_INF("DHCP server started (%s - %s)", AP_DHCP_START, AP_DHCP_END);
    }

    state_ = WifiState::ApMode;
    strncpy(current_ssid_, ssid, sizeof(current_ssid_) - 1);
    strncpy(ip_addr_, AP_IP_ADDR, sizeof(ip_addr_) - 1);

    LOG_INF("AP mode started: SSID=%s IP=%s", ssid, ip_addr_);
    return 0;
}

void WifiManager::disconnect() {
    if (!iface_ || state_ == WifiState::Disconnected) {
        return;
    }

    LOG_INF("Disconnecting WiFi");

    if (state_ == WifiState::ApMode) {
        // Stop DHCP server first
        net_dhcpv4_server_stop(iface_);
        net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface_, NULL, 0);
    } else {
        net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface_, NULL, 0);
        k_sem_take(&wifi_disconnected_sem, K_SECONDS(5));
    }

    state_ = WifiState::Disconnected;
    ip_addr_[0] = '\0';
    current_ssid_[0] = '\0';
}

int8_t WifiManager::rssi() const {
    if (!iface_ || state_ != WifiState::Connected) {
        return 0;
    }

    struct wifi_iface_status status = {};
    if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface_, &status, sizeof(status)) == 0) {
        return status.rssi;
    }
    return 0;
}

int WifiManager::load_config() {
    LOG_INF("Loading WiFi config from NVS");

    // Initialize settings if not already done
    int ret = settings_subsys_init();
    if (ret) {
        LOG_ERR("Settings init failed: %d", ret);
        return ret;
    }

    ret = settings_load();
    if (ret) {
        LOG_WRN("Settings load failed: %d", ret);
    }

    return 0;
}

int WifiManager::save_config(const WifiConfig& config) {
    LOG_INF("Saving WiFi config to NVS");

    config_ = config;
    int ret = settings_save_one("wifi/config", &config_, sizeof(config_));
    if (ret) {
        LOG_ERR("Failed to save WiFi config: %d", ret);
    }
    return ret;
}

} // namespace wspr
