/* file: src/radio/Transmitter.hpp */
#pragma once

#include "hal/hal.hpp"
#include "wspr-encoder.hpp"
#include <cstdint>
#include <string_view>

class Transmitter {
public:
  enum class State {
    IDLE,
    TRANSMITTING,
    DONE
  };

  Transmitter(HAL::ISpi* spi, HAL::ITimer* timer);

  void prepare(uint32_t dialFreqHz, std::string_view call, std::string_view grid, uint8_t dbm);
  
  // Starts the transmission process (non-blocking)
  void start();

  // Advances the state machine (call this in the simulation loop)
  void tick();

  State getState() const { return _state; }

private:
  // Hardware Abstraction
  HAL::ISpi* _spi;
  HAL::ITimer* _timer;

  // Config
  uint32_t baseFreqHz;
  WSPREncoder::SymbolBuffer currentSymbols;
    
  // State Machine
  State _state = State::IDLE;
  size_t _symbol_index = 0;
  int64_t _tx_start_time_ps = 0;
  uint32_t _toneWords[4];

  // Helpers
  void sendTuningWord(uint32_t word);
  uint32_t calculateTuningWord(uint32_t freqHz);
};
