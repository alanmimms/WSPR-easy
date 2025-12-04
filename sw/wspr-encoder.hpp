/* file: src/radio/WsprEncoder.hpp */
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <cstdint>

class WSPREncoder {
public:
  // Standard WSPR message length is 162 symbols
  using SymbolBuffer = std::array<uint8_t, 162>;

  /**
   * @brief Generates the channel symbols for a standard WSPR Type 1 message.
   * @param callsign Standard callsign (e.g., "K1ABC")
   * @param grid 4-character Maidenhead grid (e.g., "FN42")
   * @param powerDbm Power in dBm (0-60, steps mapped to WSPR table)
   * @return 162-byte array where each byte is 0, 1, 2, or 3.
   */
  static SymbolBuffer encode(std::string_view callsign, std::string_view grid, uint8_t powerDbm);

private:
  static void packMessage(std::string_view call, std::string_view grid, uint8_t power, uint8_t* buffer);
  static void convolve(const uint8_t* input, uint8_t* output);
  static void interleave(const uint8_t* input, uint8_t* output);
  static void mergeSync(const uint8_t* input, SymbolBuffer& output);
};
