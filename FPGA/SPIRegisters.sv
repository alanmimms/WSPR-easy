`timescale 1ns / 100ps
`include "regs.sv"

module SPIRegisters (
		     input logic reset,

		     // SPI Interface
		     input  logic fpgaSCLK,
		     input  logic fpgaMOSI,
		     output logic fpgaMISO,
		     input  logic fpgaNCS,

		     // Destination Domain (90 MHz)
		     input  logic clk_dest,
		     output logic [31:0] tuningWord = 0,
		     output logic [7:0] powerThresh = 8'hFF,
		     input  logic pllLocked,
		     output logic txEnable,

		     // Frequency counter values (from 90 MHz domain)
		     input logic [26:0] ppsCount,
		     input logic [4:0] ppsGen
		     );

  // --- SPI Domain ---
  logic [31:0] twRaw = 0;
  logic [5:0] bitCount = 0;
  logic isWrite = 0;
  logic [6:0] selAddr = 0;
  logic [31:0] writeBuf = 0;
  tWSPRControl ctrlSPI = initWSPRControl;

  always_ff @(posedge fpgaSCLK or posedge fpgaNCS) begin
    if (fpgaNCS) begin
      bitCount <= '0;
    end else begin
      writeBuf <= {writeBuf[30:0], fpgaMOSI};
      if (bitCount == 0) begin
        isWrite <= fpgaMOSI;
      end else if (bitCount < 8) begin
        selAddr <= {selAddr[5:0], fpgaMOSI};
      end else if (bitCount == 39 && isWrite) begin
        if (selAddr == aWSPRControl) ctrlSPI <= {writeBuf[30:0], fpgaMOSI};
        if (selAddr == aWSPRTuning)  twRaw   <= {writeBuf[30:0], fpgaMOSI};
      end
      bitCount <= bitCount + 1;
    end
  end

  assign fpgaMISO = 1'b0;

  // --- Destination Domain Sync (90 MHz) ---
  logic ncs_s1, ncs_s2, ncs_s3;
  logic [31:0] tw_sync;
  logic [7:0]  pwr_sync;
  logic        tx_sync;

  always_ff @(posedge clk_dest) begin
    ncs_s1 <= fpgaNCS;
    ncs_s2 <= ncs_s1;
    ncs_s3 <= ncs_s2;

    if (reset) begin
      tw_sync <= 0;
      pwr_sync <= 8'hFF;
      tx_sync <= 0;
      tuningWord <= 0;
      powerThresh <= 8'hFF;
      txEnable <= 0;
    end else begin
      if (ncs_s2 && !ncs_s3) begin 
        tw_sync <= twRaw;
        pwr_sync <= ctrlSPI.powerThresh;
        tx_sync <= ctrlSPI.txEnable;
      end
      // Registration stage
      tuningWord <= tw_sync;
      powerThresh <= pwr_sync;
      txEnable <= tx_sync;
    end
  end

endmodule
