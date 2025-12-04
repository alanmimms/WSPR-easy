WSPR-ease is a standalone, precision Multiband WSPR beacon covering the 80m through 10m Amateur Radio bands (3.5 MHz – 29.7 MHz).
It utilizes a hybrid ESP32-S3 + FPGA architecture to synthesize high-purity RF signals using a 6-Step "1-2-1" Harmonic Cancellation sequence. This approach eliminates the 3rd harmonic digitally, allowing for a simplified Low Pass Filter (LPF) design while maintaining atomic timing accuracy via GNSS discipline.
1. System Architecture
The system operates as a Software Defined Transmitter. The ESP32 calculates the WSPR symbols and frequency, while the FPGA generates the stepped RF waveform in real-time.
High-Level Data Flow
[GNSS/PPS] -> [ESP32-S3] --(SPI)--> [iCE40 FPGA] --(DDR IO)--> [74ACT Buffer] -> [MOSFET PA] -> [Transformer] -> [Switched LPF] -> [ANT]
 * Controller: ESP32-S3-WROOM-1 (N16R8). Manages Wi-Fi, Web UI, WSPR Encoding, Frequency Scheduling, and Band Switching.
 * RF Synthesis: Lattice iCE40UP5K. Implements a 32-bit NCO and 6-step sequencer running at 200 Msps (effective).
 * Timing: "Software GPSDO". A standard 25 MHz TCXO provides the clock, which is continuously calibrated against the GNSS PPS signal to achieve <0.1 Hz accuracy.
 * Wideband PA: A transformer-coupled Class D/E amplifier designed to operate from 3.5 MHz to 30 MHz.
2. Hardware Specifications
Core Components
| Component | Part Number | Package | Notes |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 | Module | 16MB Flash, 8MB Octal PSRAM. |
| FPGA | iCE40UP5K-SG48 | QFN-48 | Note: Use UP5K symbol (Pin 29=VCCPLL). |
| RF Buffer | 74ACT244 | TSSOP-20 | "T" (TTL) variant required for 3.3V logic translation. |
| MOSFETs | BS170 (x12) | SOT-23 | 3x Parallel per phase (Total 12). Low V_{GS(th)}. |
| Transformer | BN-43-202 | Core | Binocular. 4T Primary / 4T Secondary. Safe for 80m. |
| Oscillator | TCXO 25MHz | 3225 | 0.5ppm stability. |
| GNSS | ATGM336H / NEO-6M | Module | Must output PPS. |
| Diodes | 1N5817 / SS14 | SMA | Schottky. Required for Push1/Pull1 drain protection. |
Power Architecture
 * Input: USB-C (5.0V @ 1.2A Max).
 * 3.3V Rail: LDO (e.g., AP2112K). Powers ESP32, TCXO, GNSS, FPGA IO.
 * 1.2V Rail: LDO (e.g., TLV70012). Powers FPGA Core (VCC).
 * PLL Supply: Filtered 1.2V (10Ω Resistor + 10µF Cap to Pin 29).
 * PA Supply: Direct 5V VBUS with 22µF Bulk Capacitor.
3. Pinout & Interconnects
ESP32 <-> FPGA Interface
Uses FSPI (SPI2) on native pins for high-speed bitstream loading (Configuration) and Frequency Updates (Operation).
| Signal | ESP32 GPIO | FPGA Pin (SG48) | Function |
|---|---|---|---|
| SPI_CLK | IO 12 | Pin 15 | Config Clock & Tuning Clock |
| SPI_MOSI | IO 11 | Pin 17 | Config Data & Tuning Word |
| SPI_MISO | IO 13 | Pin 14 | Readback (PPS Counters) |
| SPI_CS | IO 14 | Pin 16 | Chip Select (Active Low) |
| FPGA_RST | IO 9 | Pin 8 | Hard Reset (CRESET_B) |
| FPGA_DONE | IO 10 | Pin 7 | Config Status (CDONE) |
| PPS_IN | IO 48 | Pin 6 | Shared GNSS Pulse Per Second |
Zephyr Peripheral Map
| Function | GPIO | Notes |
|---|---|---|
| USB Console | 19 / 20 | Native USB (D- / D+) |
| GNSS RXD | 43 | Connect to Module TX |
| GNSS TXD | 44 | Connect to Module RX |
| VBUS Mon | 8 | ADC1 Channel 7 (100k/100k Divider) |
Band Switching (LPF Control)
The relays/switches actuate based on the target frequency to select the appropriate Low Pass Filter bank.
| Signal | GPIO | Logic |
|---|---|---|
| LPF_hiON | IO 4 | High Band (e.g., 17m - 10m) |
| LPF_loON | IO 5 | Main Low Path Enable (Master Switch) |
| LPF_lo1ON | IO 6 | Low Band 1 (e.g., 80m - 40m) |
| LPF_lo2ON | IO 7 | Low Band 2 (e.g., 30m - 20m) |
4. FPGA Logic (Wideband NCO)
The FPGA logic is frequency-agnostic. It simply steps through the "1-2-1" sequence at a rate determined by the Tuning Word.
 * Clock: 100 MHz PLL (derived from 25 MHz TCXO).
 * Update Rate: 200 Msps (using DDR I/O).
 * Coverage:
   * 80m (3.5 MHz): Oversampling ratio ~57x. Extremely clean waveform.
   * 10m (29.7 MHz): Oversampling ratio ~6.7x. Clean waveform (3rd harmonic cancelled).
5. RF Power Amplifier
Topology
A Low-Side Tapped Transformer driver (Class D/E variant) optimized for 5V operation.
 * Driver: 74ACT244 (running at 5V).
 * PA: 4 Groups of 3x Parallel BS170.
 * Transformer: BN-43-202 Binocular Core.
   * Primary: 4 Turns Center Tapped (Tap at 2 Turns).
   * Secondary: 4 Turns.
   * Note: The 4-turn primary provides \sim 35\mu H inductance (X_L \approx 770\Omega @ 3.5 MHz), ensuring efficient operation down to the 80m band without saturation.
Protection Circuitry (Critical)
 * Flyback Diodes: 1N5817 Schottky diodes in series with the Drains of the Push 1 and Pull 1 MOSFET groups. This blocks reverse current during the negative half of the "1-2-1" cycle.
 * Gate Resistors: 10Ω series resistors on all Gate lines to dampen parasitic oscillation (ringing) which can occur due to the fast edges of the 74ACT244.
6. PCB Design Rules
 * Symbol Verification: Ensure the iCE40UP5K-SG48 symbol is used. (Pin 29 is VCCPLL, Pin 24 is VPP_2V5). Do not use LP1K symbols.
 * Decoupling:
   * FPGA VCCPLL (Pin 29): Must have 10Ω Series Resistor and 10µF Capacitor to GND.
   * RF PA: Place 22µF bulk capacitance right at the Transformer Center Tap.
 * Layout:
   * Keep the 74ACT -> MOSFET -> Transformer loop as short as possible.
   * Use Copper Pours for the MOSFET Drains (Heatsinking).
   * Keep GNSS Antenna input traces away from the 30 MHz RF switching nodes.
7. Firmware Operation (Zephyr)
 * Boot: ESP32 loads bitstream to FPGA via SPI.
 * Band Select: User selects Band (e.g., 20m). ESP32 sets LPF_loON + LPF_lo2ON HIGH.
 * Calibration: ESP32 measures PPS interval vs 25 MHz clock to determine true frequency.
 * Transmit: ESP32 computes NCO tuning word for target frequency + WSPR tone shift and updates FPGA over SPI at 1.46 Hz.


# NOTES
* Web UI needs to support both desktop and phone browser sizes and use cases.
