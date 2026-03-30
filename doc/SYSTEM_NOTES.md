# WSPR-ease System Notes

## Hardware Configuration (ESP32-S3-WROOM-1-N8R8)

The system is designed around the ESP32-S3-WROOM-1-N8R8 module, which includes:
- **Flash:** 8MB Octal SPI (OPI) Flash.
- **PSRAM:** 8MB Octal SPI (OPI) PSRAM.
- **TCXO:** 40.000 MHz reference clock for the FPGA.

### FPGA-ESP32 Interconnect

The ESP32-S3 and the iCE40UP5K FPGA are connected via a shared SPI bus (FSPI/SPI2) that handles both initial configuration and real-time operation.

| Signal | ESP32 GPIO | FPGA Pin (SG48) | Function |
|---|---|---|---|
| SPI_CLK | IO 12 | Pin 15 | Config Clock & Tuning Clock |
| SPI_MOSI | IO 11 | Pin 17 | Config Data & Tuning Word |
| SPI_MISO | IO 13 | Pin 14 | Readback (PPS Counters) |
| SPI_CS | IO 14 | Pin 16 | Chip Select (Active Low) |
| FPGA_RST | IO 9 | Pin 8 | Hard Reset (CRESET_B) |
| FPGA_DONE | IO 10 | Pin 7 | Config Status (CDONE) |
| PPS_IN | IO 16 | Pin 6 | Shared GNSS Pulse Per Second |

---

## FPGA Internal Architecture

- **Input Clock:** 40 MHz TCXO on FPGA Pin 35.
- **Internal NCO Clock:** 100 MHz (Synthesized via PLL from 40 MHz).
- **PPS Counter:** 32-bit counter running at 100 MHz, latched on every PPS rising edge.
- **NCO:** 32-bit phase accumulator running at 100 MHz.
- **RF Drive:** DDR outputs producing a 6-step synthesized sine wave.

---

## Software Architecture (ZephyrOS)

WSPR-ease runs on **Zephyr RTOS** (v4.3+), providing a robust multitasking environment.

### Memory Management (PSRAM)
Due to the memory-intensive nature of the web server and network stack, internal DRAM is constrained.
- **Thread Stacks:** The Web Server (16KB), GNSS (4KB), and Captive DNS (2KB) thread stacks are dynamically allocated from the **heap** at runtime.
- **Heap Configuration:** The Zephyr heap is configured to include the 8MB of external PSRAM.
- **Flash Optimization:** Instructions and Rodata are kept in Flash to save DRAM, but PSRAM is used for large buffers.

### Build Workarounds
- **MSPI Timing Tuning:** When Octal Flash is enabled, the Espressif HAL requires timing tuning. This code expects `ESP_BOOTLOADER_OFFSET` to be defined. A global workaround is added in `sw/CMakeLists.txt`: `add_compile_definitions(ESP_BOOTLOADER_OFFSET=0x0)`.

---

## Storage & File System (LittleFS)

The ESP32-S3's flash is partitioned to include a **LittleFS** filesystem mounted at `/lfs`.

### LittleFS Usage
- **`fpga.img`**: The compiled FPGA bitstream. On boot, the ESP32 reads this file and loads it into the FPGA via bit-banging.
- **Web UI Files**: HTML, CSS, and JavaScript files for the web interface.
- **Configuration**: User settings (Callsign, Grid Square, Schedules) are stored in NVS.

### UI Notes
- **GNSS Display:** Displays Latitude, Longitude, Grid Square, and **Altitude (ASL)**.
- **Clock:** The 40MHz TCXO is a fixed hardware feature and is not reported as a dynamic status.
