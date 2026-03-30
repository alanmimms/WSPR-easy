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

LOG_MODULE_REGISTER(gnss, LOG_LEVEL_INF);

namespace wspr {

static K_THREAD_STACK_DEFINE(gnss_stack, 4096);

Gnss& Gnss::instance() {
    static Gnss inst;
    return inst;
}

int Gnss::init() {
    LOG_INF("Initializing GNSS module");
    k_mutex_init(&mutex_);

    // GNSS Reset sequence
    static const struct gpio_dt_spec gnss_reset = GPIO_DT_SPEC_GET(DT_NODELABEL(gnss_reset), gpios);
    if (device_is_ready(gnss_reset.port)) {
        LOG_INF("Pulsing GNSS reset (GPIO 15)");
        gpio_pin_configure_dt(&gnss_reset, GPIO_OUTPUT_INACTIVE); // Start High (Inactive)
        
        gpio_pin_set_dt(&gnss_reset, 0); // Physical High
        k_msleep(10);
        
        gpio_pin_set_dt(&gnss_reset, 1); // Physical Low (Active Reset)
        k_msleep(50);
        
        gpio_pin_set_dt(&gnss_reset, 0); // Physical High (Release Reset)
        LOG_INF("GNSS reset released");
    }

    uart_dev_ = DEVICE_DT_GET(DT_ALIAS(gnss_uart));
    if (!device_is_ready(uart_dev_)) {
        LOG_ERR("GNSS UART device not ready");
        return -ENODEV;
    }

    // Start processing thread
    start();

    return 0;
}

void Gnss::start() {
    if (running_) return;
    
    running_ = true;
    k_thread_create(&thread_data_, gnss_stack, K_THREAD_STACK_SIZEOF(gnss_stack),
                    thread_fn, this, NULL, NULL,
                    K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
    k_thread_name_set(&thread_data_, "gnss_worker");
}

void Gnss::stop() {
    running_ = false;
}

void Gnss::thread_fn(void* p1, void* p2, void* p3) {
    Gnss* inst = static_cast<Gnss*>(p1);
    inst->process_loop();
}

void Gnss::process_loop() {
    LOG_INF("GNSS worker thread started (fixed 9600 baud)");
    
    uint8_t c;
    uint32_t last_log_time = 0;
    uint32_t last_data_time = k_uptime_get_32();
    bool first_data_received = false;

    while (running_) {
        // Skip processing during transmission to avoid affecting realtime modulation
        if (Fpga::instance().is_transmitting()) {
            while (uart_poll_in(uart_dev_, &c) == 0) { /* flush */ }
            k_sleep(K_MSEC(1000));
            continue;
        }

        // Read from UART
        if (uart_poll_in(uart_dev_, &c) == 0) {
            last_data_time = k_uptime_get_32();
            if (!first_data_received) {
                LOG_INF("GNSS: Data received from UART (byte: 0x%02x '%c')", c, (c >= 32 && c <= 126) ? c : '.');
                first_data_received = true;
            }

            if (c == '\n' || c == '\r') {
                if (nmea_pos_ > 0) {
                    nmea_buf_[nmea_pos_] = '\0';
                    parse_nmea(nmea_buf_);
                    nmea_pos_ = 0;
                }
            } else {
                if (nmea_pos_ < (int)sizeof(nmea_buf_) - 1) {
                    nmea_buf_[nmea_pos_++] = c;
                } else {
                    nmea_pos_ = 0; // Buffer overflow, reset
                }
            }
        } else {
            k_sleep(K_MSEC(10));
        }

        // Periodic status log (every 10 seconds)
        uint32_t now = k_uptime_get_32();
        if (now - last_log_time >= 10000) {
            k_mutex_lock(&mutex_, K_FOREVER);
            if (!first_data_received) {
                LOG_WRN("GNSS Status: NO DATA RECEIVED ON UART (fixed 9600 baud)");
            } else {
                LOG_INF("GNSS Status: lock=%s, sats=%d, snr=%.1f, time=%s",
                        data_.valid ? "YES" : "NO", data_.satellites, (double)data_.avg_snr, time_str_);
            }
            k_mutex_unlock(&mutex_);
            last_log_time = now;
        }
    }
}

// Helper to parse NMEA lat/lon format (DDMM.MMMM)
static double parse_nmea_coord(const char* s, const char* dir) {
    if (!s || !*s || !dir || !*dir) return 0.0;
    
    double val = atof(s);
    int degrees = (int)(val / 100.0);
    double minutes = val - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);
    
    if (*dir == 'S' || *dir == 'W') decimal = -decimal;
    
    return decimal;
}

void Gnss::parse_nmea(char* line) {
    if (line[0] != '$') return;

    // We look for RMC (Recommended Minimum Navigation Information) 
    // and GGA (Global Positioning System Fix Data)
    // and GSV (GNSS Satellites in View)
    
    char* saveptr;
    char* token = strtok_r(line, ",", &saveptr);
    if (!token) return;

    if (strcmp(token, "$GPRMC") == 0 || strcmp(token, "$GNRMC") == 0) {
        // $--RMC,time,status,lat,N,lon,W,spd,cog,date,mv,mvE,mode*cs
        k_mutex_lock(&mutex_, K_FOREVER);
        
        char* t_time = strtok_r(NULL, ",", &saveptr);
        char* t_status = strtok_r(NULL, ",", &saveptr);
        char* t_lat = strtok_r(NULL, ",", &saveptr);
        char* t_lat_dir = strtok_r(NULL, ",", &saveptr);
        char* t_lon = strtok_r(NULL, ",", &saveptr);
        char* t_lon_dir = strtok_r(NULL, ",", &saveptr);
        strtok_r(NULL, ",", &saveptr); // speed
        strtok_r(NULL, ",", &saveptr); // cog
        char* t_date = strtok_r(NULL, ",", &saveptr);

        if (t_status && *t_status == 'A') {
            data_.valid = true;
            if (t_time && strlen(t_time) >= 6) {
                data_.hour = (t_time[0]-'0')*10 + (t_time[1]-'0');
                data_.minute = (t_time[2]-'0')*10 + (t_time[3]-'0');
                data_.second = (t_time[4]-'0')*10 + (t_time[5]-'0');
            }
            if (t_date && strlen(t_date) >= 6) {
                data_.day = (t_date[0]-'0')*10 + (t_date[1]-'0');
                data_.month = (t_date[2]-'0')*10 + (t_date[3]-'0');
                data_.year = 2000 + (t_date[4]-'0')*10 + (t_date[5]-'0');
            }
            if (t_lat && t_lat_dir && t_lon && t_lon_dir) {
                data_.latitude = parse_nmea_coord(t_lat, t_lat_dir);
                data_.longitude = parse_nmea_coord(t_lon, t_lon_dir);
                compute_grid();
            }
            format_time();
        } else {
            data_.valid = false;
            format_time();
        }
        k_mutex_unlock(&mutex_);
    } 
    else if (strcmp(token, "$GPGGA") == 0 || strcmp(token, "$GNGGA") == 0) {
        // $--GGA,time,lat,N,lon,W,fix,sats,hdop,alt,M,geoid,M,age,ref*cs
        k_mutex_lock(&mutex_, K_FOREVER);
        strtok_r(NULL, ",", &saveptr); // time
        strtok_r(NULL, ",", &saveptr); // lat
        strtok_r(NULL, ",", &saveptr); // N
        strtok_r(NULL, ",", &saveptr); // lon
        strtok_r(NULL, ",", &saveptr); // W
        char* t_fix = strtok_r(NULL, ",", &saveptr);
        char* t_sats = strtok_r(NULL, ",", &saveptr);
        char* t_hdop = strtok_r(NULL, ",", &saveptr);
        char* t_alt = strtok_r(NULL, ",", &saveptr);

        if (t_fix && *t_fix != '0') {
            data_.valid = true;
            if (t_sats) data_.satellites = atoi(t_sats);
            if (t_hdop) data_.hdop = atof(t_hdop);
            if (t_alt) data_.altitude = atof(t_alt);
        }
        k_mutex_unlock(&mutex_);
    }
    else if (strcmp(token, "$GPGSV") == 0 || strcmp(token, "$GNGSV") == 0) {
        // $--GSV,num_msgs,msg_num,sats_in_view,sat1_prn,sat1_elev,sat1_az,sat1_snr,...
        // Simple average SNR calculation
        strtok_r(NULL, ",", &saveptr); // num_msgs
        strtok_r(NULL, ",", &saveptr); // msg_num
        char* t_sats_in_view = strtok_r(NULL, ",", &saveptr);
        
        if (t_sats_in_view) {
            double sum_snr = 0;
            int count_snr = 0;
            
            for (int i = 0; i < 4; i++) {
                strtok_r(NULL, ",", &saveptr); // PRN
                strtok_r(NULL, ",", &saveptr); // Elev
                strtok_r(NULL, ",", &saveptr); // Az
                char* t_snr = strtok_r(NULL, ",", &saveptr);
                if (t_snr && *t_snr) {
                    sum_snr += atof(t_snr);
                    count_snr++;
                }
            }
            
            if (count_snr > 0) {
                k_mutex_lock(&mutex_, K_FOREVER);
                data_.avg_snr = (float)(sum_snr / count_snr);
                k_mutex_unlock(&mutex_);
            }
        }
    }
}

int64_t Gnss::unix_time() const {
    k_mutex_lock(&mutex_, K_FOREVER);
    if (!data_.valid) {
        k_mutex_unlock(&mutex_);
        return 0;
    }

    struct tm t;
    t.tm_sec = data_.second;
    t.tm_min = data_.minute;
    t.tm_hour = data_.hour;
    t.tm_mday = data_.day;
    t.tm_mon = data_.month - 1;
    t.tm_year = data_.year - 1900;
    t.tm_isdst = 0;

    // Use Zephyr's time util or standard mktime
    // Note: mktime uses local time, we want UTC.
    // For now, a simple approximation or better yet use a proper UTC conversion.
    // Zephyr has posix-like time functions.
    time_t epoch = mktime(&t);
    k_mutex_unlock(&mutex_);
    return (int64_t)epoch;
}

bool Gnss::is_tx_slot() const {
    k_mutex_lock(&mutex_, K_FOREVER);
    // WSPR transmissions start at even minutes
    bool result = data_.valid && (data_.second == 0) && ((data_.minute % 2) == 0);
    k_mutex_unlock(&mutex_);
    return result;
}

void Gnss::compute_grid() {
    // Maidenhead grid locator calculation
    double lon = data_.longitude + 180.0;
    double lat = data_.latitude + 90.0;

    grid_[0] = 'A' + (int)(lon / 20.0);
    grid_[1] = 'A' + (int)(lat / 10.0);

    double remLon = fmod(lon, 20.0);
    double remLat = fmod(lat, 10.0);
    grid_[2] = '0' + (int)(remLon / 2.0);
    grid_[3] = '0' + (int)(remLat / 1.0);

    double subLon = fmod(remLon, 2.0);
    double subLat = fmod(remLat, 1.0);
    grid_[4] = 'a' + (int)(subLon * 12.0);
    grid_[5] = 'a' + (int)(subLat * 24.0);
    grid_[6] = '\0';
}

void Gnss::format_time() {
    if (!data_.valid) {
        snprintf(time_str_, sizeof(time_str_), "n/a");
        return;
    }
    snprintf(time_str_, sizeof(time_str_), "%02d:%02d:%02d",
             data_.hour, data_.minute, data_.second);
}

} // namespace wspr
