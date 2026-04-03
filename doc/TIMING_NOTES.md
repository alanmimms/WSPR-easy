# WSPR-ease Timing Analysis & 180MHz Strategy

## 1. The Core Problem: The 5.55ns Wall
The iCE40 UP5K is a 40nm process FPGA. While it is efficient, its fabric performance is the primary constraint for a 180MHz design.
* **Period:** 5.55ns.
* **Overhead:** Clock-to-Q (~0.5ns) + Setup Time (~0.2ns) + Clock Skew (~0.3ns) = **~1.0ns fixed overhead**.
* **Remaining Budget:** **~4.5ns** for all logic and routing.

## 2. Interpreting the `nextpnr.log`
When the build fails, the log provides a `Critical path report`. Here is what the numbers mean:
* **Logic Delay:** Time spent inside the LUTs. If this is > 1.5ns, you have too many logic levels (more than 2 LUTs).
* **Routing Delay:** Time spent in the wires. If this is > 3ns, the design is too congested or a signal has too much fanout.
* **Global Promotion:** Look for `promoting ... [reset/cen]`. If a signal like `spiAddr` is promoted to a global buffer, it adds a ~1.6ns "insertion delay". This is fatal at 180MHz if there is combinatorial logic before the buffer.

## 3. Current Bottlenecks (as of April 2026)
1. **SPI MUXing:** Multiplexing 32-bit registers based on a 7-bit address creates a deep LUT tree.
2. **Control Fanout:** Signals like `txEnable` or `ncoReset` drive many registers across the chip. The routing delay to reach all those sinks exceeds the 5.5ns budget.
3. **Carry Chains:** Even an 8-bit ripple carry is marginal at 180MHz if the input data or control gating isn't perfectly local.

## 4. The "Path to Victory" Implementation
To hit 180MHz, the design must adopt these three pillars:

### A. BRAM Shadowing
Instead of using FFs and LUT MUXes for registers, use the **SB_RAM40_4K** block.
* **Why:** BRAM has dedicated high-speed read/write ports and internal registers. It handles the 16-to-1 MUXing in hardware logic that is faster than the LUT fabric.
* **Action:** Move `regTuning`, `regPwrThresh`, etc., into BRAM.

### B. Manual Register Trees
Do not let any control signal drive more than **8-12 sinks**.
* **Action:** If `ncoReset` needs to drive 32 FFs, create 4 copies of `ncoReset` (`nr_tree[3:0]`), each driving a 8-bit chunk. This forces `nextpnr` to use fast local routing instead of slow global buffers.

### C. 4-bit Skewed Pipelining
Break all 32-bit arithmetic into 4-bit "nibbles".
* **Latency:** A 32-bit addition will take 8 clock cycles to complete. 
* **Timing:** Each stage is just **1 LUT + 1 Carry**, which is the absolute minimum possible delay.

## 5. Build Configuration
* **Makefile:** Always use `--opt-timing` and `--no-promote-globals`.
* **Clocking:** Manually instantiate `SB_GB` for the 180MHz clock to ensure it uses the highest-priority global network.
* **IO:** Use the `PIN_TYPE(6'b010101)` for registered SDR outputs to ensure the final output FF is inside the IOB, removing the last stage of routing delay.
