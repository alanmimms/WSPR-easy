#include "verilated.h"
#include "verilated_vcd_c.h"
#include "VTop.h"
#include "sim_hal.hpp"
#include "../sw/transmitter.hpp"
#include "../sw/wspr-encoder.hpp"
#include <iostream>
#include <cstdint>
#include <memory>

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  VTop* top = new VTop;

  // Parse command line options
  bool enableTrace = true;
  bool fastForward = false;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--notrace") {
      enableTrace = false;
      std::cout << "Tracing disabled for faster simulation" << std::endl;
    } else if (std::string(argv[i]) == "--fastforward") {
      fastForward = true;
      std::cout << "Fast-forward mode enabled (event-driven simulation)" << std::endl;
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
  vluint64_t lastUsPrintTime = 0;
  bool fastMode = true; // Skip detailed waveform during idle periods

  // Initialize inputs
  top->clk25MHz = 0;
  top->nFpgaCS = 1;
  top->fpgaClk = 0;
  top->fpgaMOSI = 0;
  top->gnssPPS = 0;

  // Reset phase
  std::cout << "Starting reset..." << std::endl;
  for (int i = 0; i < 20; i++) {
    top->clk25MHz = !top->clk25MHz;
    top->eval();
    if (tfp) tfp->dump(mainTime);
    mainTime += 20000;
  }
  std::cout << "Reset complete." << std::endl;

  // Create HAL instances
  SimSpi spiHal(top, &mainTime);
  SimTimer timerHal(&mainTime);

  // Create transmitter instance
  Transmitter tx(&spiHal, &timerHal);

  // Prepare WSPR message
  // 20m band (14.097 MHz dial frequency)
  const uint32_t dialFreqHz = 14097000;
  const char* callsign = "W1ABC";
  const char* grid = "FN42";
  const uint8_t powerDbm = 23;

  tx.prepare(dialFreqHz, callsign, grid, powerDbm);
  tx.start();

  std::cout << "Transmitter started at " << mainTime << " ps" << std::endl;
  std::cout << "Running transmission simulation..." << std::endl;

  // Run simulation
  // WSPR transmission is 162 symbols * 0.6827 seconds = ~110 seconds
  // Fast mode: run for 3 symbol periods (~2 seconds) to verify operation
  vluint64_t maxSimTime = 3000000000000LL; // 3 seconds in picoseconds
  int symbolsTransmitted = 0;
  int spiTransactions = 0;
  Transmitter::State lastState = tx.getState();
  vluint64_t nextSymbolTime = 682666666667LL; // First symbol at ~0.68s

  while (mainTime < maxSimTime && spiTransactions < 3) {
    // Print progress every 100ms
    if (mainTime >= lastUsPrintTime + 100000000000LL) {
      std::cout << "Sim time: " << (mainTime / 1000000000LL) << " ms, "
                << "Symbols sent: " << spiTransactions << ", "
                << "State: " << (int)tx.getState() << std::endl;
      lastUsPrintTime = mainTime;
    }

    // Fast-forward mode: Skip to just before next event
    if (fastForward) {
      const vluint64_t FF_MARGIN = 10000000LL; // 10us before event
      vluint64_t timeToNextSymbol = nextSymbolTime - mainTime;

      if (timeToNextSymbol > FF_MARGIN) {
        // Jump forward without hardware evaluation
        mainTime = nextSymbolTime - FF_MARGIN;
        std::cout << "  [Fast-forward to " << (mainTime / 1000000000LL)
                  << " ms]" << std::endl;
        continue;
      }
    }

    // Advance the transmitter state machine (call periodically, not every clock)
    if ((mainTime % 1000000) == 0) { // Call every 1us
      tx.tick();
    }

    // Detect SPI transaction (new symbol) - check if we just crossed boundary
    if (mainTime >= nextSymbolTime && spiTransactions < 3) {
      spiTransactions++;
      nextSymbolTime += 682666666667LL;
      std::cout << "Symbol " << spiTransactions << " sent at "
                << (mainTime / 1000000000LL) << " ms" << std::endl;
    }

    // Advance simulation clock
    top->clk25MHz = !top->clk25MHz;
    top->eval();

    // Only trace interesting periods to reduce file size
    if (tfp) {
      bool traceThisCycle = !fastMode ||
                            (mainTime < 10000000000LL) || // First 10ms
                            (top->rfPushBase || top->rfPushPeak ||
                             top->rfPullBase || top->rfPullPeak) || // RF active
                            (top->nFpgaCS == 0); // SPI active

      if (traceThisCycle) {
        tfp->dump(mainTime);
      }
    }

    mainTime += 20000; // 20ns per half-cycle = 25 MHz

    // Monitor RF outputs (count active cycles)
    if (top->rfPushBase || top->rfPushPeak || top->rfPullBase || top->rfPullPeak) {
      symbolsTransmitted++;
    }
  }

  // Finalize
  top->final();
  if (tfp) {
    tfp->close();
    delete tfp;
  }
  delete top;

  std::cout << "\n=== Simulation Summary ===" << std::endl;
  std::cout << "Simulation time: " << (mainTime / 1000000000LL) << " ms" << std::endl;
  std::cout << "Transmitter state: " << (int)tx.getState() << std::endl;
  std::cout << "SPI transactions: " << spiTransactions << std::endl;
  std::cout << "RF activity cycles: " << symbolsTransmitted << std::endl;

  if (spiTransactions >= 3) {
    std::cout << "\nSUCCESS: Transmitted 3+ symbols via SPI to FPGA!" << std::endl;
  } else {
    std::cout << "\nWARNING: Only " << spiTransactions << " symbols transmitted" << std::endl;
  }

  return 0;
}
