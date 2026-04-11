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
    int reset();
    
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

    /**
     * @brief Copy the last complete NMEA sentence into the provided buffer.
     * 
     * @param dest Target buffer
     * @param maxLen Maximum bytes to copy
     * @return size_t Bytes copied (excluding null terminator)
     */
    size_t getRawNmea(char* dest, size_t maxLen) const;

    void setMonitor(bool enable) { monitorEnabled = enable; }
    bool isMonitoring() const { return monitorEnabled; }
    
    // Message queue for monitor mode
    struct k_msgq* getMonitorQueue() { return &monitorMsgQ; }

    GNSSData getData() const {
        k_mutex_lock(&mutex, K_FOREVER);
        GNSSData d = data;
        k_mutex_unlock(&mutex);
        return d;
    }

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
    char lastNmea[256] = "";

    // UART device
    const struct device* uartDev = nullptr;
    char nmeaBuf[256];
    int nmeaPos = 0;

    // Synchronization
    mutable struct k_mutex mutex;

    bool monitorEnabled = false;
    struct k_msgq monitorMsgQ;
    char msgq_buffer[4 * 256]; // Buffer for 4 NMEA sentences
};

} // namespace wspr
