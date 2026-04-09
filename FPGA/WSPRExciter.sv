module WSPRExciter (
		    input  logic clk90,
		    input logic reset,
		    input  logic [31:0] tuningWord,
		    input  logic txEnable,

		    // Physical RF Pins (DDR)
		    output logic rfPushBase,
		    output logic rfPushPeak,
		    output logic rfPullBase,
		    output logic rfPullPeak
		    );

  // =========================================================================
  // 1. Domain Crossing & NCO
  // =========================================================================
  
  // Sync txEnable to clk90 domain
  logic txEnable_sync;
  Synchronizer txEnSync (.clk(clk90), .dIn(txEnable), .dOut(txEnable_sync));

  // For tuningWord, we just register it once in clk90. 
  // It's 32-bits so technically could have skew, but for WSPR it doesn't matter.
  logic [31:0] tuningWord_q;
  always_ff @(posedge clk90) tuningWord_q <= tuningWord;

  logic [31:0] ncoAccumulator;
  logic [31:0] ncoHalfStep;
  
  always_ff @(posedge clk90) begin
    if (!txEnable_sync) begin
      ncoAccumulator <= 32'd0;
    end else begin
      ncoAccumulator <= ncoAccumulator + tuningWord_q;
    end
  end

  // DDR falling edge phase
  assign ncoHalfStep = ncoAccumulator + (tuningWord_q >> 1);

  // =========================================================================
  // 2. High-Precision Phase to State Mapping
  // =========================================================================
  // Target: 45 MHz allows single-cycle mult on iCE40 if we are careful.
  // We want (phase[31:16] * 6) >> 16.
  
  logic [2:0] stateHigh;
  logic [2:0] stateLow;

  always_ff @(posedge clk90) begin
    stateHigh <= (32'(ncoAccumulator[31:16]) * 32'd6) >> 16;
    stateLow  <= (32'(ncoHalfStep[31:16])    * 32'd6) >> 16;
  end

  // =========================================================================
  // 3. State Decode to DDR Signals
  // =========================================================================
  // Pipelined decode stage
  logic [1:0] txEnable_pipe;
  always_ff @(posedge clk90) txEnable_pipe <= {txEnable_pipe[0], txEnable_sync};

  logic pushBaseHigh, pushBaseLow;
  logic pushPeakHigh, pushPeakLow;
  logic pullBaseHigh, pullBaseLow;
  logic pullPeakHigh, pullPeakLow;

  always_ff @(posedge clk90) begin
    pushBaseHigh <= 1'b0; pushBaseLow <= 1'b0;
    pushPeakHigh <= 1'b0; pushPeakLow <= 1'b0;
    pullBaseHigh <= 1'b0; pullBaseLow <= 1'b0;
    pullPeakHigh <= 1'b0; pullPeakLow <= 1'b0;

    if (txEnable_pipe[1]) begin
      case (stateHigh)
        3'd0, 3'd2: pushBaseHigh <= 1'b1;
        3'd1:       pushPeakHigh <= 1'b1;
        3'd3, 3'd5: pullBaseHigh <= 1'b1;
        3'd4:       pullPeakHigh <= 1'b1;
        default:    ;
      endcase

      case (stateLow)
        3'd0, 3'd2: pushBaseLow <= 1'b1;
        3'd1:       pushPeakLow <= 1'b1;
        3'd3, 3'd5: pullBaseLow <= 1'b1;
        3'd4:       pullPeakLow <= 1'b1;
        default:    ;
      endcase
    end
  end

  // =========================================================================
  // 4. DDR I/O Instantiation
  // =========================================================================
  SB_IO #( .PIN_TYPE(6'b011001) ) ioPushBase ( .PACKAGE_PIN(rfPushBase), .OUTPUT_CLK(clk90), .D_OUT_0(pushBaseHigh), .D_OUT_1(pushBaseLow) );
  SB_IO #( .PIN_TYPE(6'b011001) ) ioPushPeak ( .PACKAGE_PIN(rfPushPeak), .OUTPUT_CLK(clk90), .D_OUT_0(pushPeakHigh), .D_OUT_1(pushPeakLow) );
  SB_IO #( .PIN_TYPE(6'b011001) ) ioPullBase ( .PACKAGE_PIN(rfPullBase), .OUTPUT_CLK(clk90), .D_OUT_0(pullBaseHigh), .D_OUT_1(pullBaseLow) );
  SB_IO #( .PIN_TYPE(6'b011001) ) ioPullPeak ( .PACKAGE_PIN(rfPullPeak), .OUTPUT_CLK(clk90), .D_OUT_0(pullPeakHigh), .D_OUT_1(pullPeakLow) );

endmodule // WSPRExciter
