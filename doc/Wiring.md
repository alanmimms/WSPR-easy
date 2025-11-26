Here is the **Master Wiring Map** for your WSPR Beacon.

---

### **1. RP2040 Pinout (QFN-56)**

_This is the brain. Note: GPIO numbers match the Pin names, but check the physical pin number on the symbol._

|**Function**|**RP2040 Pin**|**Connects To**|**Notes**|
|---|---|---|---|
|**Power**|IOVDD (Multiple)|**+3.3V**|Decouple with 100nF caps|
|**Power**|DVDD (Internal)|**1.1V Output**|Connect to VREG_OUT (Pin 45)|
|**Power**|VREG_IN|**+3.3V**|Main input for internal LDO|
|**Power**|VREG_OUT|**DVDD Pins**|Requires 1uF Cap to GND|
|**Clock**|XIN (Pin 20)|**Crystal Pin 1**||
|**Clock**|XOUT (Pin 21)|**Crystal Pin 3**||
|**Flash**|QSPI_SS (Pin 56)|**Flash CS**|Also connect to **BOOTSEL Button**|
|**Flash**|QSPI_SD0 - SD3|**Flash IO0 - IO3**|Direct connection|
|**Flash**|QSPI_SCLK|**Flash CLK**|Direct connection|
|**USB**|USB_D- (Pin 46)|**USB Conn A7/B7**|Via 27$\Omega$ Resistor|
|**USB**|USB_D+ (Pin 47)|**USB Conn A6/B6**|Via 27$\Omega$ Resistor|
|**RF Drive**|GPIO 0 (Pin 2)|**74AC244 Input 1A**|Push Base|
|**RF Drive**|GPIO 1 (Pin 3)|**74AC244 Input 1B**|Push Peak|
|**RF Drive**|GPIO 2 (Pin 4)|**74AC244 Input 2A**|Pull Base|
|**RF Drive**|GPIO 3 (Pin 5)|**74AC244 Input 2B**|Pull Peak|
|**Switching**|GPIO 4 (Pin 6)|**SW1/SW2 Logic A**|Band Select Bit 0|
|**Switching**|GPIO 5 (Pin 7)|**SW3/SW4 Logic B**|Band Select Bit 1|
|**GNSS**|GPIO 12 (UART0 TX)|**GNSS RX**||
|**GNSS**|GPIO 13 (UART0 RX)|**GNSS TX**||
|**GNSS**|GPIO 14|**GNSS PPS**|Time Sync Input|
|**Control**|RUN (Pin 26)|**RESET Button**|Pull High (10k), Switch to GND|

---

### **2. RF Drive Stage (74AC244 & MOSFETs)**

_This is the "Super Buffer" configuration._

|**74AC244 Pin**|**Function**|**Connects To**|
|---|---|---|
|**1 (1OE)**|Enable Group 1|**GND** (Always On)|
|**19 (2OE)**|Enable Group 2|**GND** (Always On)|
|**2 + 4**|Input Phase A|**RP2040 GPIO 0**|
|**18 + 16**|Output Phase A|**Mosfet Bank A** (Push Base) via 10$\Omega$ Resistors|
|**6 + 8**|Input Phase B|**RP2040 GPIO 1**|
|**14 + 12**|Output Phase B|**Mosfet Bank B** (Push Peak) via 10$\Omega$ Resistors|
|**11 + 13**|Input Phase C|**RP2040 GPIO 2**|
|**9 + 7**|Output Phase C|**Mosfet Bank C** (Pull Base) via 10$\Omega$ Resistors|
|**15 + 17**|Input Phase D|**RP2040 GPIO 3**|
|**5 + 3**|Output Phase D|**Mosfet Bank D** (Pull Peak) via 10$\Omega$ Resistors|

---

### **3. Transformer Connections (BN-43-202)**

_Using the custom symbol pinout I generated earlier._

|**Trans. Pin**|**Connects To**|
|---|---|
|**1 (Pri A Start)**|**+5V (USB VBUS)**|
|**2 (Pri A Tap)**|**Mosfet Bank B** (Push Peak Drains)|
|**3 (Pri A End)**|**Mosfet Bank A** (Push Base Drains)|
|**4 (Pri B Start)**|**Mosfet Bank D** (Pull Peak Drains)|
|**5 (Pri B Tap)**|**Mosfet Bank C** (Pull Base Drains)|
|**6 (Pri B End)**|**+5V (USB VBUS)**|
|**7 (Sec Start)**|**GND**|
|**8 (Sec Tap)**|**Input of Filter Switch 1** (Antenna Out)|
|**9 (Sec End)**|**Floating** (Not used in tapped config)|

---

### **4. Filter Tree Logic (4x AS183 Switches)**

_Logic Table for your Firmware (GPIO 4 & 5)._

| **Band**      | **GPIO 4** | **GPIO 5** | **Path Active** | **Filter Cutoff** |
| ------------- | ---------- | ---------- | --------------- | ----------------- |
| **80m / 60m** | LOW        | LOW        | Sub-Low         | 6 MHz             |
| **40m - 20m** | HIGH       | LOW        | Mid Band        | 16 MHz            |
|               | LOW        | HIGH       | High Band       | 32 MHz            |
