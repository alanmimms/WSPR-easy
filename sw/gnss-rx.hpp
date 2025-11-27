/* file: src/dev/GnssReceiver.hpp */
#pragma once
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <mutex>
#include "../model/GnssData.hpp"

class GnssReceiver {
public:
  GnssReceiver(const struct device* uartDev);
  void init();

  /**
   * @brief Returns a copy of the most recently parsed GNSS data.
   * Thread-safe.
   */
  GnssData getLatestData() const;

private:
  const struct device* uart;
    
  // --- Buffering ---
  static constexpr size_t RCV_BUF_SIZE = 128;
  char rxBuffer[RCV_BUF_SIZE];
  size_t rxIndex = 0;
    
  // --- State ---
  mutable std::mutex dataMtx;
  GnssData latestData; // The "Master" copy
    
  // --- Processing ---
  void processLine(std::string_view line, int64_t timestamp);
  void parseRmc(std::string_view line, GnssData& target);
  void parseGga(std::string_view line, GnssData& target);
  static std::string latLonToGrid(double lat, double lon);
    
  // UART Callback
  static void uartCallback(const struct device *dev, void *user_data);
};
