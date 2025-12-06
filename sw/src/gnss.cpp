/*
 * GNSS Module Implementation for WSPR-ease
 * Stub implementation for development without hardware
 */

#include "gnss.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cstdio>
#include <cmath>

LOG_MODULE_REGISTER(gnss, LOG_LEVEL_INF);

namespace wspr {

Gnss& Gnss::instance() {
    static Gnss inst;
    return inst;
}

int Gnss::init() {
    LOG_INF("Initializing GNSS module");

    if (stub_mode_) {
        LOG_WRN("GNSS running in STUB mode - no real hardware");

        // Simulate a location (middle of nowhere, Atlantic Ocean)
        data_.latitude = 40.0;
        data_.longitude = -74.0;
        data_.altitude = 10.0;
        data_.satellites = 8;
        data_.valid = true;

        // Use system uptime as base
        stub_base_time_ = 1733400000;  // Approximate Unix time

        compute_grid();
        format_time();

        LOG_INF("GNSS stub initialized: grid=%s", grid_);
    } else {
        // TODO: Initialize real UART connection to GNSS module
        // const struct device* uart = DEVICE_DT_GET(DT_ALIAS(gnss_uart));
        LOG_INF("Real GNSS initialization not yet implemented");
        return -ENOTSUP;
    }

    return 0;
}

void Gnss::update() {
    if (stub_mode_) {
        // Update simulated time based on system uptime
        int64_t uptime_sec = k_uptime_get() / 1000;
        int64_t sim_time = stub_base_time_ + uptime_sec;

        // Convert to broken-down time (simplified)
        int64_t day_sec = sim_time % 86400;

        data_.hour = day_sec / 3600;
        data_.minute = (day_sec % 3600) / 60;
        data_.second = day_sec % 60;

        // Approximate date (good enough for stub)
        data_.year = 2024;
        data_.month = 12;
        data_.day = 5;

        format_time();
    } else {
        // TODO: Parse NMEA sentences from UART
    }
}

int64_t Gnss::unix_time() const {
    if (stub_mode_) {
        return stub_base_time_ + (k_uptime_get() / 1000);
    }

    // TODO: Compute from data_ fields
    return 0;
}

bool Gnss::is_tx_slot() const {
    // WSPR transmissions start at even minutes
    return (data_.second == 0) && ((data_.minute % 2) == 0);
}

void Gnss::compute_grid() {
    // Maidenhead grid locator calculation
    double lon = data_.longitude + 180.0;
    double lat = data_.latitude + 90.0;

    grid_[0] = 'A' + (int)(lon / 20.0);
    grid_[1] = 'A' + (int)(lat / 10.0);
    grid_[2] = '0' + (int)(fmod(lon, 20.0) / 2.0);
    grid_[3] = '0' + (int)(fmod(lat, 10.0));
    grid_[4] = 'a' + (int)(fmod(lon, 2.0) * 12.0);
    grid_[5] = 'a' + (int)(fmod(lat, 1.0) * 24.0);
    grid_[6] = '\0';
}

void Gnss::format_time() {
    snprintf(time_str_, sizeof(time_str_), "%02d:%02d:%02d",
             data_.hour, data_.minute, data_.second);
}

} // namespace wspr
