/*
 * WiFi Manager Implementation for WSPR-ease
 */

#include "wifiManager.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/settings/settings.h>

#include <cstring>
#include "logmanager.hpp"

LOG_MODULE_REGISTER(wifiMgr, LOG_LEVEL_INF);

namespace wspr {

  // Register subsystem with LogManager
  static Logger& logger = LogManager::instance().registerSubsystem("wifi", 
      {"mgmt", "dhcp", "rssi", "ap", "init"});

  static struct net_mgmt_event_callback wifiCB;
  static struct net_mgmt_event_callback ipv4CB;

  static K_SEM_DEFINE(wifiConnectedSem, 0, 1);
  static K_SEM_DEFINE(wifiDisconnectedSem, 0, 1);
  static K_SEM_DEFINE(ipAcquiredSem, 0, 1);

  // AP mode configuration
#define AP_IP_ADDR      "192.168.4.1"
#define AP_NETMASK      "255.255.255.0"
#define AP_DHCP_START   "192.168.4.10"
#define AP_DHCP_END     "192.168.4.50"

  // C-style callback that forwards to WifiManager
  static void wifiEventHandler(struct net_mgmt_event_callback* cb,
				 uint64_t mgmtEvent,
				 struct net_if* iface) {
    WifiManager::instance().onWifiEvent(mgmtEvent, iface);
  }

  WifiManager& WifiManager::instance() {
    static WifiManager inst;
    return inst;
  }

  void WifiManager::onWifiEvent(uint64_t mgmtEvent, struct net_if* iface) {
    if (mgmtEvent == NET_EVENT_WIFI_CONNECT_RESULT) {
      const struct wifi_status* status = (const struct wifi_status*)wifiCB.info;
      if (status->status == 0) {
	logger.inf("mgmt", "WiFi connected");
	state = WifiState::Connected;
      } else {
	logger.err("mgmt", "WiFi connection failed: %d", status->status);
	state = WifiState::Disconnected;
      }
      k_sem_give(&wifiConnectedSem);
    } else if (mgmtEvent == NET_EVENT_WIFI_DISCONNECT_RESULT) {
      // Ignore disconnect events while connecting - ESP32 driver quirk
      if (state == WifiState::Connecting) {
	logger.dbg("mgmt", "Ignoring disconnect during connection attempt");
	return;
      }
      logger.inf("mgmt", "WiFi disconnected");
      state = WifiState::Disconnected;
      ipAddr[0] = '\0';
      k_sem_give(&wifiDisconnectedSem);
    } else if (mgmtEvent == NET_EVENT_IPV4_ADDR_ADD) {
      char buf[NET_IPV4_ADDR_LEN];
      struct net_if_ipv4* ipv4 = iface->config.ip.ipv4;
      if (ipv4) {
	net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr,
		      buf, sizeof(buf));
	strncpy(ipAddr, buf, sizeof(ipAddr) - 1);
	logger.inf("dhcp", "Got IP address: %s", ipAddr);
	k_sem_give(&ipAcquiredSem);
      }
    } else if (mgmtEvent == NET_EVENT_WIFI_AP_ENABLE_RESULT) {
      logger.inf("ap", "WiFi AP enabled");
    } else if (mgmtEvent == NET_EVENT_WIFI_AP_DISABLE_RESULT) {
      logger.inf("ap", "WiFi AP disabled");
    } else if (mgmtEvent == NET_EVENT_WIFI_AP_STA_CONNECTED) {
      logger.inf("ap", "Station connected to AP");
    } else if (mgmtEvent == NET_EVENT_WIFI_AP_STA_DISCONNECTED) {
      logger.inf("ap", "Station disconnected from AP");
    }
  }

  int WifiManager::init() {
    logger.inf("init", "Initializing WiFi manager");

    // Get the default WiFi interface
    iface = net_if_get_default();
    if (!iface) {
      logger.err("init", "No network interface found");
      return -ENODEV;
    }

    // Register for WiFi events
    net_mgmt_init_event_callback(&wifiCB, wifiEventHandler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT |
                                 NET_EVENT_WIFI_AP_ENABLE_RESULT |
                                 NET_EVENT_WIFI_AP_DISABLE_RESULT |
                                 NET_EVENT_WIFI_AP_STA_CONNECTED |
                                 NET_EVENT_WIFI_AP_STA_DISCONNECTED);
    net_mgmt_add_event_callback(&wifiCB);

    // Register for IP events
    net_mgmt_init_event_callback(&ipv4CB, wifiEventHandler,
                                 NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4CB);

    logger.inf("init", "WiFi manager initialized");
    return 0;
  }

  int WifiManager::connect(const char* ssid, const char* password) {
    if (!iface) {
      logger.err("mgmt", "WiFi not initialized");
      return -ENODEV;
    }

    // If already connected, check if it's to the same network with valid IP
    if (state == WifiState::Connected) {
      // Check for valid IP (not empty and not 0.0.0.0)
      bool hasValidIP = ipAddr[0] != '\0' && strcmp(ipAddr, "0.0.0.0") != 0;
      if (strcmp(currentSSID, ssid) == 0 && hasValidIP) {
	logger.inf("mgmt", "Already connected to %s with IP %s", ssid, ipAddr);
	return 0;
      }
      // Disconnect first if no valid IP or connecting to different network
      logger.inf("mgmt", "Disconnecting (ssid=%s, validIP=%s)",
	      (strcmp(currentSSID, ssid) == 0) ? "yes" : "no",
	      hasValidIP ? "yes" : "no");
      disconnect();
      k_sleep(K_MSEC(500));
    }

    logger.inf("mgmt", "Connecting to WiFi SSID: %s", ssid);
    state = WifiState::Connecting;

    // Reset semaphores before connecting
    k_sem_reset(&wifiConnectedSem);
    k_sem_reset(&ipAcquiredSem);

    struct wifi_connect_req_params params = {};
    params.ssid = (uint8_t*)ssid;
    params.ssid_length = strlen(ssid);
    params.psk = (uint8_t*)password;
    params.psk_length = strlen(password);
    params.channel = WIFI_CHANNEL_ANY;
    params.security = password[0] ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
    params.band = WIFI_FREQ_BAND_UNKNOWN;
    params.mfp = WIFI_MFP_OPTIONAL;

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (ret) {
      logger.err("mgmt", "WiFi connect request failed: %d", ret);
      state = WifiState::Disconnected;
      return ret;
    }

    // Save current SSID
    strncpy(currentSSID, ssid, sizeof(currentSSID) - 1);

    // Wait for connection with timeout
    ret = k_sem_take(&wifiConnectedSem, K_SECONDS(30));
    if (ret) {
      logger.err("mgmt", "WiFi connection timeout");
      state = WifiState::Disconnected;
      return -ETIMEDOUT;
    }

    if (state != WifiState::Connected) {
      return -ECONNREFUSED;
    }

    // Start DHCP client to get IP address
    logger.inf("dhcp", "Starting DHCP client...");
    net_dhcpv4_start(iface);

    // Wait for DHCP to assign IP address
    ret = k_sem_take(&ipAcquiredSem, K_SECONDS(30));
    if (ret) {
      logger.wrn("dhcp", "DHCP timeout, no IP address assigned");
      return -ETIMEDOUT;
    }

    return 0;
  }

  int WifiManager::startAP(const char* ssid, const char* password) {
    if (!iface) {
      logger.err("ap", "WiFi not initialized");
      return -ENODEV;
    }

    logger.inf("ap", "Starting AP mode: %s", ssid);

    // Configure static IP for AP mode
    struct in_addr addr, netmask;
    net_addr_pton(AF_INET, AP_IP_ADDR, &addr);
    net_addr_pton(AF_INET, AP_NETMASK, &netmask);

    // Remove any existing addresses
    struct net_if_addr* ifaddr;
    for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
      ifaddr = &iface->config.ip.ipv4->unicast[i].ipv4;
      if (ifaddr->is_used) {
	net_if_ipv4_addr_rm(iface, &ifaddr->address.in_addr);
      }
    }

    // Add AP IP address
    if (!net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0)) {
      logger.err("ap", "Failed to add AP IP address");
      return -EFAULT;
    }

    // Set netmask
    net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);

    logger.inf("ap", "AP IP configured: %s", AP_IP_ADDR);

    // Enable AP mode
    struct wifi_connect_req_params params = {};
    params.ssid = (uint8_t*)ssid;
    params.ssid_length = strlen(ssid);
    params.psk = (uint8_t*)password;
    params.psk_length = strlen(password);
    params.channel = 6;  // Fixed channel for AP
    params.security = (password && password[0]) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &params, sizeof(params));
    if (ret) {
      logger.err("ap", "Failed to enable AP: %d", ret);
      return ret;
    }

    // Start DHCP server
    struct in_addr dhcp_start, dhcp_end;
    net_addr_pton(AF_INET, AP_DHCP_START, &dhcp_start);
    net_addr_pton(AF_INET, AP_DHCP_END, &dhcp_end);

    ret = net_dhcpv4_server_start(iface, &dhcp_start);
    if (ret) {
      logger.wrn("dhcp", "Failed to start DHCP server: %d (may already be running)", ret);
    } else {
      logger.inf("dhcp", "DHCP server started (%s - %s)", AP_DHCP_START, AP_DHCP_END);
    }

    state = WifiState::ApMode;
    strncpy(currentSSID, ssid, sizeof(currentSSID) - 1);
    strncpy(ipAddr, AP_IP_ADDR, sizeof(ipAddr) - 1);

    logger.inf("ap", "AP mode started: SSID=%s IP=%s", ssid, ipAddr);
    return 0;
  }

  void WifiManager::disconnect() {
    if (!iface || state == WifiState::Disconnected) {
      return;
    }

    logger.inf("mgmt", "Disconnecting WiFi");

    if (state == WifiState::ApMode) {
      // Stop DHCP server first
      net_dhcpv4_server_stop(iface);
      net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
    } else {
      net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
      k_sem_take(&wifiDisconnectedSem, K_SECONDS(5));
    }

    state = WifiState::Disconnected;
    ipAddr[0] = '\0';
    currentSSID[0] = '\0';
  }

  int8_t WifiManager::getRSSI() const {
    if (!iface || state != WifiState::Connected) {
      return 0;
    }

    struct wifi_iface_status status = {};
    if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status)) == 0) {
      return status.rssi;
    }
    return 0;
  }
} // namespace wspr
