/*
 * Captive Portal DNS Server for WSPR-ease
 * Responds to all DNS queries with the AP IP address
 */

#pragma once

#include <cstdint>

namespace wspr {

class CaptiveDns {
public:
    static CaptiveDns& instance();

    int start(const char* redirect_ip);
    void stop();

    bool is_running() const { return running_; }

private:
    CaptiveDns() = default;

    bool running_ = false;
    uint32_t redirect_ip_ = 0;
};

} // namespace wspr
