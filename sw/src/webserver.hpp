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

    int init();
    int start(uint16_t port = 80);
    void stop();

    bool isRunning() const { return running; }

private:
    bool running = false;
};

} // namespace wspr
