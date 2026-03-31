/*
 * GNSS Module for WSPR-ease
 * Handles GPS/GNSS receiver for time sync and location
 */

#pragma once

#include <cstdint>
#include <zephyr/kernel.h>

namespace wspr {

struct GNSSData {
    bool valid;
    double latitude;
    double longitude;
    double altitude;
    float hdop;       // Horizontal Dilution of Precision (lower is better)
    float avgSNR;     // Average Signal-to-Noise Ratio (dB-Hz)
    uint8_t satellites;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t year;
    uint8_t month;
    uint8_t day;
};

class GNSS {
public:
    static GNSS& instance();

    int init();
    
    // Start/Stop processing (thread-safe)
    void start();
    void stop();

    bool hasFix() const { return data.valid; }
    int satellites() const { return data.satellites; }
    double latitude() const { return data.latitude; }
    double longitude() const { return data.longitude; }
    double altitude() const { return data.altitude; }
    float getHDOP() const { return data.hdop; }
    float avgSNR() const { return data.avgSNR; }

    const char* timeString() const { return timeStr; }
    const char* gridLocator() const { return grid; }

    // Get Unix timestamp (seconds since epoch)
    int64_t unixTime() const;

    // Check if we're at the start of an even minute (for WSPR timing)
    bool isTXSlot() const;

private:
    GNSS() = default;

    void computeGrid();
    void formatTime();
    void processLoop();
    void parseNMEA(char* line);

    // Thread management
    static void threadFn(void* p1, void* p2, void* p3);
    struct k_thread threadData;
    bool running = false;

    GNSSData data = {};
    char timeStr[20] = "n/a";
    char grid[7] = "AA00aa";

    // UART device
    const struct device* uartDev = nullptr;
    char nmeaBuf[128];
    int nmeaPos = 0;

    // Synchronization
    mutable struct k_mutex mutex;
};

} // namespace wspr
