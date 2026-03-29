/*
 * GNSS Module for WSPR-ease
 * Handles GPS/GNSS receiver for time sync and location
 */

#pragma once

#include <cstdint>
#include <zephyr/kernel.h>

namespace wspr {

struct GnssData {
    bool valid;
    double latitude;
    double longitude;
    double altitude;
    float hdop;       // Horizontal Dilution of Precision (lower is better)
    float avg_snr;    // Average Signal-to-Noise Ratio (dB-Hz)
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
    
    // Start/Stop processing (thread-safe)
    void start();
    void stop();

    bool has_fix() const { return data_.valid; }
    int satellites() const { return data_.satellites; }
    double latitude() const { return data_.latitude; }
    double longitude() const { return data_.longitude; }
    double altitude() const { return data_.altitude; }
    float hdop() const { return data_.hdop; }
    float avg_snr() const { return data_.avg_snr; }

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
    void process_loop();
    void parse_nmea(char* line);

    // Thread management
    static void thread_fn(void* p1, void* p2, void* p3);
    struct k_thread thread_data_;
    bool running_ = false;

    GnssData data_ = {};
    char time_str_[20] = "n/a";
    char grid_[7] = "AA00aa";

    // UART device
    const struct device* uart_dev_ = nullptr;
    char nmea_buf_[128];
    int nmea_pos_ = 0;

    // Synchronization
    mutable struct k_mutex mutex_;
};

} // namespace wspr
