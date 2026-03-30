/*
 * FPGA Control Module Implementation for WSPR-ease
 * Handles iCE40 bitstream loading and SPI register access
 */

#include "fpga.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/fs/fs.h>

#include <cstring>
#include <errno.h>

LOG_MODULE_REGISTER(fpga, LOG_LEVEL_INF);

namespace wspr {

// GPIO specs from devicetree
static const struct gpio_dt_spec fpga_reset = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_reset), gpios);
static const struct gpio_dt_spec fpga_done  = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_done), gpios);
static const struct gpio_dt_spec fpga_cs    = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_cs), gpios);

// iCE40 Slave SPI: Mode 0 (CPOL=0, CPHA=0), MSB First.
static const struct spi_dt_spec fpga_spi = SPI_DT_SPEC_GET(DT_NODELABEL(fpga_dev), 
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

// Fast bit-reversal helper
static inline uint8_t bit_reverse(uint8_t b) {
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
    return b;
}

Fpga& Fpga::instance() {
    static Fpga inst;
    return inst;
}

int Fpga::init() {
    LOG_INF("Initializing FPGA module");

    if (!device_is_ready(fpga_reset.port) || !device_is_ready(fpga_done.port) || !device_is_ready(fpga_cs.port)) {
        LOG_ERR("FPGA GPIO devices not ready");
        return -ENODEV;
    }

    if (!spi_is_ready_dt(&fpga_spi)) {
        LOG_ERR("FPGA SPI device not ready");
        return -ENODEV;
    }

    // Configure GPIOs. Done pin often needs a pull-up.
    gpio_pin_configure_dt(&fpga_reset, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&fpga_cs, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&fpga_done, GPIO_INPUT | GPIO_PULL_UP);

    // Ensure SS_B is High initially
    gpio_pin_set_dt(&fpga_cs, 0); 

    int ret = load_bitstream("/lfs/fpga.img");
    if (ret < 0) {
        initialized_ = false;
        return ret;
    }

    initialized_ = true;
    LOG_INF("FPGA initialized successfully");
    return 0;
}

int Fpga::reset() {
    // 1. Wakeup: Send 8 clocks with SS_B HIGH
    gpio_pin_set_dt(&fpga_cs, 0); // SS_B = High
    uint8_t dummy = 0xFF;
    struct spi_buf s_buf = { .buf = &dummy, .len = 1 };
    struct spi_buf_set s_bufs = { .buffers = &s_buf, .count = 1 };
    spi_write_dt(&fpga_spi, &s_bufs);
    k_usleep(100);

    // 2. Pulse CRESET_B
    gpio_pin_set_dt(&fpga_reset, 1); // CRESET_B = Low (Active)
    k_msleep(2);
    
    // 3. Drive SS_B Low (Must be LOW when CRESET_B transitions High)
    gpio_pin_set_dt(&fpga_cs, 1);    // SS_B = Low (Active)
    k_usleep(200);

    // 4. Release Reset
    gpio_pin_set_dt(&fpga_reset, 0); 
    
    // 5. Wait for t_STAB (datasheet says 1200us)
    k_msleep(5);
    
    return 0;
}

int Fpga::load_bitstream(const char* path) {
    struct fs_file_t file;
    fs_file_t_init(&file);

    int ret = fs_open(&file, path, FS_O_READ);
    if (ret < 0) {
        LOG_ERR("Could not open %s: %s", path, strerror(-ret));
        return ret;
    }

    struct fs_dirent stat;
    fs_stat(path, &stat);
    LOG_INF("Bitstream %s: %zu bytes", path, (size_t)stat.size);

    reset();

    LOG_INF("Transmitting bitstream with bit-reversal...");

    // SS_B is LOW from reset()
    uint8_t buf[1024];
    ssize_t bytes_read;
    size_t total_bytes = 0;
    struct spi_buf s_buf = { .buf = buf, .len = sizeof(buf) };
    struct spi_buf_set s_bufs = { .buffers = &s_buf, .count = 1 };

    while ((bytes_read = fs_read(&file, buf, sizeof(buf))) > 0) {
        // Bit-reverse each byte for iCE40 Slave SPI compatibility
        for (int i = 0; i < bytes_read; i++) {
            buf[i] = bit_reverse(buf[i]);
        }

        s_buf.len = bytes_read;
        ret = spi_write_dt(&fpga_spi, &s_bufs);
        if (ret < 0) {
            LOG_ERR("SPI fail at %zu: %s", total_bytes, strerror(-ret));
            fs_close(&file);
            gpio_pin_set_dt(&fpga_cs, 0);
            return ret;
        }
        total_bytes += bytes_read;
        k_yield();
    }

    fs_close(&file);
    LOG_INF("Upload complete (%zu bytes), sending final clocks...", total_bytes);

    // Send final dummy clocks with SS_B HIGH
    gpio_pin_set_dt(&fpga_cs, 0); 
    memset(buf, 0xFF, 16);
    s_buf.len = 16;
    spi_write_dt(&fpga_spi, &s_bufs);

    if (gpio_pin_get_dt(&fpga_done) > 0) {
        LOG_INF("FPGA Configuration SUCCESS (CDONE is HIGH)");
        return 0;
    } else {
        LOG_ERR("FPGA Configuration FAILED (CDONE remains LOW)");
        return -EAGAIN; 
    }
}

int Fpga::set_frequency(uint32_t freq_hz) {
    current_freq_ = freq_hz;
    if (!initialized_) return -ENODEV;

    // word = (freq * 2^32) / 40,000,000
    uint64_t word = ((uint64_t)freq_hz << 32) / 40000000;
    return spi_write_reg(0x01, (uint32_t)word);
}

int Fpga::start_tx() {
    if (!initialized_) return -ENODEV;
    if (transmitting_) return -EALREADY;
    LOG_INF("Starting transmission at %u Hz", current_freq_);
    transmitting_ = true;
    return spi_write_reg(0x00, 0x01); 
}

int Fpga::stop_tx() {
    if (!initialized_) return -ENODEV;
    if (!transmitting_) return 0;
    LOG_INF("Stopping transmission");
    transmitting_ = false;
    return spi_write_reg(0x00, 0x00); 
}

int Fpga::send_symbol(uint8_t symbol) {
    if (!initialized_) return -ENODEV;
    return spi_write_reg(0x02, symbol);
}

int Fpga::set_lpf_band(WsprBand band) {
    current_band_ = band;
    return 0;
}

uint32_t Fpga::get_counter() {
    if (!initialized_) return 0;
    uint32_t val = 0;
    spi_read_reg(0x03, &val);
    return val;
}

int Fpga::spi_write_reg(uint8_t reg, uint32_t value) {
    uint8_t tx_buf[5];
    tx_buf[0] = 0x80 | (reg & 0x7F);
    tx_buf[1] = (value >> 24) & 0xFF;
    tx_buf[2] = (value >> 16) & 0xFF;
    tx_buf[3] = (value >> 8) & 0xFF;
    tx_buf[4] = value & 0xFF;

    // NO bit reversal for register access - this is post-config SPI
    gpio_pin_set_dt(&fpga_cs, 1); 
    struct spi_buf s_buf = { .buf = tx_buf, .len = sizeof(tx_buf) };
    struct spi_buf_set s_bufs = { .buffers = &s_buf, .count = 1 };
    int ret = spi_write_dt(&fpga_spi, &s_bufs);
    gpio_pin_set_dt(&fpga_cs, 0); 
    return ret;
}

int Fpga::spi_read_reg(uint8_t reg, uint32_t* value) {
    uint8_t tx_buf[1] = { (uint8_t)(reg & 0x7F) };
    uint8_t rx_buf[4] = { 0 };

    gpio_pin_set_dt(&fpga_cs, 1); 
    struct spi_buf s_tx = { .buf = tx_buf, .len = 1 };
    struct spi_buf_set s_txs = { .buffers = &s_tx, .count = 1 };
    struct spi_buf s_rx = { .buf = rx_buf, .len = 4 };
    struct spi_buf_set s_rxs = { .buffers = &s_rx, .count = 1 };
    int ret = spi_transceive_dt(&fpga_spi, &s_txs, &s_rxs);
    gpio_pin_set_dt(&fpga_cs, 0); 

    if (ret == 0) {
        *value = ((uint32_t)rx_buf[0] << 24) |
                 ((uint32_t)rx_buf[1] << 16) |
                 ((uint32_t)rx_buf[2] << 8) |
                 (uint32_t)rx_buf[3];
    }
    return ret;
}

} // namespace wspr
