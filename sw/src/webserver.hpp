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
    WebServer() = default;

    // Mount LittleFS - can be called early, before network is ready
    int mount_filesystem();

    int init();
    int start(uint16_t port = 80);
    void stop();

    bool is_running() const { return running_; }

private:
    bool running_ = false;
};

} // namespace wspr
