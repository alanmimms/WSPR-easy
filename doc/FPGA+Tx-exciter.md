### 1. The FPGA Selection: Lattice iCE40LP384

The best candidate that balances cost, speed, and "hand-solderability" is the **Lattice iCE40LP384** or the slightly larger **iCE40HX1K**.

- **Part Number:** **iCE40LP384-SG32** (or `iCE40LP384-SG32TR`)
    
- **Package:** **QFN-32** (0.5mm pitch). This is a standard "Quad Flat No-leads" package. It has pads on the bottom edge, but no balls. You can solder this with a hot air gun or a reflow hotplate easily. You can even hand-solder it with a fine-tip iron if you extend the pads on the PCB footprint.
    
- **Logic Cells:** 384 LUTs (Look-Up Tables).
    
    - _Is this enough?_ Yes. A 32-bit NCO + 6-step sequencer + SPI interface will consume maybe 100-150 LUTs. You have plenty of room.
        
- **Speed:** The internal fabric runs easily at >200 MHz. The I/O pins can toggle at these speeds without issue.
    
- **Cost:** ~$3.50 - $4.50 USD (DigiKey/Mouser).
    
- **Availability:** Generally good. If the LP384 is out, the **iCE40LP1K-SWG16** is a tiny BGA (16-ball, 2.5mm), but since you want to avoid BGA, stick to the **SG32** or **VQ100** (TQFP) packages found in the HX series (e.g. **iCE40HX1K-VQ100**), though VQ100 is much physically larger.
    

**Recommendation:** Go with **iCE40LP384-SG32**. It is the smallest non-BGA option.

---

### 2. The "1-2-1" Waveform Logic Explained

You are absolutely correct that the "1-2-1" name is a shorthand. The actual sequence is a **6-step cycle** representing $0^\circ$ to $360^\circ$.

The goal is to synthesize a stepped sine wave with values: 0, +1, +2, +1, 0, -1, -2, -1.

However, since we are driving a transformer with Push-Pull logic, we don't output "negative" voltage directly. We swap the active winding.

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

In the transformer design we settled on (Tapped Primary), you switch BETWEEN the End (Base) and the Tap (Peak).

If you turn both on (Shorting the Tap to the End), you effectively short-circuit the winding turns between the tap and the end. That would be bad.

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

It is a simple **One-Hot State Machine** for the positive side, then repeated for the negative side.

---

### 3. Can the FPGA clock fast enough for 30 MHz?

Requirement:

For 10m Band (30 MHz), the update rate is $6 \times 30 \text{ MHz} = \mathbf{180 \text{ MHz}}$.

Can iCE40 do it?

Yes, but it's tight.

- The iCE40LP/HX series logic fabric is typically rated for ~200-250 MHz max toggle rate for simple logic.
    
- A 32-bit adder (for the NCO) carries a long carry-chain delay. A 32-bit add might only close timing at ~80-100 MHz in this specific low-power fabric.
    

The Workaround: Pipelining or PLL Phase Shift

To hit 180 MHz output with a slower NCO, you can cheat:

1. **DDR Output:** The iCE40 I/O cells support **DDR (Double Data Rate)**.
    
    - You clock the logic at **90 MHz**.
        
    - You output data on both the Rising and Falling edge.
        
    - Result: Effective update rate of **180 MHz**.
        
2. **PLL Multiplier:** The iCE40 has an internal PLL. You can feed it 25 MHz and multiply up to 180 MHz easily. The simple counter/lookup logic _will_ run at 180 MHz if the routing is short. The bottleneck is the NCO accumulator.
    
    - _Solution:_ Run the NCO accumulator at 90 MHz. Use the output to drive a "Doubler" logic block or just accept that the phase updates every 2 output steps (which is fine, it just means your frequency granularity is slightly coarser, but still sub-Hz).
        

Verdict:

Yes, the iCE40LP384 can generate the 10m band signal (180 Msps) cleanly, likely using the PLL to generate a fast clock for the I/O shift register, while running the main NCO logic at half-rate (90 MHz). This eliminates the "4x Limitation" entirely. You can have 6x harmonic cancellation on all bands.

### 4. FPGA Implementation Plan

1. **Clock:** 25 MHz Crystal $\rightarrow$ FPGA PLL $\rightarrow$ 180 MHz (System Clock).
    
2. **NCO:** 32-bit Accumulator running at 180 MHz.
    
    - `acc <= acc + tuning_word`
        
3. **Sequencer:**
    
    - Take top 3 bits of `acc`.
        
    - Map `0..5` to the gate pins.
        
    - Map `6..7` to "Wait/Hold" (or handle the overflow math to skip these states).
        
    - _Actually:_ It's better to make the NCO wrap at a value divisible by 6 (like a Modulo-6 counter) rather than a binary $2^{32}$ counter, OR just use the binary counter and map the top range 0-255 to 0-5.
        
    - _Better Math:_ Use the NCO to generate the _pulse_ that advances a separate 0-5 counter.
        
        - NCO generates a "Tick" pulse at $6 \times F_{carrier}$.
            
        - Counter `0->1->2->3->4->5->0` increments on every "Tick".
            
        - Combinatorial Logic decodes `0..5` to the MOSFET pins.
            

This is definitely the superior technical path for signal purity.