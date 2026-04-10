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
		     input  logic pllLocked,
		     output logic txEnable,

		     // Frequency counter values (from clk90 domain)
		     input logic [25:0] ppsCount,
		     input logic [5:0] ppsGen
		     );

  logic [5:0] bitCount = 0;
  logic isWrite = 0;
  logic [6:0] selAddr = 0;
  logic [31:0] readBuf = 0;
  logic [31:0] writeBuf = 0;

  tWSPRControl ctrlReg = initWSPRControl;

  // Synchronization for status bits
  logic pllLockedSync;
  Synchronizer pllSync (.clk(clk90), .dIn(pllLocked), .dOut(pllLockedSync));

  logic txEnRaw;
  assign txEnRaw = ctrlReg.txEnable;
  Synchronizer txEnOutSync (.clk(clk90), .dIn(txEnRaw), .dOut(txEnable));

  // SPI write sampling (SPI Domain)
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
        case (selAddr)
          aWSPRControl: ctrlReg <= {writeBuf[30:0], fpgaMOSI};
          aWSPRTuning:  tuningWord <= {writeBuf[30:0], fpgaMOSI};
        endcase
      end
      bitCount <= bitCount + 1;
    end
  end

  // SPI read sampling (SPI Domain)
  // We sample clk90 domain signals here. They might be skewed but at 12MHz it's safe.
  always_ff @(negedge fpgaSCLK or posedge fpgaNCS) begin
    if (fpgaNCS) begin
      readBuf <= '0;
      fpgaMISO <= '0;
    end else begin
      if (bitCount == 8 && !isWrite) begin
        logic [31:0] v;
        case (selAddr)
          aWSPRControl: v = {ctrlReg.powerThresh, 22'd0, pllLockedSync, ctrlReg.txEnable};
          aWSPRTuning:  v = tuningWord;
          aWSPRPPS:     v = {ppsGen, ppsCount};
          aWSPRSig:     v = eWSPRSigVal;
          default:      v = 32'hDEADBEEF;
        endcase
        fpgaMISO <= v[31];
        readBuf  <= {v[30:0], 1'b0};
      end else if (bitCount > 8) begin
        fpgaMISO <= readBuf[31];
        readBuf  <= {readBuf[30:0], 1'b0};
      end
    end
  end

endmodule
