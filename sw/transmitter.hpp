/* file: src/radio/Transmitter.hpp */
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <cstdint>
#include <span>
#include "WsprEncoder.hpp"

class Transmitter {
public:
  Transmitter(const struct device* spiDev, const struct gpio_dt_spec* csSpec);

  /**
   * @brief Prepares the transmitter for a specific frequency and message.
   * Does not start transmission.
   */
  void prepare(uint32_t dialFreqHz, std::string_view call, std::string_view grid, uint8_t dbm);

  /**
   * @brief Starts the blocking real-time transmission loop.
   * This function blocks for ~2 minutes (110.6 seconds).
   * Run this in a dedicated thread.
   */
  void transmit();

  bool isTransmitting() const { return transmitting; }

private:
  // Hardware
  const struct device* spi;
  const struct gpio_dt_spec* cs;

  // Config
  uint32_t baseFreqHz;
  WsprEncoder::SymbolBuffer currentSymbols;
    
  // State
  volatile bool transmitting = false;
  static constexpr uint32_t FPGA_SYS_CLK = 180000000; // 180 MHz

  // Helpers
  void sendTuningWord(uint32_t word);
  uint32_t calculateTuningWord(uint32_t freqHz);
};
