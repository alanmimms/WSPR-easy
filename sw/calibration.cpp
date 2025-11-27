/* file: src/sys/CalibrationManager.cpp */
#include "CalibrationManager.hpp"
#include <zephyr/logging/log.h>
#include <cmath>

LOG_MODULE_REGISTER(calib, LOG_LEVEL_INF);

// Global instance pointer for ISR trampoline
static CalibrationManager* gCalibInstance = nullptr;

CalibrationManager::CalibrationManager(const struct device* spiDev, const struct gpio_dt_spec* ppsPin)
  : spi(spiDev), ppsGpio(ppsPin) {
  gCalibInstance = this;
}

void CalibrationManager::init() {
  // 1. Setup PPS Interrupt
  gpio_init_callback(&ppsCbData, ppsIsr, BIT(ppsGpio->pin));
  gpio_add_callback(ppsGpio->port, &ppsCbData);
  gpio_pin_interrupt_configure_dt(ppsGpio, GPIO_INT_EDGE_RISING);

  // 2. Init Work Item (to offload SPI reads from ISR)
  k_work_init(&ppsWork, ppsWorkHandler);
}

void CalibrationManager::ppsIsr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
  // HARDWARE INTERRUPT CONTEXT
  // 1. If we have a pending UTC time from NMEA, apply it to system clock NOW.
  //    (This sets the coarse time; millisecond adjustments happen via sys_clock_set)
  if (gCalibInstance && gCalibInstance->pendingUtcTime > 0) {
    struct timespec ts = {
      .tv_sec = gCalibInstance->pendingUtcTime,
      .tv_nsec = 0 // Top of the second
    };
    // Ideally: clock_settime(CLOCK_REALTIME, &ts); 
    // For Zephyr: we might just store the offset or use a custom time base.
    gCalibInstance->pendingUtcTime = 0; // Consumed
  }

  // 2. Trigger the work queue to read the FPGA counters
  if (gCalibInstance) {
    k_work_submit(&gCalibInstance->ppsWork);
  }
}

void CalibrationManager::ppsWorkHandler(struct k_work *work) {
  // THREAD CONTEXT
  gCalibInstance->processPpsEvent();
}

void CalibrationManager::processPpsEvent() {
  // Read the count of 25MHz ticks that occurred in the last second
  uint32_t measuredCounts = readFpgaCounter();

  // Sanity check: If count is 0 or wildly off, ignore (FPGA not ready?)
  if (measuredCounts < 24000000 || measuredCounts > 26000000) {
    LOG_WRN("Invalid FPGA PPS Count: %u", measuredCounts);
    return;
  }

  // Calculate Correction
  // If FPGA counted 25,000,100, our clock is FAST.
  // To generate a true 10 MHz, we generated (10M / 25M) * 25,000,100 cycles.
  // We need to command a LOWER frequency tuning word to compensate.
    
  double errorRatio = (double)measuredCounts / (double)NOMINAL_COUNTS;
    
  // Store reciprocal as the correction factor
  // Target_TW = (Desired_Hz * correction) ...
  // Wait, if Clock is FAST (Ratio > 1), we need to send a LARGER tuning word?
  // No. Tuning Word = (F_out * 2^32) / F_sys.
  // If F_sys is actually 25.0001 MHz, and we use 25.0 in math, we are outputting WRONG.
  // Correct Math: TW = (F_out * 2^32) / F_measured.
  // F_measured = F_nominal * errorRatio.
  // So TW = (F_out * 2^32) / (F_nominal * errorRatio)
  //       = Uncorrected_TW / errorRatio.
    
  double newFactor = 1.0 / errorRatio;
    
  // Simple low-pass filter (Exponential Moving Average) to reduce jitter
  double current = correctionFactor.load();
  double smoothed = (current * 0.9) + (newFactor * 0.1);
    
  correctionFactor.store(smoothed);

  LOG_DBG("PPS: Count=%u, Err=%.2f ppm, Factor=%.8f", 
	  measuredCounts, (errorRatio - 1.0) * 1e6, smoothed);
}

uint32_t CalibrationManager::readFpgaCounter() {
  // SPI Transaction to read register 0x01 (or whatever logic defined)
  // Assuming FPGA returns 4 bytes of count data on MISO
  // We send a dummy byte or command byte
    
  uint8_t txBuf[5] = {0x80, 0, 0, 0, 0}; // 0x80 = Read Command (example)
  uint8_t rxBuf[5] = {0};

  struct spi_buf tx = { .buf = txBuf, .len = 5 };
  struct spi_buf rx = { .buf = rxBuf, .len = 5 };
  struct spi_buf_set txSet = { .buffers = &tx, .count = 1 };
  struct spi_buf_set rxSet = { .buffers = &rx, .count = 1 };

  if (spi_transceive(spi, &spi_cfg, &txSet, &rxSet) == 0) {
    // Parse Big Endian 32-bit count from bytes 1-4
    uint32_t count = (rxBuf[1] << 24) | (rxBuf[2] << 16) | (rxBuf[3] << 8) | rxBuf[4];
    return count;
  }
  return 0;
}

uint32_t CalibrationManager::getCorrectedTuningWord(uint32_t targetFreqHz) const {
  // Base Calculation
  // TW = (Target * 6 * 2^32) / 180,000,000
  // We can pre-calc constant: K = (6 * 2^32) / 180M = 143.1655765
    
  constexpr double K = 143.16557653;
  double nominalTw = (double)targetFreqHz * K;
    
  // Apply Correction
  return (uint32_t)(nominalTw * correctionFactor.load());
}
