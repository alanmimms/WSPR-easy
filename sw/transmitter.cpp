/* file: src/radio/Transmitter.cpp */
#include "Transmitter.hpp"
#include <zephyr/logging/log.h>
#include <cmath>

LOG_MODULE_REGISTER(tx_core, LOG_LEVEL_INF);

// WSPR Tone Spacing = 12000 / 8192 = 1.46484375 Hz
static constexpr double TONE_SPACING_HZ = 1.46484375;
static constexpr uint32_t SYMBOL_PERIOD_MS = 683; // ~0.6827s 

Transmitter::Transmitter(const struct device* spiDev, const struct gpio_dt_spec* csSpec) 
  : spi(spiDev), cs(csSpec) {}

void Transmitter::prepare(uint32_t dialFreqHz, std::string_view call, std::string_view grid, uint8_t dbm) {
  this->baseFreqHz = dialFreqHz;
  // Encode the message into 0..3 symbols
  this->currentSymbols = WsprEncoder::encode(call, grid, dbm);
  LOG_INF("TX Prepared: %u Hz, %s, %s", dialFreqHz, call.data(), grid.data());
}

uint32_t Transmitter::calculateTuningWord(uint32_t freqHz) {
  // FPGA Logic: The NCO drives a 6-step sequence.
  // Target Step Rate = 6 * Carrier Frequency.
  // Formula: TW = (Target_Hz * 2^32) / System_Clock
    
  // We use double for precision during calc, then cast to uint32
  double targetStepRate = (double)freqHz * 6.0; 
    
  // Factor = 2^32 / 180,000,000
  //        = 4294967296 / 180000000 ~= 23.8609
  double tuningWord = targetStepRate * (4294967296.0 / (double)FPGA_SYS_CLK);
    
  return (uint32_t)tuningWord;
}

void Transmitter::sendTuningWord(uint32_t word) {
  // FPGA expects Big Endian 32-bit word
  uint32_t txWord = __builtin_bswap32(word); // Convert LE to BE

  struct spi_buf buf = {
    .buf = &txWord,
    .len = sizeof(txWord)
  };
  struct spi_buf_set tx = {
    .buffers = &buf,
    .count = 1
  };
    
  // Using simple polling or blocking SPI since this is the only activity in this thread
  // Note: We manage CS manually if required, or let driver handle it if configured in DTS
  // Assuming DTS has cs-gpios configured, passing NULL to spi_write uses it automatically.
  // If we passed 'cs' explicitly, we would use spi_transceive_dt or similar struct.
    
  // However, fast updates usually benefit from holding CS if we were bursting, 
  // but here we update once per 0.6s.
  spi_write(spi, NULL, &tx);
}

void Transmitter::transmit() {
  if (!spi) {
    LOG_ERR("SPI device missing");
    return;
  }

  LOG_INF("Starting WSPR Transmission sequence...");
  transmitting = true;

  // 1. Pre-calculate the 4 tones to save math in the loop
  uint32_t toneWords[4];
  for(int i=0; i<4; i++) {
    // Frequency = Base + (Symbol * 1.4648 Hz)
    double toneFreq = (double)baseFreqHz + (i * TONE_SPACING_HZ);
    toneWords[i] = calculateTuningWord((uint32_t)toneFreq);
  }

  // 2. Synchronization
  // WSPR usually starts at the beginning of an even minute (UTC).
  // The Scheduler handles the minute-start. This function just runs the sequence.
    
  int64_t startTime = k_uptime_get();

  // 3. Real-Time Loop
  for (size_t i = 0; i < currentSymbols.size(); ++i) {
    uint8_t sym = currentSymbols[i];
        
    // Safety check
    if (sym > 3) sym = 0; 

    // Update FPGA NCO
    sendTuningWord(toneWords[sym]);

    // Precision Timing Wait
    // We calculate exactly when the NEXT symbol should start relative to StartTime
    // to prevent drift accumulation (accumulating sleep errors).
    int64_t nextSlot = startTime + ((i + 1) * 6827) / 10; // 682.7ms * (i+1)
        
    int64_t now = k_uptime_get();
    int32_t remaining = (int32_t)(nextSlot - now);

    if (remaining > 0) {
      k_msleep(remaining);
    }
  }

  // 4. End of Transmission
  // Turn off output (Set frequency to 0 or disable PA)
  // Sending 0 effectively stops the NCO phase accumulation
  sendTuningWord(0); 
    
  transmitting = false;
  LOG_INF("WSPR Transmission Complete.");
}
