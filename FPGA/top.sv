`timescale 1ns / 100ps

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

	    // RF Output Phase Drivers
	    output logic rfPushBase,
	    output wire rfPushPeak,
	    output logic rfPullBase,
	    output wire rfPullPeak,

	    // Driver Enable (Active Low)
	    output logic driverNEN
	    );

  // --- Clock Management ---
  logic clk90_pre, clk40_sys_pre;
  logic clk90, clk40_sys;
  logic fpgaSCLK;
  logic pllLocked;

  SB_GB sclkGbuf (.USER_SIGNAL_TO_GLOBAL_BUFFER(fpgaSCLK_pin), .GLOBAL_BUFFER_OUTPUT(fpgaSCLK));

  // F_VCO = 40 * (17 + 1) = 720 MHz
  // F_OUT_A = 720 / 8 = 90 MHz
  // F_OUT_B = 720 / 18 = 40 MHz (Wait! 720/18 = 40 MHz)
  SB_PLL40_2_PAD #(
		 .FEEDBACK_PATH("SIMPLE"),
		 .DIVR(4'b0000),       
		 .DIVF(7'b0010001),   
		 .DIVQ(3'b011),        // 90 MHz
		 .FILTER_RANGE(3'b011) 
		 ) sysPll (
			   .PACKAGEPIN(clk40), 
			   .PLLOUTCOREA(clk90_pre),
			   .PLLOUTCOREB(clk40_sys_pre),
			   .LOCK(pllLocked),
			   .RESETB(1'b1),
			   .BYPASS(1'b0)
			   );

  SB_GB clk90Gbuf (.USER_SIGNAL_TO_GLOBAL_BUFFER(clk90_pre), .GLOBAL_BUFFER_OUTPUT(clk90));
  SB_GB clk40Gbuf (.USER_SIGNAL_TO_GLOBAL_BUFFER(clk40_sys_pre), .GLOBAL_BUFFER_OUTPUT(clk40_sys));

  // Synchronized resets
  logic rst90, rst40;
  Synchronizer resetSync90 (.clk(clk90), .dIn(!fpgaNRESET), .dOut(rst90));
  Synchronizer resetSync40 (.clk(clk40_sys), .dIn(!fpgaNRESET), .dOut(rst40));

  logic [5:0] ppsGen;
  logic [25:0] ppsCount;
  logic [7:0] powerThresh;
  logic [31:0] tuningWord;
  logic txEnable;

  // FreqCounter now runs at its own 40 MHz clock domain
  FreqCounter freqCounter (
			   .reset(rst40),
			   .clk40(clk40_sys),
			   .fpgaNCS(fpgaNCS),
			   .ppsCount(ppsCount),
			   .ppsGen(ppsGen),
			   .samplePPS(gnssPPS)
			   );

  SPIRegisters spiCore (
			.reset(rst90),
			.fpgaSCLK(fpgaSCLK),
			.fpgaMOSI(fpgaMOSI),
			.fpgaMISO(fpgaMISO),
			.fpgaNCS(fpgaNCS),
			.clk90(clk90),
			.tuningWord(tuningWord),
			.powerThresh(powerThresh),
			.pllLocked(pllLocked),
			.txEnable(txEnable),
			.ppsCount(ppsCount),
			.ppsGen(ppsGen)
			);

  WSPRExciter exciterCore (
			   .reset(rst90),
			   .clk90(clk90),
			   .tuningWord(tuningWord),
			   .powerThreshold(powerThresh),
			   .txEnable(txEnable & pllLocked), 
			   .rfPushBase(rfPushBase),
			   .rfPushPeak(rfPushPeak),
			   .rfPullBase(rfPullBase),
			   .rfPullPeak(rfPullPeak)
			   );

  logic driverEnableState;
  always_ff @(posedge clk90) driverEnableState <= !(txEnable & pllLocked);

  SB_IO #(.PIN_TYPE(6'b010101)) ioDriverNEN (.PACKAGE_PIN(driverNEN), .D_OUT_0(driverEnableState));

endmodule
