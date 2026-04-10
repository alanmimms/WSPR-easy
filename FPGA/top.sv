module Top (
	    // 40 MHz GNSS-Disciplined TCXO Input
	    input  logic clk40,
	    input logic gnssPPS,

	    input logic fpgaNRESET,

	    // ESP32 SPI Interface
	    input  logic fpgaSCLK_pin,
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

  logic fpgaSCLK;
  SB_GB sclkGbuf (
		  .USER_SIGNAL_TO_GLOBAL_BUFFER(fpgaSCLK_pin),
		  .GLOBAL_BUFFER_OUTPUT(fpgaSCLK)
		  );

  logic clk90_pre;
  logic clk90;
  logic pllLocked;

  // TARGET: 90 MHz for 180 MHz DDR RF Output.
  // F_VCO = 40 * (17 + 1) = 720 MHz
  // F_OUT = 720 / 2^3 = 90 MHz
  SB_PLL40_PAD #(
		 .FEEDBACK_PATH("SIMPLE"),
		 .DIVR(4'b0000),       
		 .DIVF(7'b0010001),   
		 .DIVQ(3'b011),        
		 .FILTER_RANGE(3'b011) 
		 ) sysPll (
			   .PACKAGEPIN(clk40), 
			   .PLLOUTCORE(clk90_pre),
			   .LOCK(pllLocked),
			   .RESETB(1'b1),
			   .BYPASS(1'b0)
			   );

  SB_GB clk90Gbuf (
		   .USER_SIGNAL_TO_GLOBAL_BUFFER(clk90_pre),
		   .GLOBAL_BUFFER_OUTPUT(clk90)
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

  // Use clk90 for frequency counter.
  FreqCounter freqCounter (
			   .reset(!fpgaNRESET),
			   .clk40(clk90),
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
			.clk90(clk90),
			.tuningWord(tuningWord),
			.pllLocked(pllLocked),
			.txEnable(txEnable),
			.ppsCount(ppsCount),
			.ppsGen(ppsGen)
			);

  WSPRExciter exciterCore (
			   .reset(!fpgaNRESET),
			   .clk90(clk90),
			   .tuningWord(tuningWord),
			   .txEnable(txEnable & pllLocked), 
			   .rfPushBase(rfPushBase),
			   .rfPushPeak(rfPushPeak),
			   .rfPullBase(rfPullBase),
			   .rfPullPeak(rfPullPeak)
			   );

endmodule // Top
