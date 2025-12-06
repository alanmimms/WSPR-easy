/*
 * GNSS Module for WSPR-ease
 * Handles GPS/GNSS receiver for time sync and location
 */

#pragma once

#include <cstdint>

namespace wspr {

struct GnssData {
    bool valid;
    double latitude;
    double longitude;
    double altitude;
    uint8_t satellites;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t year;
    uint8_t month;
    uint8_t day;
};

class Gnss {
public:
    static Gnss& instance();

    int init();
    void update();

    bool has_fix() const { return data_.valid; }
    int satellites() const { return data_.satellites; }
    double latitude() const { return data_.latitude; }
    double longitude() const { return data_.longitude; }
    double altitude() const { return data_.altitude; }

    const char* time_string() const { return time_str_; }
    const char* grid_locator() const { return grid_; }

    // Get Unix timestamp (seconds since epoch)
    int64_t unix_time() const;

    // Check if we're at the start of an even minute (for WSPR timing)
    bool is_tx_slot() const;

private:
    Gnss() = default;

    void compute_grid();
    void format_time();

    GnssData data_ = {};
    char time_str_[20] = "00:00:00";
    char grid_[7] = "AA00aa";

    // For stub mode simulation
    bool stub_mode_ = true;
    int64_t stub_base_time_ = 0;
};

} // namespace wspr
