`timescale 1ns / 1ps
/**
 * file: top.sv
 * module: Top
 * description: WSPR-ease Main FPGA Design. 
 * Simplified Output Drive (Non-DDR) for physical verification.
 */

module Top (
	    input  logic fpgaClk,    // SPI SCK
	    input  logic fpgaMOSI,   // SPI MOSI
	    input  logic fpgaNCS,    // SPI nCS
	    output logic fpgaMISO,   // SPI MISO
	    input  logic gnssPPS,    // GPS PPS Input

	    input  logic clk40MHz,   // 40 MHz TCXO (Drives PLL on pin 35)

	    // RF Power Amplifier Drive
	    output logic rfPushBase,
	    output logic rfPushPeak,
	    output logic rfPullBase,
	    output logic rfPullPeak
	    );

  // -------------------------------------------------------------------------
  // 1. Clock Generation (40 MHz)
  // -------------------------------------------------------------------------
  logic clk;
  logic pllLocked;

  SB_PLL40_PAD #(
		 .FEEDBACK_PATH("SIMPLE"),
		 .DIVR(4'b0000), .DIVF(7'b0000111), .DIVQ(3'b011),
		 .FILTER_RANGE(3'b001)
		 ) pllInst (
			    .PACKAGEPIN(clk40MHz),
			    .PLLOUTCORE(clk),
			    .LOCK(pllLocked),
			    .RESETB(1'b1),
			    .BYPASS(1'b0)
			    );

  // -------------------------------------------------------------------------
  // 2. PPS Measurement
  // -------------------------------------------------------------------------
  logic [31:0] ppsCount = 0;
  logic [31:0] ppsFallVal = 0; 
  logic [31:0] ppsRiseVal = 0;
  logic [31:0] ppsEdges = 0;
  
  logic [2:0] ppsSync = 0;
  always_ff @(posedge clk) ppsSync <= {ppsSync[1:0], gnssPPS};

  wire ppsF = (!ppsSync[1] && ppsSync[2]);
  wire ppsR = (ppsSync[1] && !ppsSync[2]);

  always_ff @(posedge clk) begin
    ppsCount <= ppsCount + 1'b1;
    if (ppsF) begin ppsFallVal <= ppsCount; ppsEdges <= ppsEdges + 1'b1; end
    if (ppsR) begin ppsRiseVal <= ppsCount; ppsEdges <= ppsEdges + 1'b1; end
  end

  // -------------------------------------------------------------------------
  // 3. SPI Slave
  // -------------------------------------------------------------------------
  logic [39:0] spiShift;
  logic [5:0]  spiBits;
  logic [2:0]  sclkS, mosiS, ncsS;
  
  always_ff @(posedge clk) begin
    sclkS <= {sclkS[1:0], fpgaClk};
    mosiS <= {mosiS[1:0], fpgaMOSI};
    ncsS  <= {ncsS[1:0], fpgaNCS};
  end
  
  wire sclkR = (sclkS[1] && !sclkS[2]);
  wire sclkF = (!sclkS[1] && sclkS[2]);
  wire ncsA  = !ncsS[1];

  logic [31:0] regControl = 0;      
  logic [31:0] regTuning = 0;       
  logic [31:0] regSymbol = 0;       
  logic [31:0] regPwrThresh = 32'hFFFFFFFF;    
  logic [31:0] regToneSpacing = 0;  

  logic [31:0] spiRdData;
  logic [6:0]  spiAddr;

  always_ff @(posedge clk) begin
    if (!ncsA) begin
      spiBits <= 0;
      if (ncsS[1] && !ncsS[2]) begin // CS Rising
        if (spiBits == 6'd40 && spiShift[39]) begin
          case (spiShift[38:32])
            7'h00: regControl     <= spiShift[31:0];
            7'h01: regTuning      <= spiShift[31:0];
            7'h02: regSymbol      <= spiShift[31:0];
            7'h04: regPwrThresh   <= spiShift[31:0];
            7'h05: regToneSpacing <= spiShift[31:0];
          endcase
        end
      end
    end else if (sclkR) begin
      spiShift <= {spiShift[38:0], mosiS[1]};
      spiBits <= spiBits + 1'b1;
      if (spiBits == 6'd7) spiAddr <= {spiShift[6:0], mosiS[1]};
    end
  end

  // Forward declarations for status
  logic [2:0] p4StR;
  logic [31:0] p1Inc;
  logic [25:0] hbCounter = 0;
  always_ff @(posedge clk) hbCounter <= hbCounter + 1;

  always_ff @(posedge clk) begin
    case (spiAddr)
      // Bit 0: TX En, 1: PLL Lock, 2: PPS, 3: Heartbeat, 6:4: Step Index
      7'h00: spiRdData <= {25'd0, p4StR, hbCounter[25], ppsSync[1], pllLocked, regControl[0]}; 
      7'h01: spiRdData <= regTuning;
      7'h02: spiRdData <= regSymbol;
      7'h03: spiRdData <= ppsFallVal;
      7'h04: spiRdData <= regPwrThresh;
      7'h05: spiRdData <= regToneSpacing;
      7'h06: spiRdData <= ppsCount;
      7'h07: spiRdData <= ppsEdges;
      7'h08: spiRdData <= p1Inc;
      default: spiRdData <= 32'hDEADBEEF;
    endcase
  end

  logic misoOut;
  always_ff @(posedge clk) begin
    if (sclkF) begin
      if (spiBits >= 6'd8 && spiBits < 6'd40)
        misoOut <= spiRdData[5'd31 - (spiBits[4:0] - 5'd8)];
      else misoOut <= 0;
    end
  end
  assign fpgaMISO = ncsA ? misoOut : 1'bz;

  // -------------------------------------------------------------------------
  // 4. RF Synthesis (Simple 40 Msps for Debug)
  // -------------------------------------------------------------------------
  
  always_ff @(posedge clk) p1Inc <= regTuning + (regSymbol[1:0] * regToneSpacing);

  logic [31:0] p2Acc;
  always_ff @(posedge clk) begin
    if (!pllLocked || !regControl[0]) p2Acc <= 0;
    else p2Acc <= p2Acc + p1Inc;
  end

  // Pipelined Phase-to-Step mapping (6 steps per RF cycle)
  logic [34:0] p3Phase6x;
  always_ff @(posedge clk) p3Phase6x <= 35'd6 * p2Acc;
  always_ff @(posedge clk) p4StR <= p3Phase6x[34:32];

  logic p5EnR;
  wire txActive = regControl[0] && pllLocked;
  always_ff @(posedge clk) p5EnR <= txActive;

  logic r1, r2, r3, r4;
  Sequencer121 sR(.stepIndex(p4StR), .pushBase(r1), .pushPeak(r2), .pullBase(r3), .pullPeak(r4));

  logic gr1, gr2, gr3, gr4;
  always_ff @(posedge clk) begin
    gr1 <= r1 & p5EnR; gr2 <= r2 & p5EnR; 
    gr3 <= r3 & p5EnR; gr4 <= r4 & p5EnR;
  end

  // PIN_TYPE 010100 = Registered Output
  SB_IO #(.PIN_TYPE(6'b010100)) io0(.PACKAGE_PIN(rfPushBase), .D_OUT_0(gr1), .OUTPUT_CLK(clk), .CLOCK_ENABLE(1'b1), .OUTPUT_ENABLE(1'b1));
  SB_IO #(.PIN_TYPE(6'b010100)) io1(.PACKAGE_PIN(rfPushPeak), .D_OUT_0(gr2), .OUTPUT_CLK(clk), .CLOCK_ENABLE(1'b1), .OUTPUT_ENABLE(1'b1));
  SB_IO #(.PIN_TYPE(6'b010100)) io2(.PACKAGE_PIN(rfPullBase), .D_OUT_0(gr3), .OUTPUT_CLK(clk), .CLOCK_ENABLE(1'b1), .OUTPUT_ENABLE(1'b1));
  SB_IO #(.PIN_TYPE(6'b010100)) io3(.PACKAGE_PIN(rfPullPeak), .D_OUT_0(gr4), .OUTPUT_CLK(clk), .CLOCK_ENABLE(1'b1), .OUTPUT_ENABLE(1'b1));

endmodule
