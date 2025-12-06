#pragma once

#include "../sw/hal/hal.hpp"
#include "VTop.h"
#include <cstdint>
#include <vector>

/**
 * @brief SPI HAL implementation for Verilator simulation.
 *
 * This implementation drives the SPI signals in the VTop module
 * to send tuning words to the FPGA.
 */
class SimSpi : public HAL::ISpi {
public:
  SimSpi(VTop* top, vluint64_t* simTime)
    : top(top), simTime(simTime) {}

  void write(const uint8_t* data, size_t len) override {
    if (len != 4) {
      return; // Only support 32-bit writes
    }

    // Reconstruct 32-bit word (big-endian)
    uint32_t word = ((uint32_t)data[0] << 24) |
                    ((uint32_t)data[1] << 16) |
                    ((uint32_t)data[2] << 8) |
                    ((uint32_t)data[3]);

    // Assert CS
    top->fpgaNCS = 0;
    advanceClock(2);

    // Send 32 bits
    for (int i = 31; i >= 0; i--) {
      top->fpgaMOSI = (word >> i) & 1;
      top->fpgaClk = 1;
      advanceClock(1);
      top->fpgaClk = 0;
      advanceClock(1);
    }

    // Deassert CS
    top->fpgaNCS = 1;
    advanceClock(2);
  }

private:
  VTop* top;
  vluint64_t* simTime;

  void advanceClock(int cycles) {
    for (int i = 0; i < cycles; i++) {
      top->clk25MHz = !top->clk25MHz;
      top->eval();
      *simTime += 20000; // 20ns = 20000ps
    }
  }
};

/**
 * @brief Timer HAL implementation for Verilator simulation.
 *
 * Tracks simulation time in picoseconds.
 */
class SimTimer : public HAL::ITimer {
public:
  SimTimer(vluint64_t* simTime) : simTime(simTime) {}

  int64_t getUptimeMs() override {
    return *simTime / 1000000000LL; // ps to ms
  }

  int64_t getUptimePs() override {
    return *simTime;
  }

  void sleepMs(int32_t ms) override {
    // No-op in simulation
  }

private:
  vluint64_t* simTime;
};
