`timescale 1ns / 100ps
`include "regs.sv"

module SPIRegisters (
		     input logic reset,

		     // SPI Interface
		     input  logic fpgaSCLK,
		     input  logic fpgaMOSI,
		     output logic fpgaMISO,
		     input  logic fpgaNCS,

		     // 90 MHz Domain & Control
		     input  logic clk90,
		     output logic [31:0] tuningWord = 0,
		     output logic [7:0] powerThresh = 8'hFF,
		     input  logic pllLocked,
		     output logic txEnable,

		     // Frequency counter values (from clk90 domain)
		     input logic [25:0] ppsCount,
		     input logic [5:0] ppsGen
		     );

  // --- SPI Domain Logic ---
  logic [31:0] twRaw = 0;
  logic [5:0] bitCount = 0;
  logic isWrite = 0;
  logic [6:0] selAddr = 0;
  logic [31:0] readBuf = 0;
  logic [31:0] writeBuf = 0;

  // SPI-writable control shadow (SPI Domain)
  tWSPRControl ctrlSPI = initWSPRControl;
  
  // --- Cross-Domain Synchronization ---
  
  // 1. Status bits from clk90 -> SPI Domain
  logic pllLockedSPI;
  Synchronizer pllSyncSPI_highspeed (.clk(fpgaSCLK), .dIn(pllLocked), .dOut(pllLockedSPI));

  // 2. Control bits from SPI -> clk90 Domain
  logic ncsSync, ncsRising;
  Synchronizer ncsSync90 (.clk(clk90), .dIn(fpgaNCS), .dOut(ncsSync));
  edgeDetector ncsEdge90 (.clk(clk90), .sigIn(ncsSync), .risingOut(ncsRising));

  // Double-registration to break cross-domain timing paths
  logic [31:0] tw_s1, tw_s2;
  logic [7:0]  pwr_s1, pwr_s2;
  logic        tx_s1, tx_s2;

  always_ff @(posedge clk90) begin
    tw_s1 <= twRaw; tw_s2 <= tw_s1;
    pwr_s1 <= ctrlSPI.powerThresh; pwr_s2 <= pwr_s1;
    tx_s1 <= ctrlSPI.txEnable; tx_s2 <= tx_s1;

    if (ncsRising || reset) begin
      tuningWord  <= tw_s2;
      powerThresh <= pwr_s2;
      txEnable    <= tx_s2;
    end
  end

  // --- SPI Protocol Machine ---
  
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

  // Readback logic
  logic [31:0] vRead;
  always_comb begin
    vRead = 32'hDEADBEEF;
    case (selAddr)
      aWSPRControl: begin
        vRead = ctrlSPI;
        vRead[1] = pllLockedSPI;
      end
      aWSPRTuning:  vRead = twRaw;
      aWSPRPPS:     vRead = {ppsGen, ppsCount};
      aWSPRSig:     vRead = eWSPRSigVal;
    endcase
  end

  always_ff @(negedge fpgaSCLK or posedge fpgaNCS) begin
    if (fpgaNCS) begin
      fpgaMISO <= 1'b0;
    end else begin
      if (bitCount == 8) begin
        fpgaMISO <= vRead[31];
        readBuf  <= {vRead[30:0], 1'b0};
      end else if (bitCount > 8) begin
        fpgaMISO <= readBuf[31];
        readBuf  <= {readBuf[30:0], 1'b0};
      end
    end
  end

endmodule
