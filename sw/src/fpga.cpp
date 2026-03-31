/*
 * FPGA Control Module Implementation for WSPR-ease
 * Handles iCE40 bitstream loading and SPI register access
 * Strictly follows Lattice iCE40 SPI Peripheral Mode (Slave SPI) timing.
 */

#include "fpga.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/fs/fs.h>

#include <cstring>
#include <errno.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(fpga, LOG_LEVEL_INF);

namespace wspr {

// GPIO specs from devicetree (All configured as GPIO_ACTIVE_HIGH in overlay)
static const struct gpio_dt_spec fpga_reset = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_reset), gpios);
static const struct gpio_dt_spec fpga_done  = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_done), gpios);
static const struct gpio_dt_spec fpga_cs    = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_cs), gpios);

// iCE40 Slave SPI: Mode 0 (CPOL=0, CPHA=0), MSB First.
static const struct spi_dt_spec fpga_spi = SPI_DT_SPEC_GET(DT_NODELABEL(fpga_dev), 
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

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

    // Configure GPIOs for direct physical control (1=High, 0=Low)
    gpio_pin_configure_dt(&fpga_reset, GPIO_OUTPUT_LOW); // Hold in Reset
    gpio_pin_configure_dt(&fpga_cs, GPIO_OUTPUT_HIGH);    // CS Idle
    gpio_pin_configure_dt(&fpga_done, GPIO_INPUT | GPIO_PULL_UP);

    LOG_INF("FPGA held in reset while preparing image...");

    int ret = load_bitstream("/lfs/fpga.img");
    if (ret < 0) {
        initialized_ = false;
        return ret;
    }

    initialized_ = true;
    LOG_INF("FPGA initialized and running");
    return 0;
}

int Fpga::reset() {
    // 1. AP begins by driving CRESET_B Low, resetting iCE40.
    // Similarly, AP holds iCE40 SPI_SS (CS) Low.
    gpio_pin_set_dt(&fpga_cs, 0);    // Physical Low
    gpio_pin_set_dt(&fpga_reset, 0); // Physical Low
    
    // AP must hold CRESET_B Low for at least 200 ns.
    k_msleep(1); // 1ms is plenty

    // 2. AP releases CRESET_B (drives it High).
    // iCE40 enters SPI peripheral mode when CRESET_B returns High while SPI_SS is Low.
    gpio_pin_set_dt(&fpga_reset, 1); // Physical High
    
    // SPI_SS must remain low for at least 200ns after CRESET_B release.
    k_usleep(10); // 10us is plenty

    // 3. AP must wait a minimum of 1200 us for internal memory clearing.
    k_msleep(2); // 2ms is plenty
    
    // Verify CDONE is LOW (housekeeping should be in progress or done)
    if (gpio_pin_get_dt(&fpga_done) > 0) {
        LOG_ERR("FPGA Error: CDONE is HIGH immediately after reset pulse! Bad boot mode?");
        return -EIO;
    }
    
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
    LOG_INF("Bitstream %s opened (%zu bytes)", path, (size_t)stat.size);

    // Allocate transient buffer on heap
    const size_t chunk_size = 2048;
    uint8_t* buffer = (uint8_t*)k_malloc(chunk_size);
    if (!buffer) {
        LOG_ERR("Failed to allocate bitstream buffer");
        fs_close(&file);
        return -ENOMEM;
    }

    // A. Trigger Reset and enter Slave SPI mode
    if (reset() != 0) {
        k_free(buffer);
        fs_close(&file);
        return -EIO;
    }

    // B. Once housekeeping is done, SPI_SS goes to High, waits 8 clocks, then back to Low.
    gpio_pin_set_dt(&fpga_cs, 1); // Physical High
    
    uint8_t dummy = 0x00;
    struct spi_buf s_dummy = { .buf = &dummy, .len = 1 };
    struct spi_buf_set s_dummies = { .buffers = &s_dummy, .count = 1 };
    spi_write_dt(&fpga_spi, &s_dummies); // 8 clocks
    
    gpio_pin_set_dt(&fpga_cs, 0); // Physical Low

    // C. Transmit entire image without interruption
    LOG_INF("Transmitting bitstream...");

    ssize_t bytes_read;
    size_t total_bytes = 0;
    struct spi_buf s_buf = { .buf = buffer, .len = chunk_size };
    struct spi_buf_set s_bufs = { .buffers = &s_buf, .count = 1 };

    while ((bytes_read = fs_read(&file, buffer, chunk_size)) > 0) {
        s_buf.len = bytes_read;
        ret = spi_write_dt(&fpga_spi, &s_bufs);
        if (ret < 0) {
            LOG_ERR("SPI write error at %zu: %d", total_bytes, ret);
            k_free(buffer);
            fs_close(&file);
            gpio_pin_set_dt(&fpga_cs, 1); 
            return ret;
        }
        total_bytes += bytes_read;
    }

    fs_close(&file);
    LOG_INF("Transmitted %zu bytes", total_bytes);

    // D. After sending entire image, iCE40 releases CDONE.
    // SPI_SS goes High.
    gpio_pin_set_dt(&fpga_cs, 1); // Physical High

    // Wait for CDONE up to 100 clock cycles.
    // We'll send a few dummy bytes while checking.
    bool success = false;
    for (int i = 0; i < 20; i++) {
        if (gpio_pin_get_dt(&fpga_done) > 0) {
            success = true;
            break;
        }
        spi_write_dt(&fpga_spi, &s_dummies); // 8 clocks per loop
    }

    if (success) {
        LOG_INF("FPGA SUCCESS: CDONE is HIGH");
        // E. After CDONE goes High, send at least 49 additional dummy bits.
        memset(buffer, 0x00, 8); // 64 clocks
        s_buf.len = 8;
        spi_write_dt(&fpga_spi, &s_bufs);
        ret = 0;
    } else {
        LOG_ERR("FPGA FAILURE: CDONE remains LOW");
        ret = -EAGAIN; 
    }

    k_free(buffer);
    return ret;
}

int Fpga::set_frequency(uint32_t freq_hz) {
    current_freq_ = freq_hz;
    if (!initialized_) return -ENODEV;

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

    gpio_pin_set_dt(&fpga_cs, 0); // Physical Low
    struct spi_buf s_buf = { .buf = tx_buf, .len = sizeof(tx_buf) };
    struct spi_buf_set s_bufs = { .buffers = &s_buf, .count = 1 };
    int ret = spi_write_dt(&fpga_spi, &s_bufs);
    gpio_pin_set_dt(&fpga_cs, 1); // Physical High
    return ret;
}

int Fpga::spi_read_reg(uint8_t reg, uint32_t* value) {
    uint8_t tx_buf[1] = { (uint8_t)(reg & 0x7F) };
    uint8_t rx_buf[4] = { 0 };

    gpio_pin_set_dt(&fpga_cs, 0); // Physical Low
    struct spi_buf s_tx = { .buf = tx_buf, .len = 1 };
    struct spi_buf_set s_txs = { .buffers = &s_tx, .count = 1 };
    struct spi_buf s_rx = { .buf = rx_buf, .len = 4 };
    struct spi_buf_set s_rxs = { .buffers = &s_rx, .count = 1 };
    int ret = spi_transceive_dt(&fpga_spi, &s_txs, &s_rxs);
    gpio_pin_set_dt(&fpga_cs, 1); // Physical High

    if (ret == 0) {
        *value = ((uint32_t)rx_buf[0] << 24) |
                 ((uint32_t)rx_buf[1] << 16) |
                 ((uint32_t)rx_buf[2] << 8) |
                 (uint32_t)rx_buf[3];
    }
    return ret;
}

} // namespace wspr
