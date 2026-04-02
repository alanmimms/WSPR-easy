#include "verilated.h"
#include "verilated_vcd_c.h"
#include "VTop.h"
#include "simHAL.hpp"
#include <iostream>
#include <cstdint>
#include <memory>
#include <iomanip>

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  VTop* top = new VTop;

  // Parse command line options
  bool enableTrace = true;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--notrace") {
      enableTrace = false;
      std::cout << "Tracing disabled for faster simulation" << std::endl;
    }
  }

  VerilatedVcdC* tfp = nullptr;
  if (enableTrace) {
    tfp = new VerilatedVcdC;
    Verilated::traceEverOn(true);
    top->trace(tfp, 99);
    tfp->open("waveform.vcd");
  }

  vluint64_t mainTime = 0;

  // Initialize inputs
  top->clk40MHz = 0;
  top->fpgaNCS = 1;
  top->fpgaClk = 0;
  top->fpgaMOSI = 0;
  top->gnssPPS = 0;

  // Reset phase
  std::cout << "Starting simulation..." << std::endl;
  for (int i = 0; i < 100; i++) {
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
  }

  // Create HAL instances
  SimSpi spi(top, &mainTime);

  // Test Case 1: Write Tuning Word
  // 5.0 MHz target
  // Tuning Reference = 40 MHz
  uint32_t freqHz = 5000000;
  uint32_t tuningWord = ((uint64_t)freqHz << 32) / 40000000ULL;
  
  std::cout << "Setting Frequency: " << freqHz << " Hz (Tuning Word: 0x" 
            << std::hex << tuningWord << ")" << std::dec << std::endl;
  spi.writeReg(0x01, tuningWord);

  uint32_t readBackTuning = spi.readReg(0x01);
  std::cout << "Tuning Word Readback: 0x" << std::hex << readBackTuning << std::dec << std::endl;

  // Test Case 2: Enable TX
  std::cout << "Enabling TX..." << std::endl;
  spi.writeReg(0x00, 0x01); // Bit 0: TX EN

  // Run for a bit to see RF activity (longer for pipelined logic)
  int rfToggleCount = 0;
  for (int i = 0; i < 5000; i++) {
    bool oldPush = top->rfPushBase;
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
    if (top->rfPushBase != oldPush) rfToggleCount++;
  }

  std::cout << "RF PushBase Toggles detected: " << rfToggleCount << std::endl;
  if (rfToggleCount > 10) {
    std::cout << "SUCCESS: RTL is producing RF signals!" << std::endl;
  } else {
    std::cout << "FAILURE: RTL is SILENT!" << std::endl;
  }

  // Test Case 3: Power Control (50% power)
  std::cout << "Setting Power to 50%..." << std::endl;
  spi.writeReg(0x04, 0x7FFFFFFF); // 50% threshold

  for (int i = 0; i < 2000; i++) {
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
  }

  // Test Case 4: Register Read (PPS Counter)
  // Simulate a PPS pulse
  std::cout << "Simulating PPS pulse..." << std::endl;
  top->gnssPPS = 1;
  for (int i = 0; i < 100; i++) {
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
  }
  top->gnssPPS = 0;
  
  // Give it a cycle to latch
  for (int i = 0; i < 10; i++) {
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
  }

  uint32_t ppsCount = spi.readReg(0x03);
  std::cout << "PPS Counter Read: " << ppsCount << " (Expected ~100-200 cycles of 100MHz clock)" << std::endl;

  // Finalize
  std::cout << "Simulation finished at " << (mainTime / 1000000) << " us" << std::endl;
  
  top->final();
  if (tfp) {
    tfp->close();
    delete tfp;
  }
  delete top;

  return 0;
}
