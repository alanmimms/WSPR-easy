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

  bool enableTrace = true;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--notrace") {
      enableTrace = false;
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
  top->clk40MHz = 0;
  top->fpgaNCS = 1;
  top->fpgaClk = 0;
  top->fpgaMOSI = 0;
  top->gnssPPS = 0;

  std::cout << "Starting simulation..." << std::endl;
  // Let PLL lock
  for (int i = 0; i < 100; i++) {
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
  }

  SimSpi spi(top, &mainTime);

  // Set Frequency
  uint32_t freqHz = 5000000;
  // Update rate is 2 steps per clock cycle. 
  // Simulation clock is 40MHz, so update rate is 80Msps.
  uint32_t tuningWord = ((uint64_t)freqHz << 32) / 80000000ULL;

  std::cout << "Setting Tuning Word: 0x" << std::hex << tuningWord << std::dec << " for " << freqHz << " Hz at 80 Msps" << std::endl;
  spi.writeReg(0x01, tuningWord);

  uint32_t rb = spi.readReg(0x01);
  std::cout << "Readback Tuning: 0x" << std::hex << rb << std::dec << std::endl;

  // Test Case: Check driveNEN
  std::cout << "Initial driveNEN: " << (int)top->driveNEN << " (Expected 1)" << std::endl;
  
  std::cout << "Enabling TX..." << std::endl;
  spi.writeReg(0x00, 0x01); // TX EN = 1
  
  // Advance a bit for register to update (45MHz domain needs more cycles)
  for (int i = 0; i < 100; i++) {
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
  }
  std::cout << "After Enable driveNEN: " << (int)top->driveNEN << " (Expected 0)" << std::endl;

  int rfToggles = 0;
  for (int i = 0; i < 1000; i++) {
    bool old = top->rfPushBase;
    top->clk40MHz = !top->clk40MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 12500;
    if (top->rfPushBase != old) rfToggles++;
  }
  std::cout << "RF Toggles detected: " << rfToggles << std::endl;

  if (top->driveNEN == 0 && rfToggles > 0) {
    std::cout << "SUCCESS: driveNEN is active and RF is free-running!" << std::endl;
  } else {
    std::cout << "FAILURE!" << std::endl;
  }

  top->final();
  if (tfp) { tfp->close(); delete tfp; }
  delete top;
  return 0;
}
