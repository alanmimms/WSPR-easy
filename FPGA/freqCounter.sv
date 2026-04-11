`timescale 1ns / 100ps

module FreqCounter (
		    input logic clk90,
		    input logic reset,
		    input logic fpgaNCS,
		    input logic samplePPS,
		    output logic [26:0] ppsCount,
		    output logic [4:0] ppsGen
		    );

  // 4-stage pipelined counter to comfortably hit 90MHz
  reg [6:0] c0 = 0, c1 = 0, c2 = 0, c3 = 0;
  reg w0, w1, w2;

  always_ff @(posedge clk90) begin
    c0 <= c0 + 7'd1;
    w0 <= (c0 == 7'h7E);
    
    if (w0) begin
      c1 <= c1 + 7'd1;
      w1 <= (c1 == 7'h7E);
    end else begin
      w1 <= 1'b0;
    end

    if (w1) begin
      c2 <= c2 + 7'd1;
      w2 <= (c2 == 7'h7E);
    end else begin
      w2 <= 1'b0;
    end

    if (w2) begin
      c3 <= c3 + 7'd1;
    end
  end

  wire [27:0] currentCount = {c3, c2, c1, c0};

  logic syncPPS;
  logic risingPPS;
  Synchronizer ppsSyncronizer (.clk(clk90), .dIn(samplePPS), .dOut(syncPPS));
  edgeDetector ppsDetector (.clk(clk90), .sigIn(syncPPS), .risingOut(risingPPS));

  // Initialize outputs
  initial begin
    ppsGen = 0;
    ppsCount = 0;
  end

  always_ff @(posedge clk90) begin
    if (reset) begin
      ppsGen <= 0;
      ppsCount <= 0;
    end else if (risingPPS) begin
      ppsGen <= ppsGen + 5'd1;
      ppsCount <= currentCount[26:0];
    end
  end

endmodule // FreqCounter
