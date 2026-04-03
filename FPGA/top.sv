`timescale 1ns / 1ps
/**
 * file: top.sv
 * module: Top
 * description: WSPR-ease Main FPGA Design.
 * 90 MHz RF logic with DDR outputs (180 Msps effective).
 * 45 MHz SPI control domain.
 */

module Top (
    input  logic fpgaClk,    // Pin 15
    input  logic fpgaMOSI,   // Pin 17
    input  logic fpgaNCS,    // Pin 16
    output logic fpgaMISO,   // Pin 14
    input  logic gnssPPS,    // Pin 6
    input  logic clk40MHz,   // Pin 35

    // 74ACT541 Output Enable (Active Low)
    output logic driveNEN,   // Pin 25

    // RF Power Amplifier Drive (DDR Registered)
    output logic rfPushBase, // Pin 42
    output logic rfPushPeak, // Pin 37
    output logic rfPullBase, // Pin 45
    output logic rfPullPeak  // Pin 48
);

  // -------------------------------------------------------------------------
  // 1. Clock Generation
  // -------------------------------------------------------------------------
  logic clk90_raw, clk90;
  logic pllLocked;

  // 90 MHz PLL from 40 MHz TCXO
  SB_PLL40_PAD #(
    .FEEDBACK_PATH("SIMPLE"),
    .DIVR(4'b0000), .DIVF(7'b0010001), .DIVQ(3'b011), 
    .FILTER_RANGE(3'b001)
  ) pllInst (
    .PACKAGEPIN(clk40MHz), .PLLOUTCORE(clk90_raw), .PLLOUTGLOBAL(clk90), .LOCK(pllLocked), .RESETB(1'b1), .BYPASS(1'b0)
  );

  // 45 MHz Control Clock
  logic clk45_div;
  always_ff @(posedge clk90) clk45_div <= !clk45_div;
  logic clk45;
  SB_GB clk45_gb (.USER_SIGNAL_TO_GLOBAL_BUFFER(clk45_div), .GLOBAL_BUFFER_OUTPUT(clk45));

  // -------------------------------------------------------------------------
  // 2. SPI Slave (45 MHz Domain)
  // -------------------------------------------------------------------------
  logic [39:0] spiShift;
  logic [5:0]  spiBits;
  logic [2:0]  sclkS, mosiS, ncsS;
  
  always_ff @(posedge clk45) begin
    sclkS <= {sclkS[1:0], fpgaClk};
    mosiS <= {mosiS[1:0], fpgaMOSI};
    ncsS  <= {ncsS[1:0], fpgaNCS};
  end
  
  wire sclkR = (sclkS[1] && !sclkS[2]);
  wire sclkF = (!sclkS[1] && sclkS[2]);
  wire ncsA  = !ncsS[1];

  logic [6:0] activeAddr;
  logic spiWE;

  always_ff @(posedge clk45) begin
    if (!ncsA) begin
      spiWE <= (spiBits == 40) && spiShift[39];
      spiBits <= 0;
    end else begin
      spiWE <= 0;
      if (sclkR) begin
        spiShift <= {spiShift[38:0], mosiS[1]};
        spiBits <= spiBits + 1'b1;
        if (spiBits == 7) activeAddr <= {spiShift[5:0], mosiS[1]};
      end
    end
  end

  logic [31:0] regControl = 0;
  always_ff @(posedge clk45) if (spiWE && activeAddr == 7'h00) regControl <= spiShift[31:0];

  always_ff @(posedge clk45) driveNEN <= !regControl[0];

  // -------------------------------------------------------------------------
  // 3. Register File (BRAM)
  // -------------------------------------------------------------------------
  logic [15:0] ramB_Lo, ramB_Hi;
  logic [15:0] ramS_Lo, ramS_Hi;
  
  // Bank B for RF synthesis (45MHz write, 90MHz read)
  SB_RAM40_4K regBankB_Lo (.WADDR({4'd0, activeAddr}), .WDATA(spiShift[15:0]), .WE(spiWE), .WCLKE(1'b1), .WCLK(clk45), .RADDR(11'h01), .RDATA(ramB_Lo), .RE(1'b1), .RCLKE(1'b1), .RCLK(clk90));
  SB_RAM40_4K regBankB_Hi (.WADDR({4'd0, activeAddr}), .WDATA(spiShift[31:16]), .WE(spiWE), .WCLKE(1'b1), .WCLK(clk45), .RADDR(11'h01), .RDATA(ramB_Hi), .RE(1'b1), .RCLKE(1'b1), .RCLK(clk90));
  
  // Bank S for SPI readback (45MHz write, 45MHz read)
  SB_RAM40_4K regBankS_Lo (.WADDR({4'd0, activeAddr}), .WDATA(spiShift[15:0]), .WE(spiWE), .WCLKE(1'b1), .WCLK(clk45), .RADDR({4'd0, activeAddr}), .RDATA(ramS_Lo), .RE(1'b1), .RCLKE(1'b1), .RCLK(clk45));
  SB_RAM40_4K regBankS_Hi (.WADDR({4'd0, activeAddr}), .WDATA(spiShift[31:16]), .WE(spiWE), .WCLKE(1'b1), .WCLK(clk45), .RADDR({4'd0, activeAddr}), .RDATA(ramS_Hi), .RE(1'b1), .RCLKE(1'b1), .RCLK(clk45));

  logic [31:0] regPPSFall, regPPSRise;
  logic [31:0] spiRdData;
  always_comb begin
    case (activeAddr)
      7'h00:   spiRdData = regControl;
      7'h01:   spiRdData = {ramS_Hi, ramS_Lo};
      7'h03:   spiRdData = regPPSFall;
      7'h09:   spiRdData = regPPSRise;
      7'h0B:   spiRdData = 32'h0000600D;
      default: spiRdData = 32'hDEADBEEF;
    endcase
  end

  logic [31:0] spiRd_q;
  always_ff @(posedge clk45) spiRd_q <= spiRdData;

  logic [31:0] misoSR;
  always_ff @(posedge clk45) begin
    if (sclkF) begin
      if (spiBits == 9) misoSR <= spiRd_q;
      else misoSR <= {misoSR[30:0], 1'b0};
    end
  end
  SB_IO #(.PIN_TYPE(6'b101001)) ioMISO(.PACKAGE_PIN(fpgaMISO), .D_OUT_0(misoSR[31]), .OUTPUT_ENABLE(ncsA), .OUTPUT_CLK(clk45));

  // -------------------------------------------------------------------------
  // 4. RF Synthesis (90 MHz logic -> 180 Msps DDR)
  // -------------------------------------------------------------------------
  logic [31:0] tw;
  always_ff @(posedge clk90) tw <= {ramB_Hi, ramB_Lo};
  wire [31:0] tw2 = tw << 1;

  logic [3:0] a [7:0];
  logic c [7:0];
  logic [3:0] tp [7:0][7:0]; 
  
  always_ff @(posedge clk90) begin
    {c[0], a[0]} <= a[0] + tw2[3:0];
    for (int i=1; i<8; i++) tp[0][i] <= tw2[i*4 +: 4];
    for (int s=1; s<8; s++) begin
      {c[s], a[s]} <= a[s] + tp[s-1][s] + c[s-1];
      for (int i=s+1; i<8; i++) tp[s][i] <= tp[s-1][i];
    end
  end

  logic [3:0] sa [7:0];
  logic sc [7:0];
  logic [3:0] twp [7:0][7:0];
  
  always_ff @(posedge clk90) begin
    {sc[0], sa[0]} <= a[0] + tw[3:0];
    for (int i=1; i<8; i++) twp[0][i] <= tw[i*4 +: 4];
    for (int s=1; s<8; s++) begin
      {sc[s], sa[s]} <= a[s] + twp[s-1][s] + sc[s-1];
      for (int i=s+1; i<8; i++) twp[s][i] <= twp[s-1][i];
    end
  end

  logic cB_q;
  always_ff @(posedge clk90) cB_q <= c[7];

  logic [5:0] rc = 6'b000001;
  logic [5:0] rcMid;
  logic [5:0] rcNext;
  
  always_comb begin
    rcNext = rc;
    if (sc[7])  rcNext = {rcNext[4:0], rcNext[5]};
    if (cB_q)   rcNext = {rcNext[4:0], rcNext[5]};
  end

  logic pllLocked_sync;
  always_ff @(posedge clk90) pllLocked_sync <= pllLocked;

  always_ff @(posedge clk90) begin
    if (!pllLocked_sync) rc <= 6'b000001;
    else begin
      if (sc[7] && cB_q) rc <= {rc[3:0], rc[5:4]}; // Advance twice if both carried
      else if (cB_q) rc <= {rc[4:0], rc[5]}; // Advance once if total carried
    end
  end
  always_ff @(posedge clk90) rcMid <= sc[7] ? {rc[4:0], rc[5]} : rc;

  logic drvB0, drvB1, drvP0, drvP1, drvL0, drvL1, drvK0, drvK1;
  always_ff @(posedge clk90) begin
    drvB0 <= (rc[0] | rc[2]);
    drvB1 <= (rcMid[0] | rcMid[2]);
    drvP0 <= rc[1];
    drvP1 <= rcMid[1];
    drvL0 <= (rc[3] | rc[5]);
    drvL1 <= (rcMid[3] | rcMid[5]);
    drvK0 <= rc[4];
    drvK1 <= rcMid[4];
  end

  logic drvB1n, drvP1n, drvL1n, drvK1n;
  always_ff @(negedge clk90) begin
    drvB1n <= drvB1; drvP1n <= drvP1; drvL1n <= drvL1; drvK1n <= drvK1;
  end

  SB_IO #(.PIN_TYPE(6'b010101)) ioPushB(.PACKAGE_PIN(rfPushBase), .D_OUT_0(drvB0), .D_OUT_1(drvB1n), .OUTPUT_CLK(clk90), .OUTPUT_ENABLE(1'b1));
  SB_IO #(.PIN_TYPE(6'b010101)) ioPushP(.PACKAGE_PIN(rfPushPeak), .D_OUT_0(drvP0), .D_OUT_1(drvP1n), .OUTPUT_CLK(clk90), .OUTPUT_ENABLE(1'b1));
  SB_IO #(.PIN_TYPE(6'b010101)) ioPullB(.PACKAGE_PIN(rfPullBase), .D_OUT_0(drvL0), .D_OUT_1(drvL1n), .OUTPUT_CLK(clk90), .OUTPUT_ENABLE(1'b1));
  SB_IO #(.PIN_TYPE(6'b010101)) ioPullP(.PACKAGE_PIN(rfPullPeak), .D_OUT_0(drvK0), .D_OUT_1(drvK1n), .OUTPUT_CLK(clk90), .OUTPUT_ENABLE(1'b1));

  // -------------------------------------------------------------------------
  // 5. PPS Timing (45 MHz domain)
  // -------------------------------------------------------------------------
  logic [31:0] ppsCounter;
  always_ff @(posedge clk45) ppsCounter <= ppsCounter + 1'b1;

  logic [2:0] ppsS;
  always_ff @(posedge clk45) begin
    ppsS <= {ppsS[1:0], gnssPPS};
    if (ppsS[1] && !ppsS[2]) regPPSRise <= ppsCounter;
    if (!ppsS[1] && ppsS[2]) regPPSFall <= ppsCounter;
  end

endmodule
