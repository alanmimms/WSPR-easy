/*
 * HTTP Web Server for WSPR-ease
 * Serves static files and REST API endpoints
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace wspr {

class WebServer {
public:
    static WebServer& instance();

    int init();
    int start(uint16_t port = 80);
    void stop();

    bool is_running() const { return running_; }

private:
    WebServer() = default;

    // API handlers
    static int handle_api_status(void* ctx);
    static int handle_api_config_get(void* ctx);
    static int handle_api_config_post(void* ctx);
    static int handle_api_wifi_scan(void* ctx);
    static int handle_api_wifi_connect(void* ctx);
    static int handle_api_transmit(void* ctx);

    // Static file handler
    static int handle_static_file(void* ctx, const char* path);

    bool running_ = false;
};

} // namespace wspr
