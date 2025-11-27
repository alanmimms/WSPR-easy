/* file: src/sys/CalibrationManager.hpp */
#pragma once
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <atomic>
#include <mutex>

class CalibrationManager {
public:
  struct CalibrationStats {
    double frequencyErrorPpm;
    uint32_t lastFpgaCount;
    bool isLocked;
  };

  CalibrationManager(const struct device* spiDev, const struct gpio_dt_spec* ppsPin);

  void init();
    
  // Called by Transmitter to get the exact Tuning Word
  uint32_t getCorrectedTuningWord(uint32_t targetFreqHz) const;

  // Called by NMEA parser when we have a valid UTC time
  void setNextPpsTime(time_t utcTime);

  CalibrationStats getStats() const;

private:
  // Hardware
  const struct device* spi;
  const struct gpio_dt_spec* ppsGpio;
  struct gpio_callback ppsCbData;

  // State
  std::atomic<double> correctionFactor { 1.0 }; // Multiplier (e.g., 0.999996)
  std::atomic<time_t> pendingUtcTime { 0 };
    
  // Synchronization
  struct k_work ppsWork;
    
  // FPGA Constants
  static constexpr uint32_t NOMINAL_COUNTS = 25000000;

  // ISR and Work Handlers
  static void ppsIsr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
  static void ppsWorkHandler(struct k_work *work);
    
  void processPpsEvent();
  uint32_t readFpgaCounter();
};
