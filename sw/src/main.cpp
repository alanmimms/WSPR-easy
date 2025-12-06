/*
 * WSPR-ease Main Application
 *
 * Multi-band WSPR beacon controller for ESP32-S3 + iCE40 FPGA
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

#include "wifi_manager.hpp"
#include "webserver.hpp"
#include "captive_dns.hpp"
#include "gnss.hpp"
#include "fpga.hpp"

LOG_MODULE_REGISTER(wspr_ease, LOG_LEVEL_INF);

// WiFi retry configuration
#define WIFI_RETRY_DELAY_SECONDS  10
#define WIFI_MAX_RETRIES          0   // 0 = infinite retries

// Print a banner with connection info
static void print_connection_banner(const char* ip)
{
    printk("\n");
    printk("╔════════════════════════════════════════════╗\n");
    printk("║           WSPR-ease Ready                  ║\n");
    printk("╠════════════════════════════════════════════╣\n");
    printk("║  Mode: WiFi Client                         ║\n");
    printk("║  SSID: %-36s ║\n", CONFIG_WSPR_WIFI_SSID);
    printk("╠════════════════════════════════════════════╣\n");
    printk("║  Web UI: http://%-26s ║\n", ip);
    printk("╚════════════════════════════════════════════╝\n");
    printk("\n");
}

// Try to connect to WiFi with retries
// Returns true if connected, false if no SSID configured
static bool wifi_connect_with_retry()
{
    auto& wifi = wspr::WifiManager::instance();

    const char* ssid = CONFIG_WSPR_WIFI_SSID;
    const char* pass = CONFIG_WSPR_WIFI_PASSWORD;

    if (ssid[0] == '\0') {
        LOG_ERR("No WiFi SSID configured! Set CONFIG_WSPR_WIFI_SSID in prj.conf");
        return false;
    }

    int retry_count = 0;

    while (WIFI_MAX_RETRIES == 0 || retry_count < WIFI_MAX_RETRIES) {
        if (retry_count > 0) {
            LOG_INF("WiFi retry %d, waiting %d seconds...",
                    retry_count, WIFI_RETRY_DELAY_SECONDS);
            k_sleep(K_SECONDS(WIFI_RETRY_DELAY_SECONDS));
        }

        LOG_INF("Connecting to WiFi: %s", ssid);

        if (wifi.connect(ssid, pass) == 0) {
            LOG_INF("WiFi connected, IP: %s", wifi.ip_address());
            return true;
        }

        LOG_WRN("WiFi connection failed");
        retry_count++;
    }

    return false;
}

static void init_subsystems()
{
    auto& wifi = wspr::WifiManager::instance();
    auto& web = wspr::WebServer::instance();
    auto& gnss = wspr::Gnss::instance();
    auto& fpga = wspr::Fpga::instance();

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
    wifi_connect_with_retry();

    // Initialize and start web server
    if (web.init() == 0) {
        web.start(80);
        // Print connection banner after everything is ready
        if (wifi.is_connected()) {
            print_connection_banner(wifi.ip_address());
        }
    } else {
        LOG_ERR("Web server init failed");
    }
}

int main(void)
{
    LOG_INF("WSPR-ease starting...");
    LOG_INF("Build: %s %s", __DATE__, __TIME__);

    // Wait for network interface to be ready
    k_sleep(K_SECONDS(2));

    init_subsystems();

    LOG_INF("Entering main loop");

    auto& wifi = wspr::WifiManager::instance();
    auto& gnss = wspr::Gnss::instance();
    auto& fpga = wspr::Fpga::instance();

    uint32_t loop_count = 0;
    bool was_connected = wifi.is_connected();

    while (1) {
        // Update GNSS data
        gnss.update();

        // Check for scheduled WSPR transmission
        // (In real implementation, this would check schedule and start TX)
        if (gnss.is_tx_slot() && !fpga.is_transmitting()) {
            LOG_INF("TX slot detected (not transmitting in stub mode)");
        }

        // Monitor WiFi connection and reconnect if needed
        bool is_connected = wifi.is_connected();
        if (was_connected && !is_connected) {
            LOG_WRN("WiFi disconnected! Attempting to reconnect...");
            if (wifi_connect_with_retry()) {
                print_connection_banner(wifi.ip_address());
            }
        }
        was_connected = wifi.is_connected();

        // Periodic status log
        if ((loop_count % 60) == 0) {  // Every 60 seconds
            LOG_INF("Status: WiFi=%s IP=%s GNSS=%s Freq=%u",
                    wifi.is_connected() ? "connected" : "disconnected",
                    wifi.ip_address(),
                    gnss.has_fix() ? "fix" : "no fix",
                    fpga.frequency());
        }

        loop_count++;
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
