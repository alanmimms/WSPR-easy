/*
 * GNSS Module Implementation for WSPR-ease
 * Real hardware implementation using Zephyr UART API
 */

#include "gnss.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>

#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include "fpga.hpp"
#include "logmanager.hpp"

LOG_MODULE_REGISTER(gnss, LOG_LEVEL_INF);

namespace wspr {

// Register subsystem with LogManager
static Logger& logger = LogManager::instance().registerSubsystem("gnss", 
    {"raw", "fix", "time", "init"});

#define GNSS_STACK_SIZE 4096
static k_thread_stack_t *gnssStackPtr = nullptr;

GNSS& GNSS::instance() {
    static GNSS inst;
    return inst;
}

int GNSS::init() {
    logger.inf("init", "Initializing GNSS module");
    k_mutex_init(&mutex);
    k_msgq_init(&monitorMsgQ, msgq_buffer, 256, 4);

    // Allocate stack from heap
    if (!gnssStackPtr) {
        gnssStackPtr = (k_thread_stack_t *)k_aligned_alloc(
            ARCH_STACK_PTR_ALIGN, 
            K_THREAD_STACK_LEN(GNSS_STACK_SIZE)
        );
        if (!gnssStackPtr) {
            logger.err("init", "Failed to allocate GNSS stack");
            return -ENOMEM;
        }
    }

    // GNSS Reset sequence
    static const struct gpio_dt_spec gnssReset = GPIO_DT_SPEC_GET(DT_NODELABEL(gnss_reset), gpios);
    if (device_is_ready(gnssReset.port)) {
        gpio_pin_configure_dt(&gnssReset, GPIO_OUTPUT_INACTIVE);
        reset();
    }

    uartDev = DEVICE_DT_GET(DT_ALIAS(gnss_uart));
    if (!device_is_ready(uartDev)) {
        logger.err("init", "GNSS UART device not ready");
        return -ENODEV;
    }

    // Start processing thread
    start();

    return 0;
}

int GNSS::reset() {
    static const struct gpio_dt_spec gnssReset = GPIO_DT_SPEC_GET(DT_NODELABEL(gnss_reset), gpios);
    if (!device_is_ready(gnssReset.port)) return -ENODEV;

    logger.inf("init", "Resetting GNSS chip (IO15 Active-Low)...");
    gpio_pin_set_dt(&gnssReset, 0); // reset
    k_msleep(10);
    gpio_pin_set_dt(&gnssReset, 1); // deassert reset
    logger.inf("init", "GNSS reset released");
    return 0;
}

void GNSS::start() {
    if (running) return;
    
    running = true;
    k_thread_create(&threadData, gnssStackPtr, GNSS_STACK_SIZE,
                    threadFn, this, NULL, NULL,
                    K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
    k_thread_name_set(&threadData, "gnssWorker");
}

void GNSS::stop() {
    running = false;
}

void GNSS::threadFn(void* p1, void* p2, void* p3) {
    GNSS* inst = static_cast<GNSS*>(p1);
    inst->processLoop();
}

void GNSS::processLoop() {
    logger.inf("init", "GNSS worker thread started (fixed 9600 baud)");
    
    uint8_t c;
    uint32_t lastLogTime = 0;
    bool firstDataReceived = false;

    while (running) {
        // Skip processing during transmission
        if (FPGA::instance().isTransmitting()) {
            while (uart_poll_in(uartDev, &c) == 0) { /* flush */ }
            k_sleep(K_MSEC(1000));
            continue;
        }

        // Read from UART
        if (uart_poll_in(uartDev, &c) == 0) {
            if (!firstDataReceived) {
                logger.inf("raw", "GNSS: Data received from UART (byte: 0x%02x '%c')", c, (c >= 32 && c <= 126) ? c : '.');
                firstDataReceived = true;
            }

            if (c == '$') {
                nmeaPos = 0; // Always sync to start of sentence
            }

            if (c == '\n' || c == '\r') {
                if (nmeaPos > 5) { // Minimum NMEA sentence length ($GPxyz)
                    nmeaBuf[nmeaPos] = '\0';
                    
                    // COHERENT COPY: Latch the full message into the 'last' buffer
                    k_mutex_lock(&mutex, K_FOREVER);
                    strncpy(lastNmea, nmeaBuf, sizeof(lastNmea)-1);
                    lastNmea[sizeof(lastNmea)-1] = '\0';
                    
                    if (monitorEnabled) {
                        k_msgq_put(&monitorMsgQ, nmeaBuf, K_NO_WAIT);
                    }
                    
                    // Parse while holding lock to keep GNSSData coherent
                    parseNMEA(nmeaBuf);
                    k_mutex_unlock(&mutex);
                    
                    nmeaPos = 0;
                }
            } else {
                if (nmeaPos < (int)sizeof(nmeaBuf) - 1) {
                    nmeaBuf[nmeaPos++] = c;
                } else {
                    nmeaPos = 0; // Overflow
                }
            }
        } else {
            k_sleep(K_MSEC(1)); // Fast poll to avoid FIFO overruns
        }

        // Periodic status log (every 10 seconds)
        uint32_t now = k_uptime_get_32();
        if (now - lastLogTime >= 10000) {
            k_mutex_lock(&mutex, K_FOREVER);
            if (!firstDataReceived) {
                logger.wrn("init", "GNSS Status: NO DATA RECEIVED ON UART");
            } else {
                logger.inf("fix", "GNSS Status: lock=%s, sats=%d, snr=%.1f, time=%s",
                        data.valid ? "YES" : "NO", data.satellites, (double)data.avgSNR, timeStr);
            }
            k_mutex_unlock(&mutex);
            lastLogTime = now;
        }
    }
}

size_t GNSS::getRawNmea(char* dest, size_t maxLen) const {
    if (!dest || maxLen == 0) return 0;
    
    k_mutex_lock(&mutex, K_FOREVER);
    strncpy(dest, lastNmea, maxLen - 1);
    dest[maxLen - 1] = '\0';
    size_t len = strlen(dest);
    k_mutex_unlock(&mutex);
    
    return len;
}

// Helper to parse NMEA lat/lon format (DDMM.MMMM)
static double parseNMEACoord(const char* s, const char* dir) {
    if (!s || !*s || !dir || !*dir) return 0.0;
    
    double val = atof(s);
    int degrees = (int)(val / 100.0);
    double minutes = val - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);
    
    if (*dir == 'S' || *dir == 'W') decimal = -decimal;
    
    return decimal;
}

void GNSS::parseNMEA(char* line) {
    if (line[0] != '$' || strlen(line) < 7) return;

    char* saveptr;
    char* token = strtok_r(line, ",", &saveptr);
    if (!token) return;

    if (strstr(token, "RMC")) {
        // $--RMC,time,status,lat,N,lon,W,spd,cog,date,mv,mvE,mode*cs
        
        char* tTime = strtok_r(NULL, ",", &saveptr);
        char* tStatus = strtok_r(NULL, ",", &saveptr);
        char* tLat = strtok_r(NULL, ",", &saveptr);
        char* tLatDir = strtok_r(NULL, ",", &saveptr);
        char* tLon = strtok_r(NULL, ",", &saveptr);
        char* tLonDir = strtok_r(NULL, ",", &saveptr);
        strtok_r(NULL, ",", &saveptr); // speed
        strtok_r(NULL, ",", &saveptr); // cog
        char* tDate = strtok_r(NULL, ",", &saveptr);

        if (tStatus && *tStatus == 'A') {
            data.valid = true;
            if (tTime && strlen(tTime) >= 6) {
                data.hour = (tTime[0]-'0')*10 + (tTime[1]-'0');
                data.minute = (tTime[2]-'0')*10 + (tTime[3]-'0');
                data.second = (tTime[4]-'0')*10 + (tTime[5]-'0');
            }
            if (tDate && strlen(tDate) >= 6) {
                data.day = (tDate[0]-'0')*10 + (tDate[1]-'0');
                data.month = (tDate[2]-'0')*10 + (tDate[3]-'0');
                data.year = 2000 + (tDate[4]-'0')*10 + (tDate[5]-'0');
            }
            if (tLat && tLatDir && tLon && tLonDir) {
                data.latitude = parseNMEACoord(tLat, tLatDir);
                data.longitude = parseNMEACoord(tLon, tLonDir);
                computeGrid();
            }
            formatTime();
        } else {
            data.valid = false;
            formatTime();
        }
    } 
    else if (strstr(token, "GGA")) {
        // $--GGA,time,lat,N,lon,W,fix,sats,hdop,alt,M,geoid,M,age,ref*cs
        strtok_r(NULL, ",", &saveptr); // time
        strtok_r(NULL, ",", &saveptr); // lat
        strtok_r(NULL, ",", &saveptr); // N
        strtok_r(NULL, ",", &saveptr); // lon
        strtok_r(NULL, ",", &saveptr); // W
        char* tFix = strtok_r(NULL, ",", &saveptr);
        char* tSats = strtok_r(NULL, ",", &saveptr);
        char* tHDOP = strtok_r(NULL, ",", &saveptr);
        char* tAlt = strtok_r(NULL, ",", &saveptr);

        if (tFix && *tFix != '0') {
            data.valid = true;
            if (tSats) data.satellites = atoi(tSats);
            if (tHDOP) data.hdop = atof(tHDOP);
            if (tAlt) data.altitude = atof(tAlt);
        }
    }
    else if (strstr(token, "GSV")) {
        // $--GSV,num_msgs,msg_num,sats_in_view,sat1_prn,sat1_elev,sat1_az,sat1_snr,...
        strtok_r(NULL, ",", &saveptr); // num_msgs
        strtok_r(NULL, ",", &saveptr); // msg_num
        char* tSatsInView = strtok_r(NULL, ",", &saveptr);
        
        if (tSatsInView) {
            double sumSNR = 0;
            int countSNR = 0;
            
            for (int i = 0; i < 4; i++) {
                strtok_r(NULL, ",", &saveptr); // PRN
                strtok_r(NULL, ",", &saveptr); // Elev
                strtok_r(NULL, ",", &saveptr); // Az
                char* tSNR = strtok_r(NULL, ",", &saveptr);
                if (tSNR && *tSNR) {
                    sumSNR += atof(tSNR);
                    countSNR++;
                }
            }
            
            if (countSNR > 0) {
                data.avgSNR = (float)(sumSNR / countSNR);
            }
        }
    }
}

int64_t GNSS::unixTime() const {
    k_mutex_lock(&mutex, K_FOREVER);
    if (!data.valid) {
        k_mutex_unlock(&mutex);
        return 0;
    }

    struct tm t;
    t.tm_sec = data.second;
    t.tm_min = data.minute;
    t.tm_hour = data.hour;
    t.tm_mday = data.day;
    t.tm_mon = data.month - 1;
    t.tm_year = data.year - 1900;
    t.tm_isdst = 0;

    // Use Zephyr's time util or standard mktime
    // Note: mktime uses local time, we want UTC.
    // For now, a simple approximation or better yet use a proper UTC conversion.
    // Zephyr has posix-like time functions.
    time_t epoch = mktime(&t);
    k_mutex_unlock(&mutex);
    return (int64_t)epoch;
}

bool GNSS::isTXSlot() const {
    k_mutex_lock(&mutex, K_FOREVER);
    // WSPR transmissions start at even minutes
    bool result = data.valid && (data.second == 0) && ((data.minute % 2) == 0);
    k_mutex_unlock(&mutex);
    return result;
}

void GNSS::computeGrid() {
    // Maidenhead grid locator calculation
    double lon = data.longitude + 180.0;
    double lat = data.latitude + 90.0;

    grid[0] = 'A' + (int)(lon / 20.0);
    grid[1] = 'A' + (int)(lat / 10.0);

    double remLon = fmod(lon, 20.0);
    double remLat = fmod(lat, 10.0);
    grid[2] = '0' + (int)(remLon / 2.0);
    grid[3] = '0' + (int)(remLat / 1.0);

    double subLon = fmod(remLon, 2.0);
    double subLat = fmod(remLat, 1.0);
    grid[4] = 'a' + (int)(subLon * 12.0);
    grid[5] = 'a' + (int)(subLat * 24.0);
    grid[6] = '\0';
}

void GNSS::formatTime() {
    if (!data.valid) {
        snprintf(timeStr, sizeof(timeStr), "n/a");
        return;
    }
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
             data.hour, data.minute, data.second);
}

} // namespace wspr
