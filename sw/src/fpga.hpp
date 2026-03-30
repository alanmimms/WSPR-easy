/*
 * FPGA Control Module for WSPR-ease
 * Handles iCE40 FPGA communication via SPI
 */

#pragma once

#include <cstdint>

namespace wspr {

// WSPR band definitions (dial frequencies in Hz)
enum class WsprBand : uint32_t {
    Band160m = 1836600,
    Band80m  = 3568600,
    Band60m  = 5287200,
    Band40m  = 7038600,
    Band30m  = 10138700,
    Band20m  = 14095600,
    Band17m  = 18104600,
    Band15m  = 21094600,
    Band12m  = 24924600,
    Band10m  = 28124600,
    Band6m   = 50293000,
};

class Fpga {
public:
    static Fpga& instance();

    int init();
    int reset();
    int load_bitstream(const char* path);

    // Frequency control
    int set_frequency(uint32_t freq_hz);
    uint32_t frequency() const { return current_freq_; }

    // Transmission control
    int start_tx();
    int stop_tx();
    bool is_transmitting() const { return transmitting_; }

    // Send WSPR symbol (0-3) - 4-FSK modulation
    int send_symbol(uint8_t symbol);

    // LPF band switching
    int set_lpf_band(WsprBand band);
    WsprBand get_band() const { return current_band_; }

    uint32_t get_counter();

    bool is_initialized() const { return initialized_; }

private:
    Fpga() = default;

    int spi_write_reg(uint8_t reg, uint32_t value);
    int spi_read_reg(uint8_t reg, uint32_t* value);

    bool initialized_ = false;
    bool transmitting_ = false;
    uint32_t current_freq_ = 0;
    WsprBand current_band_ = WsprBand::Band20m;
};

} // namespace wspr
