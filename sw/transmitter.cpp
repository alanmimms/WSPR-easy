#include "transmitter.hpp"
#include <cmath>
#include <cstdio> // For printf

// WSPR Tone Spacing = 12000 / 8192 = 1.46484375 Hz
static constexpr double TONE_SPACING_HZ = 1.46484375;
// Symbol period is 8192/12000 s = 0.682666... s
static constexpr int64_t SYMBOL_PERIOD_PS = 682666666667;

Transmitter::Transmitter(HAL::ISpi* spi, HAL::ITimer* timer) 
  : _spi(spi), _timer(timer) {}

void Transmitter::prepare(uint32_t dialFreqHz, std::string_view call, std::string_view grid, uint8_t dbm) {
  this->baseFreqHz = dialFreqHz;
  this->currentSymbols = WSPREncoder::encode(call, grid, dbm);
  _state = State::IDLE;
}

void Transmitter::start() {
    if (_state == State::IDLE && _timer) {
        _state = State::TRANSMITTING;
        _symbol_index = 0;
        // Get time in picoseconds directly
        _tx_start_time_ps = _timer->getUptimePs();
        
        for(int i=0; i<4; i++) {
            double toneFreq = (double)baseFreqHz + (i * TONE_SPACING_HZ);
            _toneWords[i] = calculateTuningWord((uint32_t)toneFreq);
        }
    }
}

void Transmitter::tick() {
    if (_state != State::TRANSMITTING || !_timer) {
        return;
    }

    int64_t now_ps = _timer->getUptimePs();
    int64_t next_slot_time_ps = _tx_start_time_ps + (_symbol_index * SYMBOL_PERIOD_PS);

    if (now_ps >= next_slot_time_ps) {
        if (_symbol_index >= currentSymbols.size()) {
            sendTuningWord(0);
            _state = State::DONE;
            return;
        }

        uint8_t sym = currentSymbols[_symbol_index];
        if (sym > 3) sym = 0;

        sendTuningWord(_toneWords[sym]);
        _symbol_index++;
    }
}

uint32_t Transmitter::calculateTuningWord(uint32_t freqHz) {
  double targetStepRate = (double)freqHz * 6.0; 
  double tuningWord = targetStepRate * (4294967296.0 / 180000000.0);
  return (uint32_t)tuningWord;
}

void Transmitter::sendTuningWord(uint32_t word) {
  if (!_spi) return;
  uint32_t txWord = __builtin_bswap32(word);
  _spi->write(reinterpret_cast<uint8_t*>(&txWord), sizeof(txWord));
}
