/*
 * FPGA Control Module Implementation for WSPR-ease
 * Handles iCE40 bitstream loading and SPI register access
 * Strictly follows Lattice iCE40 SPI Peripheral Mode (Slave SPI) timing.
 */

#include "fpga.hpp"
#include "filesystem.hpp"

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
static const struct gpio_dt_spec fpgaNRESET = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_nreset), gpios);
static const struct gpio_dt_spec fpgaDONE   = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_done), gpios);
static const struct gpio_dt_spec fpgaCS     = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_cs), gpios);

static const struct gpio_dt_spec pgFPGACORE = GPIO_DT_SPEC_GET(DT_NODELABEL(pg_fpgacore), gpios);
static const struct gpio_dt_spec enFPGAIO   = GPIO_DT_SPEC_GET(DT_NODELABEL(en_fpgaio), gpios);

// iCE40 Slave SPI: Mode 0 (CPOL=0, CPHA=0), MSB First.
static const struct spi_dt_spec fpgaSPI = SPI_DT_SPEC_GET(DT_NODELABEL(fpga_dev), 
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

FPGA& FPGA::instance() {
    static FPGA inst;
    return inst;
}

int FPGA::init() {
    LOG_INF("Initializing FPGA module");

    if (!device_is_ready(fpgaNRESET.port) || !device_is_ready(fpgaDONE.port) || 
        !device_is_ready(fpgaCS.port) || !device_is_ready(pgFPGACORE.port) || 
        !device_is_ready(enFPGAIO.port)) {
        LOG_ERR("FPGA GPIO devices not ready");
        return -ENODEV;
    }

    if (!spi_is_ready_dt(&fpgaSPI)) {
        LOG_ERR("FPGA SPI device not ready");
        return -ENODEV;
    }

    // MANDATE: Explicitly start with enFPGAIO DEASSERTED (Physical Low).
    // The hardware has a 10k pulldown, but we ensure it in software too.
    gpio_pin_configure_dt(&enFPGAIO, GPIO_OUTPUT_LOW);
    
    // Hold FPGA in reset and set CS High
    gpio_pin_configure_dt(&fpgaNRESET, GPIO_OUTPUT_LOW); // CRESET_B Physical Low
    gpio_pin_configure_dt(&fpgaCS, GPIO_OUTPUT_HIGH);    // CS Physical High
    gpio_pin_configure_dt(&pgFPGACORE, GPIO_INPUT);
    gpio_pin_configure_dt(&fpgaDONE, GPIO_INPUT | GPIO_PULL_UP);

    LOG_INF("enFPGAIO deasserted. Waiting for FPGA Core power good (pgFPGACORE)...");
    
    // Power Sequencing: Wait for Core Power Good
    uint32_t startTime = k_uptime_get_32();
    bool pgOK = true;		// XXX TEMPORARILY ignore pgFPGACORE signal
    
    while (!pgOK && k_uptime_get_32() - startTime < 500) { 

        if (gpio_pin_get_dt(&pgFPGACORE) > 0) {
            pgOK = true;
            LOG_INF("pgFPGACORE asserted after %u ms", k_uptime_get_32() - startTime);

            if (k_uptime_get_32() - startTime > 100) {
                LOG_WRN("Warning: pgFPGACORE took longer than 100ms to assert");
            }
        }
        k_msleep(5);
    }

    if (!pgOK) {
        LOG_ERR("Timeout waiting for pgFPGACORE!");
        return -ETIMEDOUT;
    }

    // Enable FPGA IO Power
    LOG_INF("Enabling FPGA IO power (enFPGAIO)...");
    gpio_pin_set_dt(&enFPGAIO, 1); // Physical High
    k_msleep(100); // Stabilization delay

    // Proceed with configuration
    char bitstreamPath[256];
    snprintf(bitstreamPath, sizeof(bitstreamPath), "%s/fpga.img", 
             FileSystem::instance().getMountPoint());
             
    int ret = loadBitstream(bitstreamPath);
    if (ret < 0) {
        initialized = false;
        return ret;
    }

    initialized = true;
    LOG_INF("FPGA initialized and running");
    return 0;
}

int FPGA::reset() {
    gpio_pin_set_dt(&fpgaCS, 0);    
    gpio_pin_set_dt(&fpgaNRESET, 0); 
    k_msleep(1);
    gpio_pin_set_dt(&fpgaNRESET, 1); 
    k_usleep(10);
    k_msleep(2);
    if (gpio_pin_get_dt(&fpgaDONE) > 0) {
        LOG_ERR("FPGA Error: CDONE is HIGH immediately after reset pulse!");
        return -EIO;
    }
    return 0;
}

int FPGA::loadBitstream(const char* path) {
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

    const size_t chunk_size = 2048;
    uint8_t* buffer = (uint8_t*)k_malloc(chunk_size);
    if (!buffer) {
        LOG_ERR("Failed to allocate bitstream buffer");
        fs_close(&file);
        return -ENOMEM;
    }

    if (reset() != 0) {
        k_free(buffer);
        fs_close(&file);
        return -EIO;
    }

    gpio_pin_set_dt(&fpgaCS, 1); 
    uint8_t dummy = 0x00;
    struct spi_buf s_dummy = { .buf = &dummy, .len = 1 };
    struct spi_buf_set s_dummies = { .buffers = &s_dummy, .count = 1 };
    spi_write_dt(&fpgaSPI, &s_dummies); 
    gpio_pin_set_dt(&fpgaCS, 0); 

    LOG_INF("Transmitting bitstream...");

    ssize_t bytesRead;
    size_t totalBytes = 0;
    struct spi_buf sBuf = { .buf = buffer, .len = chunk_size };
    struct spi_buf_set sBufs = { .buffers = &sBuf, .count = 1 };

    while ((bytesRead = fs_read(&file, buffer, chunk_size)) > 0) {
        sBuf.len = bytesRead;
        ret = spi_write_dt(&fpgaSPI, &sBufs);
        if (ret < 0) {
            LOG_ERR("SPI write error at %zu: %d", totalBytes, ret);
            k_free(buffer);
            fs_close(&file);
            gpio_pin_set_dt(&fpgaCS, 1); 
            return ret;
        }
        totalBytes += bytesRead;
    }

    fs_close(&file);
    LOG_INF("Transmitted %zu bytes", totalBytes);

    gpio_pin_set_dt(&fpgaCS, 1); 

    bool success = false;
    for (int i = 0; i < 20; i++) {

        if (gpio_pin_get_dt(&fpgaDONE) > 0) {
            success = true;
            break;
        }
        spi_write_dt(&fpgaSPI, &s_dummies);
    }

    if (success) {
        LOG_INF("FPGA SUCCESS: CDONE is HIGH");
        memset(buffer, 0x00, 8); 
        sBuf.len = 8;
        spi_write_dt(&fpgaSPI, &sBufs);
        ret = 0;
    } else {
        LOG_ERR("FPGA FAILURE: CDONE remains LOW");
        ret = -EAGAIN; 
    }

    k_free(buffer);
    return ret;
}

int FPGA::setFrequency(uint32_t freqHz) {
    currentFreq = freqHz;
    if (!initialized) return -ENODEV;

    uint64_t word = ((uint64_t)freqHz << 32) / tcxoFreqHz;
    return spiWriteReg(0x01, (uint32_t)word);
}

int FPGA::startTX() {
    if (!initialized) return -ENODEV;
    if (transmitting) return -EALREADY;
    LOG_INF("Starting transmission at %u Hz", currentFreq);
    transmitting = true;
    return spiWriteReg(0x00, 0x01); 
}

int FPGA::stopTX() {
    if (!initialized) return -ENODEV;
    if (!transmitting) return 0;
    LOG_INF("Stopping transmission");
    transmitting = false;
    return spiWriteReg(0x00, 0x00); 
}

int FPGA::setPowerLevel(uint8_t level) {
    if (!initialized) return -ENODEV;
    LOG_INF("Setting FPGA power level to %u", level);
    return spiWriteReg(0x04, (uint32_t)level);
}

int FPGA::sendSymbol(uint8_t symbol) {
    if (!initialized) return -ENODEV;
    return spiWriteReg(0x02, symbol);
}

int FPGA::setLPFBand(WSPRBand band) {
    currentBand = band;
    return 0;
}

uint32_t FPGA::getCounter() {
    if (!initialized) return 0;
    uint32_t val = 0;
    spiReadReg(0x03, &val);
    return val;
}

int FPGA::spiWriteReg(uint8_t reg, uint32_t value) {
    uint8_t txBuf[5];
    txBuf[0] = 0x80 | (reg & 0x7F);
    txBuf[1] = (value >> 24) & 0xFF;
    txBuf[2] = (value >> 16) & 0xFF;
    txBuf[3] = (value >> 8) & 0xFF;
    txBuf[4] = value & 0xFF;

    gpio_pin_set_dt(&fpgaCS, 0); 
    struct spi_buf sBuf = { .buf = txBuf, .len = sizeof(txBuf) };
    struct spi_buf_set sBufs = { .buffers = &sBuf, .count = 1 };
    int ret = spi_write_dt(&fpgaSPI, &sBufs);
    gpio_pin_set_dt(&fpgaCS, 1); 
    return ret;
}

int FPGA::spiReadReg(uint8_t reg, uint32_t* value) {
    uint8_t txBuf[1] = { (uint8_t)(reg & 0x7F) };
    uint8_t rxBuf[4] = { 0 };

    gpio_pin_set_dt(&fpgaCS, 0); 
    struct spi_buf sTX = { .buf = txBuf, .len = 1 };
    struct spi_buf_set sTXs = { .buffers = &sTX, .count = 1 };
    struct spi_buf sRX = { .buf = rxBuf, .len = 4 };
    struct spi_buf_set sRXs = { .buffers = &sRX, .count = 1 };
    int ret = spi_transceive_dt(&fpgaSPI, &sTXs, &sRXs);
    gpio_pin_set_dt(&fpgaCS, 1); 

    if (ret == 0) {
        *value = ((uint32_t)rxBuf[0] << 24) |
                 ((uint32_t)rxBuf[1] << 16) |
                 ((uint32_t)rxBuf[2] << 8) |
                 (uint32_t)rxBuf[3];
    }
    return ret;
}

} // namespace wspr
