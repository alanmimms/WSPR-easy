# WSPR-ease FPGA Technical Reference

This document describes the internal architecture and SPI register
interface of the iCE40UP5K FPGA used in the WSPR-ease project.

## Architecture Overview

The FPGA performs real-time RF synthesis and timing measurement. To
support the 10m amateur band (30 MHz) with high-fidelity harmonic
cancellation, the FPGA operates at a **90 MHz** internal clock rate.

### Performance Requirements
*   **Clock Frequency:** 90 MHz DDR (Required for 30 MHz RF at 6 samples
    per cycle).
*   **Timing Closure:** Achieving 90 MHz on the iCE40 requires
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

The SPI interface is decoupled from the 90 MHz RF domain using
**Shadow Registers**.

*   **Writes:** Data is shifted into a shadow register and transferred
    to the 90 MHz domain only upon the completion of a full 40-bit
    frame (CS rising edge).
*   **Reads:** Data is latched from the 90 MHz domain into the SPI
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
| 0x00 | **CONTROL** | R/W | `[31:24]` Power Threshold<br>`[23:2]` Reserved<br>`[1]` PLL Locked (Read Only)<br>`[0]` TX Enable |
| 0x01 | **TUNING** | R/W | 32-bit NCO Tuning Word. $M = \frac{6 \cdot f_{out} \cdot 2^{32}}{f_{clk}}$ |
| 0x03 | **PPSFALL** | RO | Counter latched at GNSS PPS falling edge. |
| 0x07 | **PPSEDGES** | RO | Total count of GNSS PPS transitions. |
| 0x09 | **PPSRISE** | RO | Counter latched at latest GNSS PPS rising edge. |
| 0x0A | **PPSRIPEP**| RO | Counter latched at previous GNSS PPS rising edge. |
| 0x0B | **SIGNATURE** | RO | Fixed value `0x0000600D`. |

---

## NCO Operation

The Numerically Controlled Oscillator (NCO) is the core of the RF
synthesis engine. It generates a 6-step synthesized sine wave with high
frequency precision and minimal jitter.

### Logical Level
At the logical level, the NCO is a 32-bit phase accumulator. In each
clock cycle, a tuning word $M$ is added to the current phase:
$$Phase(t+1) = (Phase(t) + M) \pmod{2^{32}}$$

The output frequency $f_{out}$ is determined by:
$$f_{out} = \frac{M \cdot f_{sample}}{2^{32}}$$

Because the design uses **DDR (Double Data Rate)** outputs, the FPGA
produces two samples per 90 MHz clock cycle, resulting in an effective
sample rate $f_{sample} = 180\text{ MHz}$.

#### Phase-to-State Mapping
The $360^\circ$ phase circle is divided into 6 discrete states, each
representing a $60^\circ$ wedge. The state is determined by:
$$State = \lfloor\frac{Phase \cdot 6}{2^{32}}\rfloor$$

These 6 states are mapped to the RF pins to produce a **1-2-1 weighted
synthesized sine wave**. This weighting significantly reduces the 3rd
harmonic ($18.5\text{ dB}$ suppression) compared to a raw square wave:

| State | Phase Wedge | Weight | Active Pins |
| :--- | :--- | :--- | :--- |
| 0 | $0^\circ - 60^\circ$ | +1 | `rfPushBase` |
| 1 | $60^\circ - 120^\circ$ | +2 | `rfPushBase` + `rfPushPeak` |
| 2 | $120^\circ - 180^\circ$ | +1 | `rfPushBase` |
| 3 | $180^\circ - 240^\circ$ | -1 | `rfPullBase` |
| 4 | $240^\circ - 300^\circ$ | -2 | `rfPullBase` + `rfPullPeak` |
| 5 | $300^\circ - 360^\circ$ | -1 | `rfPullBase` |

### Detailed Pipelining (90 MHz Timing)
A standard 32-bit carry chain on the iCE40UP5K is too slow to operate at
90 MHz. To achieve timing closure, the synthesis chain is implemented
as a deep pipeline.

#### Accumulation (T1 - T10)
The 32-bit addition is distributed over 8 clock cycles using 4-bit
chunks. Each stage handles 4 bits and passes the carry to the next stage
in the following cycle.

#### Deskewing & Alignment (T11)
To form a coherent phase word, the bits from different cycles must be
aligned. The lower bits are delayed (7 cycles for LSBs down to 0 for
MSBs) so that the final 32-bit word represents a single point in time.

#### DDR Prediction (T12)
To achieve 180 Msps, the FPGA calculates the phase for both edges of the
90 MHz clock. The falling edge phase is predicted by adding exactly half
of the tuning word ($M/2$) to the current phase. By using the full
32-bit width for this prediction, the NCO eliminates sub-cycle jitter.

#### Scaling (T13 - T15)
The calculation $State = Phase \cdot 6 >> 32$ is performed using a
pipelined shift-and-add: $(Phase \ll 2) + (Phase \ll 1)$. Only the top
bits are used to minimize logic depth.

#### Decoding & Output (T16 - T19)
*   **T16:** Decodes the 6 states into raw pin bits.
*   **T17:** Registers the bits to break the timing path from the
    decoder logic.
*   **T18:** Final registration. Falling-edge bits are transferred to a
    `negedge` register to provide a full $11.1\text{ ns}$ routing window
    to the I/O block.
*   **T19:** Data is packed into `SB_IO` primitives for DDR transmission.

---

## Implementation Strategies for 90 MHz

### NexRx Project Insights
These are ideas from the NexRx project, which needs four NCOs in
frequency ranges of 1MHz to 180MHz. They are untested, but act as a
guideline for this project's NCOs and are the basis of what follows.

The frustration—97 MHz is a common "glass ceiling" for the iCE40UP5K
when using a standard 32-bit architecture, as the routing delays
between Logic Clusters often become the dominant factor regardless of
how clean the carry-chain logic is.

However, the NexRx architecture hits **180 MHz** by moving away from a
single 32-bit "super-adder" in a single clock cycle and employing a
**multi-stage pipelined accumulator**.

### Why your 97 MHz limit exists
In the iCE40, even if the carry-chain is fast, the distance from the
last bit of the adder back to the first bit (the feedback loop) is
where the timing fails. At 180 MHz, you only have **5.55 ns** for the
entire round trip. If your design has even a small amount of routing
congestion, the "routing delay" alone can take up 3–4 ns, leaving
almost no time for logic.

### How NexRx achieves 180 MHz
The NexRx FPGA logic (designed in `rtl/nexrx-top.sv`) uses the
following "Extreme Pipelining" strategy to reach those frequencies:

#### Segmented Phase Accumulation
Instead of one 32-bit addition, the accumulator is split into four
**8-bit stages**.
* **Stage 1**: Adds the lowest 8 bits. The result is registered.
* **Stage 2**: Adds the next 8 bits plus the registered carry from
  Stage 1. This result is also registered.
* **Alignment**: To keep the phases correct, the upper bits of the
  frequency control word are delayed (pipelined) so they arrive at the
  adder at the exact same time as the carry bit from the previous
  stage.

#### The "Walking Ring" Parallelism
The Walking Ring counters for the QSD stages (QSD0/1 at 4-phase and
QSD2 at 6-phase) are implemented as simple **one-hot shift
registers**.
* These shift registers only look at the "overflow" (MSB) of the
  pipelined NCO.
* Because a shift register is just a series of Flip-Flops with almost
  zero logic between them, they can easily toggle at the full 180 MHz
  rate of the Master Clock.



#### Registering the I/O Cells
The NexRx design utilizes the **SB_IO** primitive's internal
registers.
* By "packing" the final clock output flip-flop directly into the I/O
  cell (the physical pin driver), the delay from the last logic gate
  to the pin is minimized and, more importantly, **deterministic**.
* This removes the unpredictable "routing delay" from the final stage
  of the timing path.

### Comparison of Timing Limits
| Feature | Standard 32-bit NCO | NexRx Pipelined NCO |
| :--- | :--- | :--- |
| **Logic Depth** | 1 stage (32-bit) | 4 stages (8-bit each) |
| **Max Logic Delay** | ~6-8 ns | ~1.5-2.0 ns |
| **Routing Slack** | Minimal/Negative | Significant (>3 ns) |
| **Achievable Clock** | ~90-100 MHz | **180-200 MHz** |



### Check your "Nextpnr" Constraints
If you want to push your 97 MHz up to the 180 MHz required for the
**$6 \times f$** QSD2 clock, try adding the following to your
synthesis flow:
1. **`--freq 180`**: Explicitly tell the tool your target is 180 MHz.
2. **`--placer heap`**: This often finds better placements for
   high-speed paths than the default.
3. **Pipelining**: If your code is `acc <= acc + inc;`, it will never
   hit 180 MHz. You must manually break `acc` into smaller registers.

If you are open to it, I can walk you through the specific
SystemVerilog code NexRx uses for this segmented NCO. It’s a bit more
complex to write, but it's the only way to get "SDR-grade" performance
out of this $5 FPGA.

# Timing and Brass Tacks

If you're building the production bitstream to hit that 180 MHz target
on the iCE40UP5K, you need the exact delay-matched, 4-stage pipelined
architecture.

When you slice a 32-bit adder into four 8-bit chunks, the Carry Out
(`c0`, `c1`, `c2`) takes one clock cycle to move to the next stage.
Because of this, the upper bytes of your Frequency Control Word
(`fcw`) must be delayed by shift registers so they arrive at their
respective adders at the exact same time the carry bit arrives.
Furthermore, the lower bytes of the output sum must be delayed at the
end so the full 32-bit phase vector aligns coherently.

Here is the production-ready SystemVerilog RTL designed specifically
for timing closure on the Lattice iCE40 architecture.



### Production 180 MHz Pipelined NCO RTL

```systemverilog `default_nettype none

module nco_pipelined_32b ( input wire clk, // 180 MHz Master Clock
    input wire rst, // Synchronous Reset input wire [31:0] fcw, //
    Frequency Control Word (Increment) output reg [31:0] phase_out, //
    Fully aligned 32-bit phase output wire msb_out // MSB for driving
    the Walking Ring );

    // =========================================================================
    // INPUT DELAY MATCHING (FCW Pipeline)
    // =========================================================================
    // Stage 0 (Bits 7:0)   needs 0 cycles of delay.
    // Stage 1 (Bits 15:8)  needs 1 cycle of delay.
    // Stage 2 (Bits 23:16) needs 2 cycles of delay.
    // Stage 3 (Bits 31:24) needs 3 cycles of delay.
    
    reg [15:8]  fcw_s1_d1;
    reg [23:16] fcw_s2_d1, fcw_s2_d2;
    reg [31:24] fcw_s3_d1, fcw_s3_d2, fcw_s3_d3;

    always_ff @(posedge clk) begin
        if (rst) begin
            fcw_s1_d1 <= 8'd0;
            fcw_s2_d1 <= 8'd0; fcw_s2_d2 <= 8'd0;
            fcw_s3_d1 <= 8'd0; fcw_s3_d2 <= 8'd0; fcw_s3_d3 <= 8'd0;
        end else begin
            // Stage 1 delay
            fcw_s1_d1 <= fcw[15:8];
            
            // Stage 2 delays
            fcw_s2_d1 <= fcw[23:16];
            fcw_s2_d2 <= fcw_s2_d1;
            
            // Stage 3 delays
            fcw_s3_d1 <= fcw[31:24];
            fcw_s3_d2 <= fcw_s3_d1;
            fcw_s3_d3 <= fcw_s3_d2;
        end
    end

    // =========================================================================
    // PIPELINED ACCUMULATOR STAGES
    // =========================================================================
    reg [7:0] acc0, acc1, acc2, acc3;
    reg       c0, c1, c2;

    // Combinatorial sums to utilize iCE40 SB_CARRY primitive mapping natively
    wire [8:0] sum0 = {1'b0, acc0} + {1'b0, fcw[7:0]};
    wire [8:0] sum1 = {1'b0, acc1} + {1'b0, fcw_s1_d1} + {8'd0, c0};
    wire [8:0] sum2 = {1'b0, acc2} + {1'b0, fcw_s2_d2} + {8'd0, c1};
    wire [8:0] sum3 = {1'b0, acc3} + {1'b0, fcw_s3_d3} + {8'd0, c2};

    always_ff @(posedge clk) begin
        if (rst) begin
            acc0 <= 8'd0; c0 <= 1'b0;
            acc1 <= 8'd0; c1 <= 1'b0;
            acc2 <= 8'd0; c2 <= 1'b0;
            acc3 <= 8'd0;
        end else begin
            acc0 <= sum0[7:0]; c0 <= sum0[8];
            acc1 <= sum1[7:0]; c1 <= sum1[8];
            acc2 <= sum2[7:0]; c2 <= sum2[8];
            acc3 <= sum3[7:0]; // Final carry out is discarded in an NCO
        end
    end

    // =========================================================================
    // OUTPUT PHASE ALIGNMENT
    // =========================================================================
    // Because acc3 is 3 clock cycles behind acc0, we must delay the lower
    // accumulator bytes so the final 32-bit phase word is coherent.
    
    reg [7:0] acc0_d1, acc0_d2, acc0_d3;
    reg [7:0] acc1_d1, acc1_d2;
    reg [7:0] acc2_d1;

    always_ff @(posedge clk) begin
        if (rst) begin
            acc0_d1 <= 8'd0; acc0_d2 <= 8'd0; acc0_d3 <= 8'd0;
            acc1_d1 <= 8'd0; acc1_d2 <= 8'd0;
            acc2_d1 <= 8'd0;
            phase_out <= 32'd0;
        end else begin
            // Re-align Stage 0
            acc0_d1 <= acc0;
            acc0_d2 <= acc0_d1;
            acc0_d3 <= acc0_d2;
            
            // Re-align Stage 1
            acc1_d1 <= acc1;
            acc1_d2 <= acc1_d1;
            
            // Re-align Stage 2
            acc2_d1 <= acc2;
            
            // Assemble coherent 32-bit output
            phase_out <= {acc3, acc2_d1, acc1_d2, acc0_d3};
        end
    end

    // The MSB is what drives the Walking Ring counters for the QSD clocks
    assign msb_out = phase_out[31];

endmodule
`default_nettype wire
```

### Why this achieves Timing Closure in Yosys/Nextpnr:
1. **Critical Path Isolation**: The longest logic path in this entire
   module is exactly one 8-bit carry chain (`sumX` calculation) into a
   Flip-Flop (`accX`). The iCE40UP5K easily clears an 8-bit carry
   chain within 2-3 ns, leaving plenty of slack for the 5.55 ns budget
   at 180 MHz.
2. **Shift-Register Packing**: Yosys is smart enough to map the delay
   pipelines (`fcw_s3_d1` -> `d2` -> `d3`) tightly without burning
   complex routing.
3. **No Deep Routing**: Standard 32-bit adders fail at 97 MHz because
   the logic spans across multiple physical logic blocks (PLBs) on the
   die, and the feedback from bit 31 back to bit 0 for the next cycle
   is too long physically. This pipeline confines all routing to
   small, localized 8-bit islands.

**To synthesize this properly:** Ensure you feed this to
`nextpnr-ice40` with `--freq 180`. Connect the `msb_out` wire directly
to your Walking Ring logic, and you will have your pristine 180 MHz
NCO foundation.

# WSPR-Ease Project Actual Implementation Notes

### 4-bit Pipelined Addition
To meet the 11.1ns clock period (with routing margin), all wide
arithmetic is broken into 4-bit chunks. This limits the carry chain
length to a single Logic Cell cluster, ensuring extremely fast
propagation.

### Synchronized Reset
The `fpgaNRESET` signal is an asynchronous input to the FPGA. To avoid
timing violations on the high-speed synthesis nets, it is synchronized
to the 90 MHz domain using a 2-stage shift register before being used as
a global reset (`reset90`).


To achieve timing closure at 90 MHz on the iCE40UP5K while supporting
180 Msps RF synthesis (via DDR), we adopt an architecture based on
"Extreme Pipelining" as suggested in the NexRx project's research.

## Architectural Choices

### Step-Clock NCO vs. Phase-to-State Mapping
The original design attempted to calculate `State = (Phase * 6) >>
32`. This multiplication (or multiple shift-adds) creates a deep
combinatorial path that fails to meet 11.1ns timing at 90 MHz.

Instead, we use a **Step-Clock NCO** approach:
*   The NCO is tuned to $f_{step} = 6 \times f_{out}$.
*   Every time the NCO phase overflows $2^{32}$, it represents a
    $60^\circ$ advance in the RF cycle.
*   We use a 6-step **Walking Ring** (one-hot or small counter) that
    advances on NCO overflows.
*   This removes the need for wide multiplication, replacing it with a
    simple ring increment.

### Segmented Accumulator
Following `TIMING.md`, we slice the 32-bit phase accumulator into four
**8-bit segments**.
*   Each segment has its own register and carry bit.
*   Segments are pipelined such that the carry from bit 7 to 8 takes
    one clock cycle, bit 15 to 16 takes another, etc.
*   The Tuning Word and output phase are deskewed using delay-matching
    shift registers.

### Pipelined Duty Cycle Control
Power control is implemented by inserting "dead time" into each
$60^\circ$ step.
*   The NCO phase $P \in [0, 2^{32}-1]$ represents the position within
    the current step.
*   We compare the top 8 bits of $P$ with a `powerThreshold`.
*   This comparison is pipelined to ensure it does not sit on the
    critical path.

### DDR Handling at 90 MHz
Since we need two samples per 90 MHz cycle (180 Msps):
*   We calculate two increments per clock: $P_{mid} = P_{start} + M$
    and $P_{next} = P_{mid} + M$.
*   Overflows from both $P_{mid}$ and $P_{next}$ are captured.
*   The Walking Ring advances by 0, 1, or 2 steps depending on these
    overflows.

### I/O Registration
We utilize the `SB_IO` primitive's internal registers and the
`negedge` clock for the falling-edge DDR sample to ensure perfect edge
alignment and minimal routing jitter.

## Nextpnr Optimization
We explicitly target 90 MHz and use the `--placer heap` option if `sa`
fails to find a solution.

```bash
nextpnr-ice40 --up5k --package sg48 --freq 90 --placer heap ...
```
