`timescale 1ns / 1ps
/**
 * file: top.sv
 * module: Top
 * description: WSPR-ease Main FPGA Design (SystemVerilog).
 * Features:
 * - 200 Msps NCO with 1-2-1 Sequencer
 * - PPS Period Counter (TCXO Calibration)
 * - Bidirectional SPI
 */

module Top (
	    input  logic fpgaClk,    // SPI SCK
	    input  logic fpgaMOSI,   // SPI MOSI
	    input  logic fpgaNCS,    // SPI nCS
	    output logic fpgaMISO,   // SPI MISO
	    input  logic gnssPPS,    // GPS PPS Input

	    input  logic clk40MHz,   // 40 MHz TCXO (Drives PLL on pin 35)

	    // RF Power Amplifier Drive (DDR Outputs)
	    output logic rfPushBase,
	    output logic rfPushPeak,
	    output logic rfPullBase,
	    output logic rfPullPeak
	    );

  // -------------------------------------------------------------------------
  // 1. Clock Generation (100 MHz from 40 MHz Pad)
  // -------------------------------------------------------------------------
  logic clk100MHz;
  logic pllLocked;

  // 40 MHz -> 100 MHz
  // F_out = F_ref * (DIVF+1) / (2^DIVQ * (DIVR+1))
  // 100 = 40 * (4+1) / (2^1 * (0+1)) = 40 * 5 / 2 = 100
  SB_PLL40_PAD #(
		 .FEEDBACK_PATH("SIMPLE"),
		 .DIVR(4'b0000),      // DIVR = 0
		 .DIVF(7'b0000100),   // DIVF = 4
		 .DIVQ(3'b001),       // DIVQ = 1
		 .FILTER_RANGE(3'b001)
		 ) pllInst (
			    .PACKAGEPIN(clk40MHz),
			    .PLLOUTCORE(clk100MHz),
			    .LOCK(pllLocked),
			    .RESETB(1'b1),
			    .BYPASS(1'b0)
			    );

  // -------------------------------------------------------------------------
  // 2. PPS Measurement (TCXO Calibration) - Now at 100 MHz
  // -------------------------------------------------------------------------
  logic [31:0] ppsLiveCounter;
  logic [31:0] ppsCapturedPeriod; 
  
  logic [2:0] ppsSync;
  logic ppsRisingEdge;

  always_ff @(posedge clk100MHz) begin
    ppsSync <= {ppsSync[1:0], gnssPPS};
  end

  assign ppsRisingEdge = (ppsSync[0] && !ppsSync[1]);

  always_ff @(posedge clk100MHz) begin
    if (ppsRisingEdge) begin
      ppsCapturedPeriod <= ppsLiveCounter;
      ppsLiveCounter <= 32'd0;
    end else begin
      ppsLiveCounter <= ppsLiveCounter + 1'b1;
    end
  end

  // -------------------------------------------------------------------------
  // 3. Synchronous SPI Slave (sampled by clk100MHz)
  // -------------------------------------------------------------------------
  logic [31:0] spiShiftReg;
  logic [31:0] tuningWordShadow = 32'd0;
  logic [5:0]  spiBitCount;
  
  logic [2:0] sclkSync, mosiSync, ncsSync;
  
  always_ff @(posedge clk100MHz) begin
    sclkSync <= {sclkSync[1:0], fpgaClk};
    mosiSync <= {mosiSync[1:0], fpgaMOSI};
    ncsSync  <= {ncsSync[1:0], fpgaNCS};
  end
  
  wire sclkRise = (sclkSync[1] && !sclkSync[2]);
  wire sclkFall = (!sclkSync[1] && sclkSync[2]);
  wire ncsActive = !ncsSync[1];
  wire ncsFallingEdge = (!ncsSync[1] && ncsSync[2]);

  logic spiMisoOut;

  always_ff @(posedge clk100MHz) begin
    if (!ncsActive) begin
      spiBitCount <= '0;
      spiMisoOut <= 1'b0;
    end else begin
      if (ncsFallingEdge) begin
        spiShiftReg <= ppsCapturedPeriod;
        spiBitCount <= '0;
        spiMisoOut <= ppsCapturedPeriod[31];
      end else if (sclkRise) begin
        spiShiftReg <= {spiShiftReg[30:0], mosiSync[1]};
        spiBitCount <= spiBitCount + 1'b1;
      end else if (sclkFall) begin
        // Shift out the next bit on falling edge
        spiMisoOut <= spiShiftReg[31];
      end
    end
  end
  
  // MISO Output
  assign fpgaMISO = ncsActive ? spiMisoOut : 1'bz;

  // Tuning Word Latch
  always_ff @(posedge clk100MHz) begin
    if (!ncsActive && ncsSync[2]) begin // NCS rising edge
      if (spiBitCount == 6'd32) begin
        tuningWordShadow <= spiShiftReg;
      end
    end
  end

  // -------------------------------------------------------------------------
  // 4. 200 Msps NCO & Sequencer (100 MHz DDR)
  // -------------------------------------------------------------------------
  logic [31:0] accumulator;
  logic [2:0]  stepIndex;
  
  logic [2:0] idxRise;
  logic [2:0] idxFall;

  logic [31:0] accPlus1;
  logic [31:0] accPlus2;
  logic        overflow1;
  logic        overflow2;

  assign accPlus1 = accumulator + tuningWordShadow;
  assign accPlus2 = accumulator + (tuningWordShadow << 1); 

  assign overflow1 = (accPlus1 < accumulator);
  assign overflow2 = (accPlus2 < accPlus1);

  always_ff @(posedge clk100MHz) begin
    if (!pllLocked) begin
      accumulator <= '0;
      stepIndex   <= '0;
    end else begin
      accumulator <= accPlus2;
      // Modulo 6 arithmetic
      if (overflow1 && overflow2) begin
        stepIndex <= (stepIndex >= 3'd4) ? (stepIndex - 3'd4) : (stepIndex + 3'd2); 
      end else if (overflow1 || overflow2) begin
        stepIndex <= (stepIndex == 3'd5) ? 3'd0 : (stepIndex + 3'd1);
      end
    end
  end

  // Combinatorial Logic: Determine state for Rise and Fall outputs
  always_comb begin
    idxRise = stepIndex;
    if (overflow1) begin
      idxFall = (stepIndex == 3'd5) ? 3'd0 : (stepIndex + 3'd1);
    end else begin
      idxFall = stepIndex;
    end
  end

  // Sequencer Signals
  logic pushBaseRise, pushPeakRise, pullBaseRise, pullPeakRise;
  logic pushBaseFall, pushPeakFall, pullBaseFall, pullPeakFall;
  
  Sequencer121 seqRise (
			.stepIndex(idxRise),
			.pushBase (pushBaseRise), 
			.pushPeak (pushPeakRise), 
			.pullBase (pullBaseRise), 
			.pullPeak (pullPeakRise)
			);

  Sequencer121 seqFall (
			.stepIndex(idxFall),
			.pushBase (pushBaseFall), 
			.pushPeak (pushPeakFall), 
			.pullBase (pullBaseFall), 
			.pullPeak (pullPeakFall)
			);

  // -------------------------------------------------------------------------
  // 5. DDR Output Primitives
  // -------------------------------------------------------------------------
  
  SB_IO #(
          .PIN_TYPE(6'b010000), // PIN_OUTPUT_DDR
          .IO_STANDARD("SB_LVCMOS")
	  ) ioPushBase (
			.PACKAGE_PIN(rfPushBase),
			.D_OUT_0(pushBaseRise),
			.D_OUT_1(pushBaseFall),
			.OUTPUT_CLK(clk100MHz),
			.CLOCK_ENABLE(1'b1),
			.INPUT_CLK(1'b0),
			.OUTPUT_ENABLE(1'b1),
			.D_IN_0(), .D_IN_1()
			);

  SB_IO #(
          .PIN_TYPE(6'b010000),
          .IO_STANDARD("SB_LVCMOS")
	  ) ioPushPeak (
			.PACKAGE_PIN(rfPushPeak),
			.D_OUT_0(pushPeakRise),
			.D_OUT_1(pushPeakFall),
			.OUTPUT_CLK(clk100MHz),
			.CLOCK_ENABLE(1'b1),
			.INPUT_CLK(1'b0),
			.OUTPUT_ENABLE(1'b1),
			.D_IN_0(), .D_IN_1()
			);

  SB_IO #(
          .PIN_TYPE(6'b010000),
          .IO_STANDARD("SB_LVCMOS")
	  ) ioPullBase (
			.PACKAGE_PIN(rfPullBase),
			.D_OUT_0(pullBaseRise),
			.D_OUT_1(pullBaseFall),
			.OUTPUT_CLK(clk100MHz),
			.CLOCK_ENABLE(1'b1),
			.INPUT_CLK(1'b0),
			.OUTPUT_ENABLE(1'b1),
			.D_IN_0(), .D_IN_1()
			);

  SB_IO #(
          .PIN_TYPE(6'b010000),
          .IO_STANDARD("SB_LVCMOS")
	  ) ioPullPeak (
			.PACKAGE_PIN(rfPullPeak),
			.D_OUT_0(pullPeakRise),
			.D_OUT_1(pullPeakFall),
        .OUTPUT_CLK(clk100MHz),
        .CLOCK_ENABLE(1'b1),
        .INPUT_CLK(1'b0),
        .OUTPUT_ENABLE(1'b1),
        .D_IN_0(), .D_IN_1()
    );

endmodule // Top
