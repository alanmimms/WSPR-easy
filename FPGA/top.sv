module Top (
	    // 40 MHz GNSS-Disciplined TCXO Input
	    input  logic clk40,
	    input logic gnssPPS,

	    input logic fpgaNRESET,

	    // ESP32 SPI Interface
	    input  logic fpgaSCLK,
	    input  logic fpgaMOSI,
	    output logic fpgaMISO,
	    input  logic fpgaNCS,

	    // TX Exciter Pins
	    output logic rfPushBase,
	    output logic rfPushPeak,
	    output logic rfPullBase,
	    output logic rfPullPeak,

	    // Transmit Enable (Active Low) routing to 74ACT buffers
	    output logic driverNEN 
	    );

  logic clk45;
  logic pllLocked;

  // Use SB_PLL40_PAD for dedicated clk40 path.
  // TARGET: 45 MHz for better timing on iCE40UP5K.
  SB_PLL40_PAD #(
		 .FEEDBACK_PATH("SIMPLE"),
		 .DIVR(4'b0000),       // Ref Div: 40 MHz / (0 + 1) = 40 MHz PFD
		 .DIVF(8'b00001000),   // FB Div: 40 MHz * (8 + 1) = 360 MHz VCO
		 .DIVQ(3'b011),        // VCO Div: 360 MHz / (2^3) = 45 MHz
		 .FILTER_RANGE(3'b100) // Filter range for 40 MHz PFD
		 ) sysPll (
			   .PACKAGEPIN(clk40), 
			   .PLLOUTCORE(clk45),
			   .LOCK(pllLocked),
			   .RESETB(1'b1),
			   .BYPASS(1'b0)
			   );

  logic [5:0] ppsGen;
  logic [25:0] ppsCount;
  logic [31:0] tuningWord;
  logic txEnable;

  // =========================================================================
  // Transmit Enable / Driver Control (Active Low)
  // =========================================================================
  logic driverEnableState;
  
  // Calculate internal state: Enabled only when txEnable is high AND PLL is locked
  // We use the async signals here, they are buffered by SB_IO in the end.
  assign driverEnableState = ~(txEnable & pllLocked);

  // Explicit I/O instantiation for Pin 25
  SB_IO #(
	  .PIN_TYPE(6'b010001) 
	  ) ioDriverNEN (
			 .PACKAGE_PIN(driverNEN), 
			 .D_OUT_0(driverEnableState)
			 );

  // Use clk45 for frequency counter.
  FreqCounter freqCounter (
			   .reset(!fpgaNRESET),
			   .clk40(clk45),
			   .fpgaNCS(fpgaNCS),
			   .ppsCount(ppsCount),
			   .ppsGen(ppsGen),
			   .samplePPS(gnssPPS)
			   );


  SPIRegisters spiCore (
			.reset(!fpgaNRESET),
			.fpgaSCLK(fpgaSCLK),
			.fpgaMOSI(fpgaMOSI),
			.fpgaMISO(fpgaMISO),
			.fpgaNCS(fpgaNCS),
			.clk90(clk45),
			.tuningWord(tuningWord),
			.pllLocked(pllLocked),
			.txEnable(txEnable),
			.ppsCount(ppsCount),
			.ppsGen(ppsGen)
			);

  WSPRExciter exciterCore (
			   .reset(!fpgaNRESET),
			   .clk90(clk45),
			   .tuningWord(tuningWord),
			   .txEnable(txEnable & pllLocked), 
			   .rfPushBase(rfPushBase),
			   .rfPushPeak(rfPushPeak),
			   .rfPullBase(rfPullBase),
			   .rfPullPeak(rfPullPeak)
			   );

endmodule // Top
