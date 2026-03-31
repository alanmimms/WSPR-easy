/*
 * Captive Portal DNS Server for WSPR-ease
 * Responds to all DNS queries with the AP IP address
 */

#pragma once

#include <cstdint>

namespace wspr {

class CaptiveDNS {
public:
    static CaptiveDNS& instance();

    int start(const char* redirectIP);
    void stop();

    bool isRunning() const { return running; }

private:
    CaptiveDNS() = default;

    bool running = false;
    uint32_t redirectIP = 0;
};

} // namespace wspr
