/*
 * Module: fpgaTop
 * Function: WSPR-ease Multiband Exciter
 * Input: 25MHz TCXO, SPI Tuning Word
 * Output: 4-Phase MOSFET Drive (Push/Pull Base/Peak)
 */

module fpgaTop (
    input  wire clk25,       // 25 MHz TCXO
    input  wire nReset,      // Active Low Reset (from ESP32)
    
    // SPI Interface (from ESP32)
    input  wire spiClk,
    input  wire spiMosi,
    input  wire nSpiCs,
    
    // RF Power Amplifier Gates
    output wire paPushBase,  // Primary A (Ends)
    output wire paPushPeak,  // Primary A (Tap)
    output wire paPullBase,  // Primary B (Ends)
    output wire paPullPeak   // Primary B (Tap)
);

    // -------------------------------------------------------------------------
    // 1. Clock Generation (PLL)
    // Target: 180 MHz from 25 MHz Reference
    // Note: Use 'icepll -i 25 -o 180' to generate specific config bits if needed
    // -------------------------------------------------------------------------
    wire sysClk;
    wire pllLocked;

    SB_PLL40_CORE #(
        .FEEDBACK_PATH("SIMPLE"),
        .PLLOUT_SELECT("GENCLK"),
        .DIVR(4'b0000),      // DIVR = 0
        .DIVF(7'b0111000),   // DIVF = 56
        .DIVQ(3'b011),       // DIVQ = 3 
        .FILTER_RANGE(3'b001)
    ) pllInst (
        .REFERENCECLK(clk25),
        .PLLOUTCORE(sysClk),
        .RESETB(nReset),
        .BYPASS(1'b0),
        .LOCK(pllLocked)
    );

    // -------------------------------------------------------------------------
    // 2. SPI Slave (Configuration Receiver)
    // Receives 32-bit Tuning Word (Big Endian)
    // -------------------------------------------------------------------------
    reg [31:0] shiftReg;
    reg [4:0]  bitCount;
    reg [31:0] tuningWordShadow;
    reg        newWordReady;

    always @(posedge spiClk or posedge nSpiCs) begin
        if (nSpiCs) begin
            bitCount <= 0;
            newWordReady <= 0;
        end else begin
            shiftReg <= {shiftReg[30:0], spiMosi};
            bitCount <= bitCount + 1;
            
            // Latch data on 32nd bit
            if (bitCount == 31) begin
                tuningWordShadow <= {shiftReg[30:0], spiMosi};
                newWordReady <= 1;
            end else begin
                newWordReady <= 0;
            end
        end
    end

    // -------------------------------------------------------------------------
    // 3. Domain Crossing (SPI -> sysClk)
    // -------------------------------------------------------------------------
    reg [31:0] activeTuningWord;
    reg [2:0]  syncPipe; // For edge detection of 'newWordReady'

    always @(posedge sysClk) begin
        if (!nReset || !pllLocked) begin
            activeTuningWord <= 0;
            syncPipe <= 0;
        end else begin
            syncPipe <= {syncPipe[1:0], newWordReady};
            
            // On rising edge of the synced 'ready' signal, update the NCO
            if (syncPipe[1] && !syncPipe[2]) begin
                activeTuningWord <= tuningWordShadow;
            end
        end
    end

    // -------------------------------------------------------------------------
    // 4. NCO & Sequencer (The "1-2-1" Logic)
    // Rate: Run NCO at 6x Carrier Frequency
    // -------------------------------------------------------------------------
    reg [31:0] phaseAcc;
    reg [2:0]  stepState; // 0..5
    reg        lastMsb;

    always @(posedge sysClk) begin
        if (!nReset) begin
            phaseAcc <= 0;
            stepState <= 0;
            lastMsb <= 0;
        end else begin
            // NCO Accumulate
            phaseAcc <= phaseAcc + activeTuningWord;

            // Tick Generation: Detect MSB Rising Edge
            // This creates a pulse stream at exactly the frequency defined by tuningWord
            if (phaseAcc[31] && !lastMsb) begin
                // Advance 6-step sequence: 0->1->2->3->4->5->0
                if (stepState == 5)
                    stepState <= 0;
                else
                    stepState <= stepState + 1;
            end
            
            lastMsb <= phaseAcc[31];
        end
    end

    // -------------------------------------------------------------------------
    // 5. Output Decoding (One-Hot / State Map)
    // Sequence: 
    // 0: Push Base (+1)
    // 1: Push Peak (+2)
    // 2: Push Base (+1)
    // 3: Pull Base (-1)
    // 4: Pull Peak (-2)
    // 5: Pull Base (-1)
    // -------------------------------------------------------------------------
    
    // Default OFF
    reg drvPushBase, drvPushPeak, drvPullBase, drvPullPeak;

    always @(*) begin
        // Safety defaults
        drvPushBase = 0; drvPushPeak = 0; 
        drvPullBase = 0; drvPullPeak = 0;

        case (stepState)
            3'd0: drvPushBase = 1;      // +1
            3'd1: drvPushPeak = 1;      // +2
            3'd2: drvPushBase = 1;      // +1
            3'd3: drvPullBase = 1;      // -1
            3'd4: drvPullPeak = 1;      // -2
            3'd5: drvPullBase = 1;      // -1
            default: ; // All Off
        endcase
    end

    // Assign to physical outputs
    // Note: We only drive if PLL is locked to prevent glitches
    assign paPushBase = pllLocked ? drvPushBase : 0;
    assign paPushPeak = pllLocked ? drvPushPeak : 0;
    assign paPullBase = pllLocked ? drvPullBase : 0;
    assign paPullPeak = pllLocked ? drvPullPeak : 0;

endmodule // fpgaTop
