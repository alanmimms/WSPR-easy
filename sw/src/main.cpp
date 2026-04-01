/*
 * WSPR-ease Main Application
 *
 * Multi-band WSPR beacon controller for ESP32-S3 + iCE40 FPGA
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

#include "wifiManager.hpp"
#include "webserver.hpp"
#include "captiveDNS.hpp"
#include "gnss.hpp"
#include "fpga.hpp"
#include "filesystem.hpp"

LOG_MODULE_REGISTER(wspr_ease, LOG_LEVEL_INF);

// WiFi retry configuration
#define WIFI_RETRY_DELAY_SECONDS  10
#define WIFI_MAX_RETRIES          0   // 0 = infinite retries

// Print a banner with connection info
static void printConnectionBanner(const char* ip)
{
    printk("\n");
    printk("==============================================\n");
    printk("  WSPR-ease Ready\n");
    printk("  Mode: WiFi Client\n");
    printk("  SSID: %s\n", CONFIG_WSPR_WIFI_SSID);
    printk("  Web UI: http://%s\n", ip);
    printk("==============================================\n");
    printk("\n");
}

// Try to connect to WiFi with retries
// Returns true if connected, false if no SSID configured
static bool wifiConnectWithRetry()
{
    auto& wifi = wspr::WifiManager::instance();

    const char* ssid = CONFIG_WSPR_WIFI_SSID;
    const char* pass = CONFIG_WSPR_WIFI_PASSWORD;

    if (ssid[0] == '\0') {
        LOG_ERR("No WiFi SSID configured! Set CONFIG_WSPR_WIFI_SSID in prj.conf");
        return false;
    }

    int retryCount = 0;

    while (WIFI_MAX_RETRIES == 0 || retryCount < WIFI_MAX_RETRIES) {
        if (retryCount > 0) {
            LOG_INF("WiFi retry %d, waiting %d seconds...",
                    retryCount, WIFI_RETRY_DELAY_SECONDS);
            k_sleep(K_SECONDS(WIFI_RETRY_DELAY_SECONDS));
        }

        LOG_INF("Connecting to WiFi: %s", ssid);

        if (wifi.connect(ssid, pass) == 0) {
            LOG_INF("WiFi connected, IP: %s", wifi.getIPAddress());
            return true;
        }

        LOG_WRN("WiFi connection failed");
        retryCount++;
    }

    return false;
}

static void initSubsystems(wspr::WebServer& webServer)
{
    auto& wifi = wspr::WifiManager::instance();
    auto& gnss = wspr::GNSS::instance();
    auto& fpga = wspr::FPGA::instance();
    auto& fs = wspr::FileSystem::instance();

    // Mount LittleFS early - doesn't need network
    fs.mount();

    // Initialize GNSS (stub mode)
    if (gnss.init() != 0) {
        LOG_ERR("GNSS init failed");
    }

    // Initialize FPGA (stub mode)
    if (fpga.init() != 0) {
        LOG_ERR("FPGA init failed");
    }

    // Initialize WiFi
    if (wifi.init() != 0) {
        LOG_ERR("WiFi init failed");
        return;
    }

    // Connect to WiFi (with retries)
    wifiConnectWithRetry();

    // Initialize and start web server
    if (webServer.init() == 0) {
        webServer.start(80);
        // Print connection banner after everything is ready
        if (wifi.isConnected()) {
            printConnectionBanner(wifi.getIPAddress());
        }
    } else {
        LOG_ERR("Web server init failed");
    }
}

int main(void)
{
    LOG_INF("WSPR-ease starting...");
    LOG_INF("Build: %s %s", __DATE__, __TIME__);

    // Create WebServer on main's stack (persists for lifetime of program)
    wspr::WebServer webServer;

    // Wait for network interface to be ready
    k_sleep(K_SECONDS(2));

    initSubsystems(webServer);

    LOG_INF("Entering main loop");

    auto& wifi = wspr::WifiManager::instance();
    auto& gnss = wspr::GNSS::instance();
    auto& fpga = wspr::FPGA::instance();

    uint32_t loopCount = 0;
    bool wasConnected = wifi.isConnected();

    while (1) {
        // Check for scheduled WSPR transmission
        // (In real implementation, this would check schedule and start TX)
        if (gnss.isTXSlot() && !fpga.isTransmitting()) {
            LOG_INF("TX slot detected (not transmitting in stub mode)");
        }

        // Monitor WiFi connection and reconnect if needed
        bool isConnected = wifi.isConnected();
        if (wasConnected && !isConnected) {
            LOG_WRN("WiFi disconnected! Attempting to reconnect...");
            if (wifiConnectWithRetry()) {
                printConnectionBanner(wifi.getIPAddress());
            }
        }
        wasConnected = wifi.isConnected();

        // Periodic status log
        if ((loopCount % 60) == 0) {  // Every 60 seconds
            LOG_INF("Status: WiFi=%s IP=%s RSSI=%d GNSS=%s Freq=%u",
                    wifi.isConnected() ? "connected" : "disconnected",
                    wifi.getIPAddress(),
                    wifi.getRSSI(),
                    gnss.hasFix() ? "fix" : "no fix",
                    fpga.frequency());
        }

        loopCount++;
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
