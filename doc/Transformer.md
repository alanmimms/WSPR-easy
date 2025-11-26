### **Component: T1 (Broadband RF Transformer)**

- **Core Part:** **BN-43-202** (Fair-Rite 2843000202)
    
    - _Type:_ Binocular (Multi-Aperture) Ferrite Core.
        
    - _Material:_ Mix 43 (Optimized for 1 MHz – 50 MHz).
        
    - _Size:_ Approx. 13.3mm $\times$ 14.4mm (0.5" square).
        
- **Wire:** **AWG 26** Enameled Magnet Wire.
    
    - _Note:_ The holes in this core are large. You can easily fit 12 passes of AWG 26. AWG 24 will also fit if you are neat, but 26 is easier to work with.
        
    - _Color Coding:_ If possible, use two different colors of enamel (e.g., Red and Green) to separate the Primary and Secondary visually.
        

---

### **Winding Schedule**

This transformer has **3 separate windings**. All windings must be wound in the **same direction** to ensure the phasing is correct.

#### **1. Primary A ("Push" Side)**

- **Total Turns:** **4 Turns**.
    
- **Tap Point:** **At 2 Turns** (Center Tap).
    
- **Construction:**
    
    1. Start at **Pin 1**.
        
    2. Wind **2 Turns** through the binocular holes. _(One "Turn" = Through Left hole, back through Right hole)_.
        
    3. Pull the wire out and twist a loop. This loop is the **Tap (Pin 2)**.
        
    4. Continue winding **2 more Turns** in the same direction.
        
    5. End at **Pin 3**.
        

#### **2. Primary B ("Pull" Side)**

- **Total Turns:** **4 Turns**.
    
- **Tap Point:** **At 2 Turns** (Center Tap).
    
- **Construction:**
    
    - Same as Primary A, but using **Pins 4, 5, and 6**.
        
    - _Tip:_ Wind this directly on top of (or next to) Primary A. Tightly coupled wires are good here.
        

#### **3. Secondary (Output)**

- **Total Turns:** **4 Turns**.
    
- **Tap Point:** **None**.
    
- **Construction:**
    
    1. Start at **Pin 7** (Ground).
        
    2. Wind **4 Turns** through the holes.
        
    3. End at **Pin 9** (RF Output).
        

---

### **Physical Wiring Map (PCB vs. Core)**

Use this table to map the twisted wire ends to your PCB pads.

|**Winding**|**Wire Start**|**Loop / Tap**|**Wire End**|**Function**|
|---|---|---|---|---|
|**Pri A**|**Pin 1** (5V)|**Pin 2** (Peak Drive)|**Pin 3** (Base Drive)|**Push** Phase|
|**Pri B**|**Pin 4** (Peak Drive)|**Pin 5** (Base Drive)|**Pin 6** (5V)|**Pull** Phase|
|**Sec**|**Pin 7** (GND)|_None_|**Pin 9** (To Filter)|**RF Output**|

_Note on Pin 4/6 swap:_ For Primary B, since it is the "Pull" side, you technically want the current to flow in the _opposite_ magnetic direction relative to the core flux. However, in a binocular core, it is physically easier to wind them all identical and simply **swap the connections on the PCB**.

- **My Schematic Symbol Pinout (Previous Response)** already accounts for this. Just connect the wires exactly as numbered above.
    

### **Construction Tips**

1. **Tight Winds:** The wire must be tight against the ferrite. Loose loops create leakage inductance, which causes voltage spikes that can kill your MOSFETs.
    
2. **Scrape Enamel:** Ensure you thoroughly scrape or burn off the enamel insulation before soldering to the PCB pads. This is the #1 cause of "Zero Power" faults.
    
3. **Twisted Pairs?** No. Do not twist the wires together like a transmission line transformer. Just wind them neatly through the holes.