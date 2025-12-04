#pragma once

#include <cstdint>
#include <cstddef>

namespace HAL {

/**
 * @brief Interface for a SPI peripheral.
 */
class ISpi {
public:
    virtual ~ISpi() = default;
    
    /**
     * @brief Write a block of data to the SPI bus.
     * 
     * @param data Pointer to the data to write.
     * @param len Length of the data in bytes.
     */
    virtual void write(const uint8_t* data, size_t len) = 0;
};

/**
 * @brief Interface for a system timer.
 */
class ITimer {
public:
    virtual ~ITimer() = default;

    /**
     * @brief Get the system uptime in milliseconds.
     * 
     * @return int64_t System uptime.
     */
    virtual int64_t getUptimeMs() = 0;

    /**
     * @brief Get the system uptime in picoseconds.
     * 
     * @return int64_t System uptime.
     */
    virtual int64_t getUptimePs() = 0;

    /**
     * @brief Sleep for a specified number of milliseconds.
     * 
     * @param ms Milliseconds to sleep.
     */
    virtual void sleepMs(int32_t ms) = 0;
};

} // namespace HAL
