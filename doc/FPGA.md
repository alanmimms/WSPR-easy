# WSPR-ease FPGA Technical Reference

This document describes the internal architecture and SPI register
interface of the iCE40UP5K FPGA used in the WSPR-ease project.

## Architecture Overview

The FPGA performs real-time RF synthesis and timing measurement. To
support the 10m amateur band (30 MHz) with high-fidelity harmonic
cancellation, the FPGA operates at a **180 MHz** internal clock rate.

### Performance Requirements
*   **Clock Frequency:** 180 MHz (Required for 30 MHz RF at 6 samples
    per cycle).
*   **Update Rate:** 180 Msps (SDR) or 360 Msps (DDR).
*   **Timing Closure:** Achieving 180 MHz on the iCE40 requires
    aggressive pipelining of all 32-bit operations and decoupling of
    the SPI control plane from the RF synthesis data plane.

### RF Synthesis Chain
1.  **Skewed Pipelined NCO:** A 32-bit accumulator split into 16-bit
    halves. The low half adds in cycle $N$, and the high half adds in
    cycle $N+1$ using the captured carry. This ensures the critical
    path is limited to a 16-bit addition plus routing (~5.5ns).
2.  **Ring Counter:** A 6-step one-hot shifter triggered by the NCO
    carry.
3.  **1-2-1 Waveform:** Maps the 6 steps to MOSFET gates to cancel the
    3rd harmonic.
4.  **DDR Outputs:** RF drive signals use DDR (`SB_IO`) primitives to
    ensure high-speed edge alignment and electrical symmetry.

---

## SPI Interface Protocol (Decoupled)

The SPI interface is decoupled from the 180 MHz RF domain using
**Shadow Registers**.

*   **Writes:** Data is shifted into a shadow register and transferred
    to the 180 MHz domain only upon the completion of a full 40-bit
    frame (CS rising edge).
*   **Reads:** Data is latched from the 180 MHz domain into the SPI
    shift register at the 8th bit of the frame (immediately after the
    address is known). This prevents the large combinatorial readback
    MUX from interfering with RF synthesis timing.

### Frame Format (40-bit)
| Bits | Field | Description |
| :--- | :--- | :--- |
| 39 | **W/nR** | 1 = Write Operation, 0 = Read Operation |
| 38:32 | **Address** | 7-bit Register Address |
| 31:0 | **Data** | 32-bit Data (Payload) |

---

## Register Map

| Address | Name | Type | Description |
| :--- | :--- | :--- | :--- |
| 0x00 | **CONTROL** | R/W | Bit 0: `TX_EN`. Readback includes Status bits. |
| 0x01 | **TUNING** | R/W | NCO Tuning Word. $M = \frac{6 \cdot f_{out} \cdot 2^{32}}{f_{clk}}$ |
| 0x03 | **PPS_FALL** | RO | Counter latched at GNSS PPS falling edge. |
| 0x04 | **PWR_THRESH**| R/W | PWM Power Threshold (Bits 31:24). |
| 0x07 | **PPS_EDGES** | RO | Total count of GNSS PPS transitions. |
| 0x09 | **PPS_RISE** | RO | Counter latched at latest GNSS PPS rising edge. |
| 0x0A | **PPS_RISE_P**| RO | Counter latched at previous GNSS PPS rising edge. |
| 0x0B | **SIGNATURE** | RO | Fixed value `0x0000600D`. |

---

## Implementation Strategies for 180 MHz

### 16-bit Skewed Addition
To meet the 5.5ns timing window, 32-bit additions must be split:
```systemverilog
// Cycle N
{cL, accL} <= accL + tune[15:0];
tuneH_q <= tune[31:16];
// Cycle N+1
{p2Carry, accH} <= accH + tuneH_q + cL;
```

### Registered Readback
The readback MUX must be registered. By latching the data as soon as
the SPI address is decoded (bit 8), the MUX delay is removed from the
high-speed synthesis paths.
