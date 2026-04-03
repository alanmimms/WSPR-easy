### 1. The FPGA Selection: Lattice iCE40UP5K

The best candidate that balances cost, speed, and capability is the
**Lattice iCE40UP5K**.

- **Part Number:** **iCE40UP5K-SG48**
    
- **Package:** **QFN-48** (0.5mm pitch). This is a standard "Quad Flat
  No-leads" package. It has pads on the bottom edge, but no balls. You
  can solder this with a hot air gun or a reflow hotplate easily.
    
- **Logic Cells:** 5280 LUTs (Look-Up Tables).
    
    - _Is this enough?_ Yes, it's more than enough. A 32-bit NCO +
      6-step sequencer + SPI interface will consume maybe 150-200
      LUTs. The UP5K also provides 1Mb of SPRAM and 8 DSP blocks which
      can be used for future signal processing.
        
- **Speed:** The internal fabric runs easily at >200 MHz. The I/O pins
  can toggle at these speeds without issue.
    
- **Cost:** ~$6.00 - $8.00 USD.
    
- **Availability:** Excellent.
    

**Recommendation:** Go with **iCE40UP5K-SG48**. It provides massive
headroom for future features.

---

### 2. The "1-2-1" Waveform Logic Explained

You are absolutely correct that the "1-2-1" name is a shorthand. The
actual sequence is a **6-step cycle** representing $0^\circ$ to
$360^\circ$.

The goal is to synthesize a stepped sine wave with values: 0, +1, +2,
+1, 0, -1, -2, -1.

However, since we are driving a transformer with Push-Pull logic, we
don't output "negative" voltage directly. We swap the active winding.

#### The 6-Step Sequence (Clocked at 6F)

The sequence repeats every 6 clock ticks.

|**Tick**|**Degrees**|**Desired Voltage**|**Active Step**|**Push Base**|**Push Peak**|**Pull Base**|**Pull Peak**|
|---|---|---|---|---|---|---|---|
|**0**|$0^\circ-60^\circ$|**+1**|Push Base|**ON**|OFF|OFF|OFF|
|**1**|$60^\circ-120^\circ$|**+2**|Push Peak|**ON**|**ON**|OFF|OFF|
|**2**|$120^\circ-180^\circ$|**+1**|Push Base|**ON**|OFF|OFF|OFF|
|**3**|$180^\circ-240^\circ$|**-1**|Pull Base|OFF|OFF|**ON**|OFF|
|**4**|$240^\circ-300^\circ$|**-2**|Pull Peak|OFF|OFF|**ON**|**ON**|
|**5**|$300^\circ-360^\circ$|**-1**|Pull Base|OFF|OFF|**ON**|OFF|

Wait, correction on the logic:

In the transformer design we settled on (Tapped Primary), you switch
BETWEEN the End (Base) and the Tap (Peak).

If you turn both on (Shorting the Tap to the End), you effectively
short-circuit the winding turns between the tap and the end. That
would be bad.

**Corrected Logic for Tapped Transformer:**

- **To get +1:** Drive **Push Base** (4 Turns).
    
- **To get +2:** Drive **Push Peak** (2 Turns).
    
- **To get +1:** Drive **Push Base** (4 Turns).
    

So the table is actually:

|**Tick**|**Phase**|**Active Pin**|
|---|---|---|
|**0**|+1|**Push Base**|
|**1**|+2|**Push Peak**|
|**2**|+1|**Push Base**|
|**3**|-1|**Pull Base**|
|**4**|-2|**Pull Peak**|
|**5**|-1|**Pull Base**|

It is a simple **One-Hot State Machine** for the positive side, then
repeated for the negative side.

---

### 3. Can the FPGA clock fast enough for 30 MHz?

Requirement:

For 10m Band (30 MHz), the update rate is $6 \times 30 \text{ MHz} =
\mathbf{180 \text{ MHz}}$.

Can iCE40 do it?

Yes, easily with the 40 MHz TCXO and internal PLL.

- The iCE40LP/HX series logic fabric is typically rated for ~200-250
  MHz max toggle rate for simple logic.
    
- A 32-bit adder (for the NCO) carries a long carry-chain delay. A
  32-bit add might only close timing at ~80-100 MHz in this specific
  low-power fabric.
    

The Workaround: Pipelining or PLL Phase Shift

To hit 180 MHz+ output with a slower NCO, we use:

1. **DDR Output:** The iCE40 I/O cells support **DDR (Double Data
   Rate)**.
    
    - We clock the logic at **100 MHz**.
        
    - We output data on both the Rising and Falling edge.
        
    - Result: Effective update rate of **200 Msps**.
        
2. **PLL Multiplier:** The iCE40 has an internal PLL. You feed it 40
   MHz (on Pin 35) and multiply up to 100 MHz. The simple
   counter/lookup logic runs at 100 MHz. The NCO accumulator also runs
   at 100 MHz.
    

Verdict:

Yes, the iCE40UP5K can generate the 10m band signal (200 Msps) cleanly
using the PLL to generate a 100 MHz clock for the DDR I/O, while
running the main NCO logic at the same 100 MHz rate.

### 4. FPGA Implementation Plan

1. **Clock:** 40 MHz TCXO (Pin 35) $\rightarrow$ FPGA PLL
   $\rightarrow$ 100 MHz (System Clock).
    
2. **NCO:** 32-bit Accumulator running at 100 MHz.
    
    - `acc <= acc + tuning_word`
        
3. **Sequencer:**
    - The sequencer uses a 6-step cycle to generate the "1-2-1"
      waveform.
    - Since it's DDR, each 100 MHz clock cycle produces two 200 Msps
      steps.

This is definitely the superior technical path for signal purity.
