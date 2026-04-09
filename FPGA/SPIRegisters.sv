`include "regs.sv"

module SPIRegisters (
		    input logic reset,

		     // SPI Interface
		     input  logic fpgaSCLK,
		     input  logic fpgaMOSI,
		     output logic fpgaMISO,
		     input  logic fpgaNCS,

		     // 45 MHz Domain & Control
		     input  logic clk90,
		     output logic [31:0] tuningWord = 0,
		     input  logic pllLocked,
		     output logic txEnable,

		     // Frequency counter values
		     input logic [25:0] ppsCount,
		     input logic [5:0] ppsGen
		     );

  logic [5:0] bitCount = 0;
  logic isWrite = 0;
  logic [6:0] selAddr = 0;
  logic [31:0] readBuf = 0;
  logic [31:0] writeBuf = 0;

  // Use the generated struct for internal control register image
  tWSPRControl ctrlReg = initWSPRControl;

  // Sync pllLocked to clk90 for status reporting
  logic pllLocked_sync;
  Synchronizer pllSync (.clk(clk90), .dIn(pllLocked), .dOut(pllLocked_sync));

  // Shadow struct for reads
  tWSPRControl readCtrlReg;
  always_comb begin
    readCtrlReg = ctrlReg;
    readCtrlReg.pllLocked = pllLocked_sync;
  end

  // Synchronize to output
  assign txEnable = ctrlReg.txEnable;

  // SPI write sampling - leading edge of fpgaSCLK
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
          aWSPRTuning: tuningWord <= {writeBuf[30:0], fpgaMOSI};
        endcase
      end
      bitCount <= bitCount + 1;
    end
  end

  // Register the read values to break timing paths
  logic [31:0] regWSPRControl;
  logic [31:0] regWSPRTuning;
  logic [31:0] regWSPRPPS;
  
  always_ff @(posedge clk90) begin
    regWSPRControl <= readCtrlReg;
    regWSPRTuning  <= tuningWord;
    regWSPRPPS     <= {ppsGen, ppsCount}; // Corrected field order for tWSPRPPS
  end

  // SPI read sampling on falling edge of fpgaSCLK
  always_ff @(negedge fpgaSCLK or posedge fpgaNCS) begin
    if (fpgaNCS) begin
      readBuf <= '0;
      fpgaMISO <= '0;
    end else begin
      if (bitCount == 8 && !isWrite) begin
        logic [31:0] nextReadVal;
        case (selAddr)
          aWSPRControl: nextReadVal = regWSPRControl;
          aWSPRTuning:  nextReadVal = regWSPRTuning;
          aWSPRPPS:     nextReadVal = regWSPRPPS;
          aWSPRSig:     nextReadVal = eWSPRSigVal;
          default:      nextReadVal = 32'hDEADBEEF;
        endcase
        fpgaMISO <= nextReadVal[31];
        readBuf  <= {nextReadVal[30:0], 1'b0};
      end else if (bitCount > 8) begin
        fpgaMISO <= readBuf[31];
        readBuf  <= {readBuf[30:0], 1'b0};
      end
    end
  end

endmodule // SPIRegisters
